#include "model_catalog.h"

#include "ollama_client.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace openoffice {
namespace {

QSettings settings()
{
    return aiSettings();
}

} // namespace

QSettings aiSettings()
{
    // Tests keep their settings in a file of their own, never the person's.
    const QString file = qEnvironmentVariable("OPEN_OFFICE_SETTINGS_FILE");
    if (!file.isEmpty()) return QSettings(file, QSettings::IniFormat);
    return QSettings(QSettings::UserScope, QStringLiteral("Open Office Project"), QStringLiteral("Open Office"));
}

QString activeModelKey()
{
    const QString given = qEnvironmentVariable("OPEN_OFFICE_AI_ACTIVE");
    if (!given.isEmpty()) return given;
    const QString backend = qEnvironmentVariable("OPEN_OFFICE_AI_BACKEND");
    if (backend == QStringLiteral("ollama")) return QStringLiteral("ollama:") + settings().value(QStringLiteral("ai/model")).toString();
    if (backend == QStringLiteral("builtin")) return QStringLiteral("builtin:") + chosenBuiltinModel();
    const QString saved = settings().value(QStringLiteral("ai/activeModel")).toString();
    if (!saved.isEmpty()) return saved;
    // Before the model list: "use Ollama" meant answering with Ollama.
    return useOllama() ? QStringLiteral("ollama:") + settings().value(QStringLiteral("ai/model")).toString()
                       : QStringLiteral("builtin:") + chosenBuiltinModel();
}

void setActiveModelKey(const QString& key)
{
    settings().setValue(QStringLiteral("ai/activeModel"), key);
}

const QList<CatalogModel>& supportedModels()
{
    static const QList<CatalogModel> models{
        {QStringLiteral("qwen3-1.7b"), QStringLiteral("Qwen3 1.7B"), QStringLiteral("Qwen/Qwen3-1.7B-GGUF"),
         QStringLiteral("Qwen3-1.7B-Q8_0.gguf"), 1.8, QStringLiteral("最快，适合内存小、没有独立显卡的电脑；写得简单些")},
        {QStringLiteral("qwen3-4b"), QStringLiteral("Qwen3 4B"), QStringLiteral("Qwen/Qwen3-4B-GGUF"),
         QStringLiteral("Qwen3-4B-Q4_K_M.gguf"), 2.5, QStringLiteral("快和好的折中，大多数电脑都合适")},
        {QStringLiteral("qwen3-8b"), QStringLiteral("Qwen3 8B"), QStringLiteral("Qwen/Qwen3-8B-GGUF"),
         QStringLiteral("Qwen3-8B-Q4_K_M.gguf"), 5.0, QStringLiteral("写得最好，需要 8 GB 左右显存的独立显卡")},
    };
    return models;
}

const CatalogModel* findCatalogModel(const QString& id)
{
    for (const CatalogModel& model : supportedModels())
        if (model.id == id) return &model;
    return nullptr;
}

const CatalogModel& recommendedCatalogModel(qint64 memoryBytes, qint64 videoBytes)
{
    // recommendDownload names Ollama's tags ("qwen3:4b"); the catalog uses "qwen3-4b".
    const QString id = recommendDownload(memoryBytes, videoBytes).name.replace(QLatin1Char(':'), QLatin1Char('-'));
    const CatalogModel* model = findCatalogModel(id);
    return model ? *model : supportedModels().at(1);
}

LocalModel asLocalModel(const CatalogModel& model)
{
    LocalModel local;
    local.name = model.id;
    local.bytes = static_cast<qint64>(model.gigabytes * (1LL << 30));
    local.family = QStringLiteral("qwen3");
    local.tools = true;
    local.thinking = true;
    return local;
}

QString modelFolder()
{
    // Development builds share one copy of the models instead of one per runtime folder.
    const QString configured = qEnvironmentVariable("OPEN_OFFICE_MODEL_DIR");
    if (!configured.isEmpty()) return QDir::cleanPath(configured);
    // Models live with the program, so moving the program (设置 → 迁移) moves them too.
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("models"));
}

QString modelFilePath(const CatalogModel& model)
{
    return QDir(modelFolder()).filePath(model.file);
}

bool modelInstalled(const CatalogModel& model)
{
    return QFileInfo(modelFilePath(model)).isFile(); // downloads finish under another name, then are renamed
}

QList<CatalogModel> installedModels()
{
    QList<CatalogModel> result;
    for (const CatalogModel& model : supportedModels())
        if (modelInstalled(model)) result.append(model);
    return result;
}

QString chosenBuiltinModel()
{
    // Tests pick the model without touching the person's settings.
    const QString configured = qEnvironmentVariable("OPEN_OFFICE_AI_MODEL");
    if (!configured.isEmpty()) return configured;
    return settings().value(QStringLiteral("ai/builtinModel")).toString();
}

void setChosenBuiltinModel(const QString& id)
{
    settings().setValue(QStringLiteral("ai/builtinModel"), id);
}

bool useOllama()
{
    const QString backend = qEnvironmentVariable("OPEN_OFFICE_AI_BACKEND");
    if (backend == QStringLiteral("ollama")) return true;
    if (backend == QStringLiteral("builtin")) return false;
    return settings().value(QStringLiteral("ai/useOllama"), false).toBool();
}

void setUseOllama(bool use)
{
    settings().setValue(QStringLiteral("ai/useOllama"), use);
}

} // namespace openoffice
