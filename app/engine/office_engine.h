#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QObject>
#include <QString>

class QWidget;

namespace openoffice {

enum class DocumentKind {
    Text,
    Spreadsheet,
    Presentation,
    Pdf,
    Unknown
};

enum class EngineOperation {
    Open,
    Save,
    Create,
    Close
};

struct EngineCapabilities {
    bool available = false;
    bool open = false;
    bool edit = false;
    bool save = false;
    bool create = false;
    // Save to a path other than the current one, including the first save of a
    // new document that has no path yet.
    bool saveAs = false;
};

struct EngineResult {
    // For request* return values, ok means accepted for asynchronous execution,
    // not completed. In operationFinished, ok means the operation completed.
    bool ok = false;
    QString code;
    QString message;
};

// Classifies an existing, readable regular file by extension only. This does
// not parse the file, establish format validity, or imply engine support.
DocumentKind classifyDocument(const QString& path);
// Classifies a file name by extension only; the file need not exist (for
// example a save destination chosen by the user).
DocumentKind classifyFileName(const QString& path);

// A session owned by the application, not a launcher for another office app.
// An implementation must advertise only operations it can actually perform.
class OfficeEngine : public QObject {
    Q_OBJECT

public:
    explicit OfficeEngine(QObject* parent = nullptr);
    ~OfficeEngine() override = default;

    virtual EngineCapabilities capabilities() const noexcept = 0;
    virtual bool documentModified() const noexcept { return false; }
    virtual bool needsCloseHandshake() const noexcept { return false; }
    // Kinds requestCreate can actually produce. The default keeps the meaning
    // of capabilities().create for engines that create every kind.
    virtual bool canCreate(DocumentKind kind) const noexcept
    {
        return capabilities().create && kind != DocumentKind::Unknown;
    }

    // The caller supplies a unique request ID before calling. Rejected requests
    // produce no completion event. For each accepted request the backend must
    // post exactly one operationFinished event after the call has returned.
    // Native worker/CEF events must be marshalled to the QObject's thread.
    // A failed operation must preserve the current path and modified state.
    // Open/Create replace the current document only after the new one is
    // confirmed; discardCurrentChanges states that the user chose to drop
    // unsaved changes of the document being replaced. A Create title is the
    // display name of the new document until it is saved, not a file path.
    // requestSave to a different path is Save As; the session then continues
    // with the saved file.
    virtual EngineResult requestOpen(quint64 requestId, const QString& path,
                                     bool discardCurrentChanges) = 0;
    virtual EngineResult requestSave(quint64 requestId, const QString& path) = 0;
    virtual EngineResult requestCreate(quint64 requestId, DocumentKind kind, const QString& title,
                                       bool discardCurrentChanges) = 0;
    // Answers saveDestinationRequested without saving. Engines without
    // editor-initiated save dialogs ignore it.
    virtual void cancelSaveDestination() {}
    // Follows the window's light/dark appearance inside the embedded editor
    // (its interface theme and, when dark, a dimmed document page). Display
    // only: the document file is never changed. Engines without one ignore it.
    virtual void setEditorAppearance(bool dark) { Q_UNUSED(dark); }
    // Editor-level commands of the assistant interface (assistant_commands.h),
    // e.g. text.insert. The caller validates arguments first. The same
    // accepted/completed contract applies, but completion is reported by
    // editorCommandFinished, never by operationFinished. The default engine
    // supports none.
    virtual bool supportsEditorCommand(const QString& name) const noexcept
    {
        Q_UNUSED(name);
        return false;
    }
    virtual EngineResult requestEditorCommand(quint64 requestId, const QString& name,
                                              const QJsonObject& arguments);
    // Native backends may need to finish closing CEF before the window and its
    // editor host are destroyed. The same accepted/completed contract applies;
    // Close completions need not identify a document path or kind.
    virtual EngineResult requestClose(quint64 requestId, bool discardChanges);

    // Returns an actual embedded editor, owned by parent, or nullptr when no
    // editor is available. An unavailable engine must not return a mock editor.
    // Create the host before requesting a document. The backend owns its CEF
    // readiness sequencing; a QWidget alone does not prove that a file opened.
    virtual QWidget* createEditorWidget(QWidget* parent) = 0;

signals:
    // path and kind identify the actual successfully opened/created/saved
    // document. On failure callers must not adopt these as new session state.
    void operationFinished(quint64 requestId,
                           openoffice::EngineOperation operation,
                           openoffice::EngineResult result,
                           QString path,
                           openoffice::DocumentKind kind);
    void modifiedChanged(bool modified);
    void capabilitiesChanged();
    // The embedded editor itself asks where to save (its own Save As command,
    // or saving a new document). The editor waits until the UI answers with
    // requestSave(id, chosenPath) or cancelSaveDestination(). suggestedPath may
    // be a bare file name.
    void saveDestinationRequested(QString suggestedPath, openoffice::DocumentKind kind);
    // value carries the command's result (for example selected text) on success.
    void editorCommandFinished(quint64 requestId, openoffice::EngineResult result, QJsonObject value);
};

} // namespace openoffice

Q_DECLARE_METATYPE(openoffice::DocumentKind)
Q_DECLARE_METATYPE(openoffice::EngineOperation)
Q_DECLARE_METATYPE(openoffice::EngineResult)
