#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QVBoxLayout;

namespace openoffice {

class LocalAssistant;

// "本地 AI 设置": the built-in engine and where it runs, the folder models are
// kept in, the supported models (download, pause, use, delete, with the one
// that suits this computer marked) and, under 高级, using Ollama instead.
// Models are optional: the office suite works without any of them.
class AiSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit AiSettingsDialog(LocalAssistant* assistant, QWidget* parent = nullptr);

    // Test/automation seams.
    QString engineText() const;
    QString folderText() const;
    QStringList rowTexts() const;

private:
    void rebuild();
    void updateProgress();
    void chooseFolder();
    void confirmDelete(const QString& id);

    LocalAssistant* m_ai;
    QLabel* m_engine = nullptr;
    QLabel* m_folder = nullptr;
    QLabel* m_folderNote = nullptr;
    QLabel* m_progress = nullptr;
    QVBoxLayout* m_rows = nullptr;
    QCheckBox* m_ollama = nullptr;
};

} // namespace openoffice
