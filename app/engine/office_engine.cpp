#include "office_engine.h"

#include <QFileInfo>

namespace openoffice {

OfficeEngine::OfficeEngine(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<openoffice::DocumentKind>("openoffice::DocumentKind");
    qRegisterMetaType<openoffice::EngineOperation>("openoffice::EngineOperation");
    qRegisterMetaType<openoffice::EngineResult>("openoffice::EngineResult");
}

EngineResult OfficeEngine::requestClose(quint64 requestId, bool discardChanges)
{
    Q_UNUSED(requestId);
    Q_UNUSED(discardChanges);
    return {false, QStringLiteral("unsupported"), QStringLiteral("当前编辑引擎不支持异步关闭。")};
}

EngineResult OfficeEngine::requestEditorCommand(quint64 requestId, const QString& name,
                                               const QJsonObject& arguments)
{
    Q_UNUSED(requestId);
    Q_UNUSED(name);
    Q_UNUSED(arguments);
    return {false, QStringLiteral("unsupported"), QStringLiteral("当前编辑引擎不支持编辑器命令。")};
}

DocumentKind classifyDocument(const QString& path)
{
    const QFileInfo file(path);
    if (!file.exists() || !file.isFile() || !file.isReadable()) {
        return DocumentKind::Unknown;
    }
    return classifyFileName(path);
}

DocumentKind classifyFileName(const QString& path)
{
    const QString extension = QFileInfo(path).suffix().toLower();
    if (extension == QStringLiteral("docx")) {
        return DocumentKind::Text;
    }
    if (extension == QStringLiteral("xlsx")) {
        return DocumentKind::Spreadsheet;
    }
    if (extension == QStringLiteral("pptx")) {
        return DocumentKind::Presentation;
    }
    if (extension == QStringLiteral("pdf")) {
        return DocumentKind::Pdf;
    }
    return DocumentKind::Unknown;
}

} // namespace openoffice
