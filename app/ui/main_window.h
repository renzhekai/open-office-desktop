#pragma once

#include "assistant/assistant_commands.h"

#include <QHash>
#include <QMainWindow>
#include <QString>

#include <functional>

class QAction;
class QCloseEvent;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

namespace openoffice {

class AssistantBall;
class LocalAssistant;
class OfficeEngine;
enum class DocumentKind;
enum class EngineOperation;
struct EngineResult;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(OfficeEngine* engine, QWidget* parent = nullptr);

    // Selection reads file metadata only. Opening is a separate engine operation.
    void selectDocument(const QString& path);
    QString selectedDocumentPath() const { return m_selectedPath; }
    bool documentOpened() const { return m_documentOpened; }
    bool darkTheme() const { return m_darkTheme; }
    // Test seam: replaces the save file dialog. Returns the chosen path or an
    // empty string for cancel.
    using SaveDestinationChooser = std::function<QString(const QString& suggestedPath, DocumentKind kind)>;
    void setSaveDestinationChooser(SaveDestinationChooser chooser) { m_saveDestinationChooser = std::move(chooser); }
    // Assistant interface: runs one schema-checked command (from the panel, or
    // later a model/MCP client). The outcome is shown in the panel and logged.
    void runAssistantCommand(const AssistantCommand& command);
    // Metadata-only JSON lines (time, source, command, result code); never the
    // inserted or selected text. Empty disables the file log.
    void setAssistantJournalPath(const QString& path) { m_assistantJournalPath = path; }

protected:
    void closeEvent(QCloseEvent* event) override;
    // The assistant ball is a separate floating window: keep it with this one.
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildActions();
    void buildInterface();
    void chooseDocument();
    void openSelectedDocument();
    void createDocument(DocumentKind kind);
    void saveDocument();
    void saveDocumentAs();
    void answerSaveDestination(const QString& suggestedPath, DocumentKind kind);
    QString chooseSaveDestination(const QString& suggestedPath, DocumentKind kind, const QString& caption);
    bool attachEditorView();
    bool prepareReplacement(const std::function<void()>& retry, bool& discardChanges);
    void runReplaceAfterSave();
    void updateAssistantVisibility(); // the ball shows with an open document page
    void updateAssistantWorking();
    void askLocalAi(const QString& text);
    void showLocalAiCard();
    void handleProposedCommand(const AssistantCommand& command);
    QString documentContext() const;
    void startHostCommand(const AssistantCommand& command, EngineOperation expected,
                          const std::function<void()>& action);
    void finishAssistantCommand(const AssistantCommand& command, const CommandOutcome& outcome);
    void handleEditorCommandFinished(quint64 requestId, const EngineResult& result, const QJsonObject& value);
    quint64 beginRequest(EngineOperation operation);
    void handleRequestAccepted(const EngineResult& result);
    void handleOperationFinished(quint64 requestId, EngineOperation operation,
                                 const EngineResult& result, const QString& path, DocumentKind kind);
    void resetCloseAfterSave();
    void awaitSavedState();
    void updateDocumentTitle();
    void refreshCapabilities();
    void applyTheme();
    void showFeedback(const QString& message, bool error = false);
    void showAbout();

    OfficeEngine* m_engine;
    QAction* m_selectFileAction = nullptr;
    QAction* m_openDocumentAction = nullptr;
    QAction* m_saveDocumentAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_editorPageAction = nullptr;
    QAction* m_assistantAction = nullptr;
    AssistantBall* m_assistantBall = nullptr;
    LocalAssistant* m_localAi = nullptr;
    QString m_pendingQuestion;      // asked while the local AI was still being detected
    bool m_cardOnNextState = false; // show the local AI card once detection settles
    bool m_downloadShown = false;
    AssistantCommand m_assistantCommand;       // the command now running
    quint64 m_assistantEditorRequest = 0;      // editor command awaiting completion
    quint64 m_assistantHostRequest = 0;        // host open/save/create awaiting completion
    QString m_assistantJournalPath;
    QAction* m_createTextAction = nullptr;
    QAction* m_createSpreadsheetAction = nullptr;
    QAction* m_createPresentationAction = nullptr;
    QAction* m_themeAction = nullptr;
    QAction* m_homeAction = nullptr;

    QStackedWidget* m_workspace = nullptr;
    QWidget* m_homePage = nullptr;
    QWidget* m_editorPage = nullptr;
    QVBoxLayout* m_editorLayout = nullptr;
    QWidget* m_editorView = nullptr;
    QLabel* m_engineStatusLabel = nullptr;
    QLabel* m_engineDescriptionLabel = nullptr;
    QLabel* m_buildStatusLabel = nullptr;
    QLabel* m_attributionLabel = nullptr;
    QLabel* m_selectedNameLabel = nullptr;
    QLabel* m_selectedPathLabel = nullptr;
    QLabel* m_selectedInfoLabel = nullptr;
    QLabel* m_selectionStateLabel = nullptr;
    QLabel* m_feedbackLabel = nullptr;
    QLabel* m_editorTitleLabel = nullptr;
    QLabel* m_editorFeedbackLabel = nullptr;
    QPushButton* m_openButton = nullptr;
    QTimer* m_closeAfterSaveTimer = nullptr;

    QString m_selectedPath;
    QString m_openedPath;
    QString m_untitledTitle;  // Display name of a created document until its first save.
    QString m_pendingTitle;
    QHash<int, int> m_untitledCounts; // Per DocumentKind, like Word's 文档1/文档2.
    DocumentKind m_openedKind;
    SaveDestinationChooser m_saveDestinationChooser;
    std::function<void()> m_replaceAfterSave; // Open/Create to resume after a confirmed save.
    bool m_documentOpened = false;
    bool m_documentModified = false;
    bool m_closeAfterSave = false;
    bool m_waitingForSavedState = false;
    bool m_editorPausedForClose = false;
    bool m_editorWasEnabledBeforeClose = true;
    bool m_closeHandshakeCompleted = false;
    quint64 m_nextRequestId = 0;
    quint64 m_pendingRequestId = 0;
    EngineOperation m_pendingOperation;
    bool m_darkTheme = false;
};

} // namespace openoffice
