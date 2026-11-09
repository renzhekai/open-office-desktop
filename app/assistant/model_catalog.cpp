#include "model_catalog.h"

#include "ollama_client.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>

namespace openoffice {
namespace {

QSettings settings()
{
    return QSettings(QSettings::UserScope, QStringLiteral("Open Office Project"), QStringLiteral("Open Office"));
}

} // namespace

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

QString defaultModelFolder()
{
    const QString system = QDir::fromNativeSeparators(qEnvironmentVariable("SystemDrive", QStringLiteral("C:"))).toUpper();
    QString best;
    qint64 mostFree = 0;
    for (const QStorageInfo& volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isValid() || !volume.isReady() || volume.isReadOnly()) continue;
        const QString root = QDir::fromNativeSeparators(volume.rootPath());
        if (root.toUpper().startsWith(system)) continue;
        if (volume.device().startsWith("\\\\")) continue; // network shares are too slow for models
        if (volume.bytesAvailable() > mostFree) {
            mostFree = volume.bytesAvailable();
            best = root;
        }
    }
    // Another drive with room for a few models; otherwise the program's data folder.
    if (!best.isEmpty() && mostFree > (12LL << 30))
        return QDir(best).filePath(QStringLiteral("OpenOffice-AI/models"));
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("OpenOffice/ai-models"));
}

QString modelFolder()
{
    const QString configured = qEnvironmentVariable("OPEN_OFFICE_MODEL_DIR");
    if (!configured.isEmpty()) return QDir::cleanPath(configured);
    const QString chosen = settings().value(QStringLiteral("ai/modelFolder")).toString();
    return chosen.isEmpty() ? defaultModelFolder() : QDir::cleanPath(chosen);
}

void setModelFolder(const QString& folder)
{
    settings().setValue(QStringLiteral("ai/modelFolder"), QDir::cleanPath(folder));
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
