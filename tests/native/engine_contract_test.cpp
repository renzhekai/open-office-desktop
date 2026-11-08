#include "engine/office_engine.h"
#include "engine/onlyoffice_engine_adapter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>
#include <QWidget>

#include <memory>

using namespace openoffice;

class EngineContractTest final : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        const QDir workingDirectory = QDir::current();
        temporaryDirectory_ = std::make_unique<QTemporaryDir>(
            workingDirectory.absoluteFilePath(QStringLiteral("engine-contract-XXXXXX")));
        // Cleanup may only remove the fresh directory created for this test.
        temporaryDirectory_->setAutoRemove(false);
        QVERIFY(temporaryDirectory_->isValid());
        QCOMPARE(QFileInfo(temporaryDirectory_->path()).absolutePath(),
                 workingDirectory.absolutePath());
        temporaryDirectory_->setAutoRemove(true);
    }

    void cleanup()
    {
        temporaryDirectory_.reset();
    }

    void unavailableEngineAdvertisesNoCapabilities()
    {
        OnlyOfficeEngineAdapter engine;
        const auto capabilities = engine.capabilities();
        QVERIFY(!capabilities.available);
        QVERIFY(!capabilities.open);
        QVERIFY(!capabilities.edit);
        QVERIFY(!capabilities.save);
        QVERIFY(!capabilities.create);
        QVERIFY(!capabilities.saveAs);
        QVERIFY(!engine.canCreate(DocumentKind::Text));
        QVERIFY(!engine.documentModified());
    }

    void openRefusesWithoutChangingAnExistingDocument()
    {
        const QString source = temporaryDirectory_->filePath(QStringLiteral("original.docx"));
        QByteArray original("opaque fixture bytes");
        original.append('\0');
        original.append("with a binary byte");
        writeFixture(source, original);

        OnlyOfficeEngineAdapter engine;
        const EngineResult result = engine.requestOpen(1, source, false);

        QVERIFY(!result.ok);
        QCOMPARE(result.code, QStringLiteral("engine_unavailable"));
        QVERIFY(!result.message.isEmpty());
        QCOMPARE(readFixture(source), original);
        QVERIFY(!engine.capabilities().available);
    }

    void openRefusesMissingAndEmptyPaths()
    {
        OnlyOfficeEngineAdapter engine;
        quint64 requestId = 1;
        for (const auto& path : {QString(), temporaryDirectory_->filePath(QStringLiteral("missing.xlsx"))}) {
            const auto result = engine.requestOpen(requestId++, path, true);
            QVERIFY(!result.ok);
            QCOMPARE(result.code, QStringLiteral("engine_unavailable"));
        }
        QVERIFY(QDir(temporaryDirectory_->path()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    }

    void saveDoesNotCreateAnyFileOrDirectory()
    {
        OnlyOfficeEngineAdapter engine;
        const QString target = temporaryDirectory_->filePath(QStringLiteral("new-folder/output.docx"));
        const auto result = engine.requestSave(1, target);

        QVERIFY(!result.ok);
        QCOMPARE(result.code, QStringLiteral("engine_unavailable"));
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(!QFileInfo::exists(QFileInfo(target).absolutePath()));
    }

    void saveDoesNotOverwriteExistingDocument()
    {
        const QString target = temporaryDirectory_->filePath(QStringLiteral("existing.pptx"));
        const QByteArray original("keep the existing document exactly");
        writeFixture(target, original);

        OnlyOfficeEngineAdapter engine;
        const auto result = engine.requestSave(1, target);

        QVERIFY(!result.ok);
        QCOMPARE(result.code, QStringLiteral("engine_unavailable"));
        QCOMPARE(readFixture(target), original);
    }

    void createRejectsEveryKind_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("text") << static_cast<int>(DocumentKind::Text);
        QTest::newRow("spreadsheet") << static_cast<int>(DocumentKind::Spreadsheet);
        QTest::newRow("presentation") << static_cast<int>(DocumentKind::Presentation);
        QTest::newRow("pdf") << static_cast<int>(DocumentKind::Pdf);
        QTest::newRow("unknown") << static_cast<int>(DocumentKind::Unknown);
    }

    void createRejectsEveryKind()
    {
        QFETCH(int, kind);
        OnlyOfficeEngineAdapter engine;
        const auto result = engine.requestCreate(1, static_cast<DocumentKind>(kind), QStringLiteral("文档1.docx"), false);

        QVERIFY(!result.ok);
        QCOMPARE(result.code, QStringLiteral("engine_unavailable"));
        QVERIFY(!engine.capabilities().create);
        QVERIFY(QDir(temporaryDirectory_->path()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    }

    void unavailableEngineDoesNotCreateAFakeEditor()
    {
        OnlyOfficeEngineAdapter engine;
        QWidget parent;
        const auto childrenBefore = parent.children();

        QVERIFY(engine.createEditorWidget(&parent) == nullptr);
        QVERIFY(engine.createEditorWidget(nullptr) == nullptr);
        QCOMPARE(parent.children(), childrenBefore);
    }

    void rejectedRequestsEmitNoCompletionOrStateChanges()
    {
        OnlyOfficeEngineAdapter engine;
        QSignalSpy completions(&engine, &OfficeEngine::operationFinished);
        QSignalSpy modifications(&engine, &OfficeEngine::modifiedChanged);
        QSignalSpy capabilities(&engine, &OfficeEngine::capabilitiesChanged);
        QVERIFY(completions.isValid());
        QVERIFY(modifications.isValid());
        QVERIFY(capabilities.isValid());

        const QString path = temporaryDirectory_->filePath(QStringLiteral("unavailable.docx"));
        QVERIFY(!engine.requestOpen(41, path, false).ok);
        QVERIFY(!engine.requestSave(42, path).ok);
        QVERIFY(!engine.requestCreate(43, DocumentKind::Text, QStringLiteral("文档1.docx"), true).ok);
        engine.cancelSaveDestination(); // No editor is waiting: must be a no-op.
        QVERIFY(!engine.supportsEditorCommand(QStringLiteral("text.insert")));
        const auto command = engine.requestEditorCommand(44, QStringLiteral("text.insert"),
                                                         {{QStringLiteral("text"), QStringLiteral("x")}});
        QVERIFY(!command.ok);
        QCOMPARE(command.code, QStringLiteral("engine_unavailable"));

        QCOMPARE(completions.count(), 0);
        // Give queued callbacks an event-loop turn: refusal must not schedule a
        // later success/failure completion or mutate a nonexistent session.
        QVERIFY(!completions.wait(20));
        QCOMPARE(completions.count(), 0);
        QCOMPARE(modifications.count(), 0);
        QCOMPARE(capabilities.count(), 0);
        QVERIFY(!engine.documentModified());
        QVERIFY(!QFileInfo::exists(path));
    }

    void classifyExistingFilesByCaseInsensitiveExtension_data()
    {
        QTest::addColumn<QString>("filename");
        QTest::addColumn<int>("kind");
        QTest::newRow("docx") << QStringLiteral("中文.DOCX") << static_cast<int>(DocumentKind::Text);
        QTest::newRow("xlsx") << QStringLiteral("values.Xlsx") << static_cast<int>(DocumentKind::Spreadsheet);
        QTest::newRow("pptx") << QStringLiteral("slides.pptx") << static_cast<int>(DocumentKind::Presentation);
        QTest::newRow("pdf") << QStringLiteral("read.pdf") << static_cast<int>(DocumentKind::Pdf);
        QTest::newRow("unknown") << QStringLiteral("other.bin") << static_cast<int>(DocumentKind::Unknown);
    }

    void classifyExistingFilesByCaseInsensitiveExtension()
    {
        QFETCH(QString, filename);
        QFETCH(int, kind);
        const QString path = temporaryDirectory_->filePath(filename);
        // This intentionally is not valid Office content: the helper promises
        // only extension classification, never package validity or editor support.
        writeFixture(path, QByteArray("extension classification only"));

        QCOMPARE(static_cast<int>(classifyDocument(path)), kind);
    }

    void fileNameClassificationNeedsNoExistingFile()
    {
        // Save destinations are classified before they exist.
        const QString missing = temporaryDirectory_->filePath(QStringLiteral("新文档.DOCX"));
        QCOMPARE(static_cast<int>(classifyFileName(missing)), static_cast<int>(DocumentKind::Text));
        QCOMPARE(static_cast<int>(classifyFileName(QStringLiteral("book.xlsx"))), static_cast<int>(DocumentKind::Spreadsheet));
        QCOMPARE(static_cast<int>(classifyFileName(QStringLiteral("notes.txt"))), static_cast<int>(DocumentKind::Unknown));
        QVERIFY(!QFileInfo::exists(missing));
    }

    void classificationRejectsMissingPathsAndDirectories()
    {
        const QString directory = temporaryDirectory_->filePath(QStringLiteral("folder.docx"));
        QVERIFY(QDir().mkdir(directory));

        QVERIFY(classifyDocument(directory) == DocumentKind::Unknown);
        QVERIFY(classifyDocument(temporaryDirectory_->filePath(QStringLiteral("missing.docx"))) == DocumentKind::Unknown);
        QVERIFY(classifyDocument(QString()) == DocumentKind::Unknown);
    }

private:
    static void writeFixture(const QString& path, const QByteArray& contents)
    {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::NewOnly), qPrintable(file.errorString()));
        QCOMPARE(file.write(contents), static_cast<qint64>(contents.size()));
        file.close();
    }

    static QByteArray readFixture(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return file.readAll();
    }

    std::unique_ptr<QTemporaryDir> temporaryDirectory_;
};

QTEST_MAIN(EngineContractTest)
#include "engine_contract_test.moc"
