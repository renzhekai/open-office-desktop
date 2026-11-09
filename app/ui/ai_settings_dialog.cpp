#include "ai_settings_dialog.h"

#include "assistant/local_assistant.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace openoffice {
namespace {

QLabel* text(const QString& value, const char* name, QWidget* parent)
{
    auto* label = new QLabel(value, parent);
    label->setObjectName(QString::fromLatin1(name));
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

QPushButton* button(const QString& title, const char* name, QWidget* parent)
{
    auto* widget = new QPushButton(title, parent);
    widget->setObjectName(QString::fromLatin1(name));
    widget->setCursor(Qt::PointingHandCursor);
    return widget;
}

bool onSystemDrive(const QString& folder)
{
    const QString system = qEnvironmentVariable("SystemDrive", QStringLiteral("C:")).toUpper();
    return QDir::fromNativeSeparators(folder).toUpper().startsWith(system);
}

} // namespace

AiSettingsDialog::AiSettingsDialog(LocalAssistant* assistant, QWidget* parent) : QDialog(parent), m_ai(assistant)
{
    setObjectName(QStringLiteral("aiSettingsDialog"));
    setWindowTitle(QStringLiteral("本地 AI 设置"));
    setMinimumWidth(560);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(10);

    auto* intro = text(QStringLiteral("AI 助手在这台电脑上运行，对话和文档内容不会发到网上。模型是可选的：不下载也能正常使用办公功能。"),
                       "aiSettingsIntro", this);
    layout->addWidget(intro);

    layout->addWidget(text(QStringLiteral("引擎"), "aiSettingsHeading", this));
    m_engine = text(QString(), "aiSettingsEngine", this);
    layout->addWidget(m_engine);

    layout->addWidget(text(QStringLiteral("模型存放位置"), "aiSettingsHeading", this));
    auto* folderRow = new QHBoxLayout;
    m_folder = text(QString(), "aiSettingsFolder", this);
    m_folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
    folderRow->addWidget(m_folder, 1);
    auto* change = button(QStringLiteral("更改…"), "aiSettingsChangeFolder", this);
    connect(change, &QPushButton::clicked, this, &AiSettingsDialog::chooseFolder);
    folderRow->addWidget(change);
    auto* open = button(QStringLiteral("打开"), "aiSettingsOpenFolder", this);
    connect(open, &QPushButton::clicked, this, [] {
        QDir().mkpath(modelFolder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(modelFolder()));
    });
    folderRow->addWidget(open);
    layout->addLayout(folderRow);
    m_folderNote = text(QString(), "aiSettingsFolderNote", this);
    layout->addWidget(m_folderNote);

    layout->addWidget(text(QStringLiteral("模型（来自魔搭 ModelScope，Qwen 官方发布）"), "aiSettingsHeading", this));
    m_rows = new QVBoxLayout;
    m_rows->setSpacing(8);
    layout->addLayout(m_rows);
    m_progress = text(QString(), "aiSettingsProgress", this);
    layout->addWidget(m_progress);

    layout->addWidget(text(QStringLiteral("高级"), "aiSettingsHeading", this));
    m_ollama = new QCheckBox(QStringLiteral("改用本机已安装的 Ollama（要另外安装 Ollama，模型也要在 Ollama 里下载）"), this);
    m_ollama->setObjectName(QStringLiteral("aiSettingsUseOllama"));
    connect(m_ollama, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked == !m_ai->builtin()) return;
        m_ai->setBackend(checked);
    });
    layout->addWidget(m_ollama);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* close = button(QStringLiteral("关闭"), "aiSettingsClose", this);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    setStyleSheet(QStringLiteral(
        "QLabel#aiSettingsHeading { font-weight: 600; margin-top: 6px; }"
        "QLabel#aiSettingsIntro, QLabel#aiSettingsFolderNote, QLabel#aiSettingsNote, QLabel#aiSettingsProgress { color: #6E6E73; }"
        "QFrame#aiSettingsModel { border: 1px solid rgba(0,0,0,0.12); border-radius: 10px; }"
        "QLabel#aiSettingsBadge { color: #0A84FF; font-weight: 600; }"));

    connect(m_ai, &LocalAssistant::modelsChanged, this, &AiSettingsDialog::rebuild);
    connect(m_ai, &LocalAssistant::stateChanged, this, &AiSettingsDialog::rebuild);
    connect(m_ai, &LocalAssistant::downloadProgress, this, &AiSettingsDialog::updateProgress);
    rebuild();
}

QString AiSettingsDialog::engineText() const { return m_engine->text(); }
QString AiSettingsDialog::folderText() const { return m_folder->text(); }

QStringList AiSettingsDialog::rowTexts() const
{
    QStringList rows;
    for (QFrame* row : findChildren<QFrame*>(QStringLiteral("aiSettingsModel"))) {
        QStringList parts;
        for (QLabel* label : row->findChildren<QLabel*>()) parts << label->text();
        for (QPushButton* action : row->findChildren<QPushButton*>())
            if (action->isEnabled()) parts << QLatin1Char('[') + action->text() + QLatin1Char(']');
        rows << parts.join(QLatin1Char(' '));
    }
    return rows;
}

void AiSettingsDialog::rebuild()
{
    // Engine line.
    if (!m_ai->builtin()) {
        m_engine->setText(QStringLiteral("正在使用本机的 Ollama（在“高级”里可以改回内置引擎）。"));
    } else if (!m_ai->engine()->available()) {
        m_engine->setText(QStringLiteral("没有找到内置引擎：程序目录里应该有 ai-engine 文件夹。可以重新安装 Open Office。"));
    } else {
        const LocalEngine* engine = m_ai->engine();
        QString status = engine->isReady() ? QStringLiteral("已启动") : engine->isStarting() ? QStringLiteral("正在启动…")
                                                                                              : QStringLiteral("用到时自动启动");
        if (m_ai->lastSpeed() > 0)
            status += QStringLiteral("，上次每秒约写 %1 个词").arg(qRound(m_ai->lastSpeed()));
        m_engine->setText(QStringLiteral("内置引擎（llama.cpp）· %1 · %2").arg(m_ai->engineText(), status));
    }
    // Folder.
    const QString folder = QDir::toNativeSeparators(modelFolder());
    m_folder->setText(folder);
    m_folderNote->setText(onSystemDrive(folder) ? QStringLiteral("现在放在系统盘（C 盘）。C 盘空间紧张时，建议换到其他盘。")
                                                : QStringLiteral("换位置后，以后下载的模型放到新位置；已下载的不会自动搬过去。"));
    m_ollama->blockSignals(true);
    m_ollama->setChecked(!m_ai->builtin());
    m_ollama->blockSignals(false);

    // Model rows.
    while (QLayoutItem* item = m_rows->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // A row's own button may have caused this rebuild: delete it later, but retire it now.
            widget->setObjectName(QString());
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    const QString recommended = recommendedCatalogModel(m_ai->memory(), m_ai->videoMemory()).id;
    const QString downloading = m_ai->downloader()->currentId();
    for (const CatalogModel& model : supportedModels()) {
        auto* row = new QFrame(this);
        row->setObjectName(QStringLiteral("aiSettingsModel"));
        row->setProperty("modelId", model.id);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 10, 12, 10);
        auto* info = new QVBoxLayout;
        auto* titleRow = new QHBoxLayout;
        titleRow->addWidget(text(QStringLiteral("%1 · %2 GB").arg(model.title, QString::number(model.gigabytes, 'f', 1)),
                                 "aiSettingsModelTitle", row));
        if (model.id == recommended) titleRow->addWidget(text(QStringLiteral("适合这台电脑"), "aiSettingsBadge", row));
        titleRow->addStretch(1);
        info->addLayout(titleRow);
        info->addWidget(text(model.note, "aiSettingsNote", row));
        const bool installed = modelInstalled(model);
        const bool current = m_ai->builtin() && m_ai->model() == model.id && installed;
        QString state = installed ? (current ? QStringLiteral("已下载 · 正在使用") : QStringLiteral("已下载"))
                                  : model.id == downloading ? QStringLiteral("正在下载…") : QStringLiteral("未下载");
        info->addWidget(text(state, "aiSettingsState", row));
        rowLayout->addLayout(info, 1);

        const QString id = model.id;
        if (!installed && id == downloading) {
            auto* pause = button(QStringLiteral("暂停"), "aiSettingsPause", row);
            connect(pause, &QPushButton::clicked, this, [this] { m_ai->cancelDownload(); });
            rowLayout->addWidget(pause);
        } else if (!installed) {
            auto* download = button(QStringLiteral("下载"), "aiSettingsDownload", row);
            download->setEnabled(downloading.isEmpty() && m_ai->builtin());
            connect(download, &QPushButton::clicked, this, [this, id] { m_ai->downloadModel(id); });
            rowLayout->addWidget(download);
        } else {
            if (!current) {
                auto* use = button(QStringLiteral("使用"), "aiSettingsUse", row);
                use->setEnabled(m_ai->builtin() && m_ai->state() != LocalAiState::Thinking);
                connect(use, &QPushButton::clicked, this, [this, id] { m_ai->setModel(id); });
                rowLayout->addWidget(use);
            }
            auto* remove = button(QStringLiteral("删除"), "aiSettingsDelete", row);
            connect(remove, &QPushButton::clicked, this, [this, id] { confirmDelete(id); });
            rowLayout->addWidget(remove);
        }
        m_rows->addWidget(row);
    }
    updateProgress();
}

void AiSettingsDialog::updateProgress()
{
    const QString progress = m_ai->downloader()->active() ? m_ai->downloader()->progressText() : m_ai->downloadStatus();
    m_progress->setText(progress);
    m_progress->setVisible(!progress.isEmpty());
}

void AiSettingsDialog::chooseFolder()
{
    const QString chosen = QFileDialog::getExistingDirectory(this, QStringLiteral("选择模型存放位置"), modelFolder());
    if (chosen.isEmpty() || QDir::cleanPath(chosen) == QDir::cleanPath(modelFolder())) return;
    if (m_ai->downloader()->active()) {
        QMessageBox::information(this, QStringLiteral("正在下载"), QStringLiteral("请先暂停正在进行的下载，再更改存放位置。"));
        return;
    }
    setModelFolder(chosen);
    m_ai->refresh();
    rebuild();
}

void AiSettingsDialog::confirmDelete(const QString& id)
{
    const CatalogModel* model = findCatalogModel(id);
    if (!model) return;
    QMessageBox box(QMessageBox::Question, QStringLiteral("删除模型"),
                    QStringLiteral("删除 %1？\n文件 %2 会被删除（约 %3 GB），以后要用需要重新下载。")
                        .arg(model->title, QDir::toNativeSeparators(modelFilePath(*model)),
                             QString::number(model->gigabytes, 'f', 1)),
                    QMessageBox::NoButton, this);
    QPushButton* remove = box.addButton(QStringLiteral("删除"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != remove) return;
    QString error;
    if (!m_ai->deleteModel(id, &error))
        QMessageBox::warning(this, QStringLiteral("没有删除"), error.isEmpty() ? QStringLiteral("删除没有成功。") : error);
    rebuild();
}

} // namespace openoffice
