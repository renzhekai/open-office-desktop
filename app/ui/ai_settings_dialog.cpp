#include "ai_settings_dialog.h"

#include "assistant/engine_client.h"
#include "assistant/local_assistant.h"
#include "assistant/style_profile.h"
#include "ui/style_profile_dialog.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
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
    setWindowTitle(QStringLiteral("AI 模型设置"));
    setMinimumWidth(580);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(10);

    auto* intro = text(QStringLiteral("本机模型在这台电脑上运行，对话和文档内容不会发到网上；云端模型由你填写的服务商运行，"
                                      "用它时提问和相关的文档内容会发给服务商。模型都是可选的：不用模型也能正常使用办公功能。"
                                      "在助手输入框旁的模型列表里可以随时切换。"),
                       "aiSettingsIntro", this);
    layout->addWidget(intro);

    layout->addWidget(text(QStringLiteral("引擎"), "aiSettingsHeading", this));
    m_engine = text(QString(), "aiSettingsEngine", this);
    layout->addWidget(m_engine);

    // Models live in the program folder; to put them elsewhere the whole program moves.
    layout->addWidget(text(QStringLiteral("软件位置"), "aiSettingsHeading", this));
    auto* folderRow = new QHBoxLayout;
    m_folder = text(QString(), "aiSettingsFolder", this);
    m_folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
    folderRow->addWidget(m_folder, 1);
    auto* open = button(QStringLiteral("打开"), "aiSettingsOpenFolder", this);
    connect(open, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QCoreApplication::applicationDirPath()));
    });
    folderRow->addWidget(open);
    auto* move = button(QStringLiteral("迁移到其他位置…"), "aiSettingsRelocate", this);
    connect(move, &QPushButton::clicked, this, &AiSettingsDialog::relocateRequested);
    folderRow->addWidget(move);
    layout->addLayout(folderRow);
    m_folderNote = text(QString(), "aiSettingsFolderNote", this);
    layout->addWidget(m_folderNote);

    layout->addWidget(text(QStringLiteral("本机模型（来自魔搭 ModelScope，Qwen 官方发布）"), "aiSettingsHeading", this));
    m_rows = new QVBoxLayout;
    m_rows->setSpacing(8);
    layout->addLayout(m_rows);
    m_progress = text(QString(), "aiSettingsProgress", this);
    layout->addWidget(m_progress);

    // Cloud services: anything with an OpenAI-compatible API.
    layout->addWidget(text(QStringLiteral("云端模型（API Key）"), "aiSettingsHeading", this));
    layout->addWidget(text(QStringLiteral("填写服务商的接口地址（Base URL）和 API Key，就能在模型列表里用它的模型，例如 DeepSeek、通义千问、"
                                          "Kimi、OpenAI 等兼容 OpenAI 接口的服务。用云端模型时，你的提问、选中的文字和相关的文档内容会发给该服务商；"
                                          "第一次切换到某个服务商时会再问你一次。API Key 用 Windows 数据保护加密，保存在这台电脑上你的账户里。"),
                           "aiSettingsNote", this));
    m_cloudRows = new QVBoxLayout;
    m_cloudRows->setSpacing(8);
    layout->addLayout(m_cloudRows);
    auto* addCloudRow = new QHBoxLayout;
    auto* addCloud = button(QStringLiteral("添加云端模型…"), "aiSettingsAddCloud", this);
    connect(addCloud, &QPushButton::clicked, this, [this] { editCloud(QString()); });
    addCloudRow->addWidget(addCloud);
    addCloudRow->addStretch(1);
    layout->addLayout(addCloudRow);

    layout->addWidget(text(QStringLiteral("Ollama"), "aiSettingsHeading", this));
    m_ollama = new QCheckBox(QStringLiteral("在模型列表里显示本机 Ollama 的模型（要另外安装 Ollama，模型也要在 Ollama 里下载）"), this);
    m_ollama->setObjectName(QStringLiteral("aiSettingsUseOllama"));
    connect(m_ollama, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked == useOllama()) return;
        m_ai->setOllamaEnabled(checked);
    });
    layout->addWidget(m_ollama);

    // What the assistant learned about the looks of drafts, and what the person set.
    layout->addWidget(text(QStringLiteral("排版风格档案"), "aiSettingsHeading", this));
    auto* styleRow = new QHBoxLayout;
    m_styleSummary = text(QString(), "aiSettingsNote", this);
    styleRow->addWidget(m_styleSummary, 1);
    auto* styles = button(QStringLiteral("查看和修改…"), "aiSettingsStyleProfile", this);
    connect(styles, &QPushButton::clicked, this, &AiSettingsDialog::openStyleProfile);
    styleRow->addWidget(styles);
    layout->addLayout(styleRow);

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

QString AiSettingsDialog::styleSummaryText() const { return m_styleSummary->text(); }

void AiSettingsDialog::openStyleProfile()
{
    auto* dialog = findChild<StyleProfileDialog*>();
    if (!dialog) {
        dialog = new StyleProfileDialog(this);
        connect(dialog, &QDialog::finished, this, &AiSettingsDialog::rebuild);
    }
    dialog->show();
    dialog->raise();
}

QStringList AiSettingsDialog::cloudRowTexts() const
{
    QStringList rows;
    for (QFrame* row : findChildren<QFrame*>(QStringLiteral("aiSettingsCloud"))) {
        QStringList parts;
        for (QLabel* label : row->findChildren<QLabel*>()) parts << label->text();
        rows << parts.join(QLatin1Char(' '));
    }
    return rows;
}
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
    const StyleProfile styleProfile = loadStyleProfile();
    m_styleSummary->setText(QStringLiteral("起草时按你选过的排版风格调整推荐。已记录 %1 次选择，%2 个风格设了常用或不用%3。")
                                .arg(styleProfile.entries.size()).arg(styleProfile.manual.size())
                                .arg(styleProfile.paused ? QStringLiteral("，学习已暂停") : QString()));
    // Engine line.
    if (m_ai->cloud()) {
        m_engine->setText(QStringLiteral("正在使用云端模型 %1（%2）。可以在助手输入框旁的模型列表里换回本机模型。")
                              .arg(m_ai->model(), m_ai->cloudServiceName()));
    } else if (m_ai->backend() == AiBackend::Ollama) {
        m_engine->setText(QStringLiteral("正在使用本机 Ollama 的模型 %1。可以在助手输入框旁的模型列表里换回内置引擎的模型。").arg(m_ai->model()));
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
    // Where the program, and so the models, are.
    const QString program = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    m_folder->setText(program);
    qint64 modelBytes = 0;
    for (const CatalogModel& model : installedModels()) modelBytes += QFileInfo(modelFilePath(model)).size();
    const QString models = QDir::toNativeSeparators(modelFolder());
    QString note;
    if (!qEnvironmentVariable("OPEN_OFFICE_MODEL_DIR").isEmpty()) {
        note = QStringLiteral("开发版：模型位置由环境变量 OPEN_OFFICE_MODEL_DIR 指定（%1），迁移只搬软件本身。").arg(models);
    } else {
        note = QStringLiteral("下载的模型放在软件文件夹里的 models%1；迁移时会一起搬走。")
                   .arg(modelBytes > 0 ? QStringLiteral("（现有 %1 GB）").arg(QString::number(modelBytes / double(1LL << 30), 'f', 1))
                                       : QString());
        if (onSystemDrive(program))
            note += QStringLiteral("软件现在在系统盘（C 盘），模型也会占用 C 盘空间（每个 1.8–5 GB）；C 盘紧张时，可以把整个软件迁移到其他盘。");
    }
    m_folderNote->setText(note);
    m_ollama->blockSignals(true);
    m_ollama->setChecked(useOllama());
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
        if (model.id == recommended) titleRow->addWidget(text(QStringLiteral("推荐"), "aiSettingsBadge", row));
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
            download->setEnabled(downloading.isEmpty());
            connect(download, &QPushButton::clicked, this, [this, id] { m_ai->downloadModel(id); });
            rowLayout->addWidget(download);
        } else {
            if (!current) {
                auto* use = button(QStringLiteral("使用"), "aiSettingsUse", row);
                use->setEnabled(m_ai->state() != LocalAiState::Thinking);
                connect(use, &QPushButton::clicked, this, [this, id] { m_ai->chooseModel(QStringLiteral("builtin:") + id); });
                rowLayout->addWidget(use);
            }
            auto* remove = button(QStringLiteral("删除"), "aiSettingsDelete", row);
            connect(remove, &QPushButton::clicked, this, [this, id] { confirmDelete(id); });
            rowLayout->addWidget(remove);
        }
        m_rows->addWidget(row);
    }

    // Cloud rows.
    while (QLayoutItem* item = m_cloudRows->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->setObjectName(QString());
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    const QList<CloudProvider> providers = cloudProviders();
    if (providers.isEmpty())
        m_cloudRows->addWidget(text(QStringLiteral("还没有添加云端模型。"), "aiSettingsNote", this));
    for (const CloudProvider& provider : providers) {
        auto* row = new QFrame(this);
        row->setObjectName(QStringLiteral("aiSettingsCloud"));
        row->setProperty("providerId", provider.id);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 10, 12, 10);
        auto* info = new QVBoxLayout;
        const bool inUse = m_ai->cloud() && m_ai->currentModelKey().startsWith(QStringLiteral("cloud:") + provider.id + QLatin1Char('/'));
        info->addWidget(text(QStringLiteral("%1 · %2").arg(provider.name, provider.models.join(QStringLiteral("、")))
                                 + (inUse ? QStringLiteral(" · 正在使用") : QString()),
                             "aiSettingsModelTitle", row));
        info->addWidget(text(provider.baseUrl + (hasCloudApiKey(provider.id) ? QStringLiteral(" · API Key 已加密保存")
                                                                             : QStringLiteral(" · 没有 API Key")),
                             "aiSettingsNote", row));
        rowLayout->addLayout(info, 1);
        const QString id = provider.id;
        auto* change = button(QStringLiteral("修改"), "aiSettingsEditCloud", row);
        connect(change, &QPushButton::clicked, this, [this, id] { editCloud(id); });
        rowLayout->addWidget(change);
        auto* remove = button(QStringLiteral("删除"), "aiSettingsRemoveCloud", row);
        remove->setEnabled(m_ai->state() != LocalAiState::Thinking);
        connect(remove, &QPushButton::clicked, this, [this, id] { confirmRemoveCloud(id); });
        rowLayout->addWidget(remove);
        m_cloudRows->addWidget(row);
    }
    updateProgress();
}

void AiSettingsDialog::editCloud(const QString& id)
{
    CloudProvider existing;
    if (!id.isEmpty()) {
        const std::optional<CloudProvider> found = findCloudProvider(id);
        if (!found) return;
        existing = *found;
    }
    CloudProviderDialog dialog(existing, this);
    if (dialog.exec() != QDialog::Accepted) return;
    // The model list, and the model in use if its service changed, follow at once.
    m_ai->refresh();
    rebuild();
}

void AiSettingsDialog::confirmRemoveCloud(const QString& id)
{
    const std::optional<CloudProvider> provider = findCloudProvider(id);
    if (!provider) return;
    QMessageBox box(QMessageBox::Question, QStringLiteral("删除云端模型"),
                    QStringLiteral("删除 %1？它的模型会从模型列表里去掉，保存的 API Key 也会删除。").arg(provider->name),
                    QMessageBox::NoButton, this);
    QPushButton* remove = box.addButton(QStringLiteral("删除"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != remove) return;
    const bool inUse = m_ai->currentModelKey().startsWith(QStringLiteral("cloud:") + id + QLatin1Char('/'));
    removeCloudProvider(id);
    if (inUse) m_ai->chooseModel(QStringLiteral("builtin:") + chosenBuiltinModel());
    else m_ai->refresh();
    rebuild();
}

CloudProviderDialog::CloudProviderDialog(const CloudProvider& existing, QWidget* parent)
    : QDialog(parent), m_existing(existing)
{
    setObjectName(QStringLiteral("cloudProviderDialog"));
    setWindowTitle(existing.id.isEmpty() ? QStringLiteral("添加云端模型") : QStringLiteral("修改云端模型"));
    setMinimumWidth(520);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);
    auto* form = new QFormLayout;
    form->setSpacing(8);
    const auto field = [this](const char* name, const QString& value, const QString& hint) {
        auto* edit = new QLineEdit(value, this);
        edit->setObjectName(QString::fromLatin1(name));
        edit->setPlaceholderText(hint);
        return edit;
    };
    m_name = field("cloudName", existing.name, QStringLiteral("例如 DeepSeek"));
    m_baseUrl = field("cloudBaseUrl", existing.baseUrl, QStringLiteral("例如 https://api.deepseek.com/v1"));
    m_apiKey = field("cloudApiKey", QString(), existing.id.isEmpty() ? QStringLiteral("服务商给你的 API Key")
                                                                   : QStringLiteral("已保存；不改就留空"));
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_models = field("cloudModels", existing.models.join(QStringLiteral(", ")), QStringLiteral("例如 deepseek-chat，多个用逗号隔开"));
    form->addRow(QStringLiteral("名称"), m_name);
    form->addRow(QStringLiteral("接口地址"), m_baseUrl);
    form->addRow(QStringLiteral("API Key"), m_apiKey);
    auto* modelRow = new QHBoxLayout;
    modelRow->addWidget(m_models, 1);
    m_fetch = button(QStringLiteral("获取模型列表"), "cloudFetchModels", this);
    connect(m_fetch, &QPushButton::clicked, this, &CloudProviderDialog::fetchModels);
    modelRow->addWidget(m_fetch);
    form->addRow(QStringLiteral("模型"), modelRow);
    layout->addLayout(form);
    layout->addWidget(text(QStringLiteral("接口要兼容 OpenAI（/chat/completions）。用这个服务商的模型时，你的提问、选中的文字和相关的文档内容会通过网络发给它。"
                                          "API Key 用 Windows 数据保护加密后保存在这台电脑上，只用于向这个地址发请求。"),
                           "aiSettingsNote", this));
    m_status = text(QString(), "cloudStatus", this);
    m_status->hide();
    layout->addWidget(m_status);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* cancel = button(QStringLiteral("取消"), "cloudCancel", this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto* ok = button(QStringLiteral("保存"), "cloudSave", this);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, [this] {
        if (save()) accept();
    });
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    layout->addLayout(buttons);
    setStyleSheet(QStringLiteral("QLabel#aiSettingsNote { color: #6E6E73; } QLabel#cloudStatus { color: #C0392B; }"));

    m_client = new EngineClient(this);
    connect(m_client, &EngineClient::modelsFetched, this, [this](const QStringList& models, const QString& error) {
        m_fetch->setEnabled(true);
        if (!models.isEmpty()) {
            // Keep what was typed first (in its order), then what the service lists.
            QStringList merged = splitModelNames(m_models->text());
            for (const QString& name : models)
                if (!merged.contains(name)) merged.append(name);
            m_models->setText(merged.join(QStringLiteral(", ")));
        }
        m_status->setText(error.isEmpty() ? QStringLiteral("已获取 %1 个模型；不需要的可以删掉。").arg(models.size()) : error);
        m_status->setStyleSheet(error.isEmpty() ? QStringLiteral("color: #2E7D32;") : QString());
        m_status->show();
    });
}

void CloudProviderDialog::fill(const QString& name, const QString& baseUrl, const QString& apiKey, const QString& models)
{
    m_name->setText(name);
    m_baseUrl->setText(baseUrl);
    m_apiKey->setText(apiKey);
    m_models->setText(models);
}

QString CloudProviderDialog::statusText() const { return m_status->isHidden() ? QString() : m_status->text(); }
QString CloudProviderDialog::modelsText() const { return m_models->text(); }

void CloudProviderDialog::fetchModels()
{
    const QString address = checkCloudBaseUrl(m_baseUrl->text());
    if (!address.isEmpty()) {
        m_status->setText(address);
        m_status->setStyleSheet(QString());
        m_status->show();
        return;
    }
    const QString key = !m_apiKey->text().isEmpty() ? m_apiKey->text()
        : m_existing.id.isEmpty() ? QString() : cloudApiKey(m_existing.id);
    m_client->setEndpoint(QUrl(m_baseUrl->text().trimmed()), key);
    m_client->setCloud(m_name->text().trimmed().isEmpty() ? QStringLiteral("服务商") : m_name->text().trimmed());
    m_fetch->setEnabled(false);
    m_status->setText(QStringLiteral("正在获取模型列表…"));
    m_status->setStyleSheet(QStringLiteral("color: #6E6E73;"));
    m_status->show();
    m_client->fetchModels();
}

bool CloudProviderDialog::save()
{
    CloudProvider provider{m_existing.id.isEmpty() ? newCloudProviderId(m_name->text()) : m_existing.id,
                           m_name->text().trimmed(), m_baseUrl->text().trimmed(), splitModelNames(m_models->text())};
    QString error;
    if (m_existing.id.isEmpty() && m_apiKey->text().isEmpty()) {
        // A service on this computer may need no key; anything else does.
        const QString host = QUrl(provider.baseUrl).host();
        if (host != QStringLiteral("127.0.0.1") && host != QStringLiteral("localhost")) error = QStringLiteral("请填写 API Key。");
    }
    if (error.isEmpty() && !saveCloudProvider(provider, m_apiKey->text().trimmed(), &error)) {}
    if (!error.isEmpty()) {
        m_status->setText(error);
        m_status->setStyleSheet(QString());
        m_status->show();
        return false;
    }
    m_saved = provider;
    return true;
}

void AiSettingsDialog::updateProgress()
{
    const QString progress = m_ai->downloader()->active() ? m_ai->downloader()->progressText() : m_ai->downloadStatus();
    m_progress->setText(progress);
    m_progress->setVisible(!progress.isEmpty());
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
