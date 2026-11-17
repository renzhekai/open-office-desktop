#pragma once

#include "assistant/chat_client.h"

#include <QList>
#include <QSettings>
#include <QString>

namespace openoffice {

// The models the built-in engine supports: Qwen's own GGUF releases on
// ModelScope (魔搭), sized for computers from small laptops to gaming PCs.
// Models are optional: nothing is bundled with the installer; the person
// downloads one from the AI settings, into a folder they can choose.
struct CatalogModel {
    QString id;         // "qwen3-4b"
    QString title;      // "Qwen3 4B"
    QString repo;       // ModelScope repository, "Qwen/Qwen3-4B-GGUF"
    QString file;       // "Qwen3-4B-Q4_K_M.gguf"
    double gigabytes;   // download size, for people
    QString note;       // what it is good for, in a few words
};

const QList<CatalogModel>& supportedModels();
const CatalogModel* findCatalogModel(const QString& id);
// The catalog model a computer like this one should use (see recommendDownload).
const CatalogModel& recommendedCatalogModel(qint64 memoryBytes, qint64 videoBytes);
// As a model the assistant can choose: Qwen3 calls tools and can reason.
LocalModel asLocalModel(const CatalogModel& model);

// Where models live: the "models" folder next to the program (fixed; the
// whole program can be moved instead). OPEN_OFFICE_MODEL_DIR overrides it for
// development builds and tests.
QString modelFolder();
QString modelFilePath(const CatalogModel& model);
bool modelInstalled(const CatalogModel& model);
QList<CatalogModel> installedModels();

// The program's settings (the person's account); OPEN_OFFICE_SETTINGS_FILE puts them in
// an INI file instead, for tests.
QSettings aiSettings();

// Which model answers, from the model list: "builtin:qwen3-4b", "ollama:qwen3:8b" or
// "cloud:<provider id>/<model>". Tests may set OPEN_OFFICE_AI_ACTIVE (or the older
// OPEN_OFFICE_AI_BACKEND=builtin|ollama).
QString activeModelKey();
void setActiveModelKey(const QString& key);

// The model the person chose to use; empty: the best installed one.
QString chosenBuiltinModel();
void setChosenBuiltinModel(const QString& id);
// Ollama is optional: used only when chosen in the AI settings
// (OPEN_OFFICE_AI_BACKEND=ollama|builtin overrides, for tests).
bool useOllama();
void setUseOllama(bool use);

} // namespace openoffice
