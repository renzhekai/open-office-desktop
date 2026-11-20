#pragma once

#include "assistant/cloud_models.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace openoffice {

class EngineClient;
class LocalAssistant;

// "AI 模型设置": the built-in engine and where it runs, where the program (and
// with it the models) is, with 迁移 to move it, the supported local models
// (download, pause, use, delete, with the recommended one marked), the cloud
// models the person added (API key, base URL, model names; add, change, delete)
// and the option to show the person's Ollama models in the model list.
// Models are optional: the office suite works without any of them.
class AiSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit AiSettingsDialog(LocalAssistant* assistant, QWidget* parent = nullptr);

    // Test/automation seams.
    QString engineText() const;
    QString folderText() const;
    QStringList rowTexts() const;
    QStringList cloudRowTexts() const;
    QString styleSummaryText() const;
    void openStyleProfile(); // what 查看和修改… does

signals:
    void relocateRequested(); // 迁移到其他位置…: the main window closes and moves the program

private:
    void rebuild();
    void updateProgress();
    void confirmDelete(const QString& id);
    void editCloud(const QString& id); // empty: add a new one
    void confirmRemoveCloud(const QString& id);

    LocalAssistant* m_ai;
    QLabel* m_engine = nullptr;
    QLabel* m_folder = nullptr;
    QLabel* m_folderNote = nullptr;
    QLabel* m_progress = nullptr;
    QVBoxLayout* m_rows = nullptr;
    QVBoxLayout* m_cloudRows = nullptr;
    QCheckBox* m_ollama = nullptr;
    QLabel* m_styleSummary = nullptr;
};

// Adds or changes one cloud service: name, base URL, API key (kept encrypted;
// left empty when changing, the saved one stays), model names (typed, or fetched
// from the service's model list). Says plainly what is sent where.
class CloudProviderDialog final : public QDialog {
    Q_OBJECT

public:
    CloudProviderDialog(const CloudProvider& existing, QWidget* parent = nullptr);
    CloudProvider provider() const { return m_saved; }

    // Test/automation seams.
    void fill(const QString& name, const QString& baseUrl, const QString& apiKey, const QString& models);
    bool save();          // what 保存 does; false (with the reason shown) when something is missing
    void fetchModels();   // what 获取模型列表 does
    QString statusText() const;
    QString modelsText() const;

private:
    CloudProvider m_existing;
    CloudProvider m_saved;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_baseUrl = nullptr;
    QLineEdit* m_apiKey = nullptr;
    QLineEdit* m_models = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_fetch = nullptr;
    EngineClient* m_client = nullptr;
};

} // namespace openoffice
