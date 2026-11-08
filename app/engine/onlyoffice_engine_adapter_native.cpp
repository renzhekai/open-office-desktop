#include "onlyoffice_engine_adapter.h"

#include <qascapplicationmanager.h>
#include <qcefview.h>
#include <OfficeFileFormats.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QPointer>
#include <QSet>
#include <QStackedLayout>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <vector>
#include <mutex>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Native declarations/flows are from desktop-sdk ae6cf2a039355183c512fc314f7a9bd1a05a6a11.
// Upstream ONLYOFFICE/Ascensio notices and engine resources remain their own.
// This host does not implement document conversion or substitute file copying.
namespace openoffice {
namespace {
using namespace NSEditorApi;

EngineResult refused(const char* code, const QString& message)
{
    return {false, QString::fromLatin1(code), message};
}

EngineResult unavailable()
{
    return refused("engine_unavailable", QStringLiteral("ONLYOFFICE 原生引擎尚未就绪，本次操作未执行。"));
}

QString canonicalFile(const QString& path)
{
    const QFileInfo file(path);
    const QString canonical = file.canonicalFilePath();
    return canonical.isEmpty() ? QString() : QDir::cleanPath(canonical);
}

bool samePath(const QString& first, const QString& second)
{
    return !first.isEmpty() && !second.isEmpty()
        && QDir::cleanPath(first).compare(QDir::cleanPath(second), Qt::CaseInsensitive) == 0;
}

// A save destination may not exist yet: resolve its folder, never create it.
QString saveTarget(const QString& path)
{
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) return {};
    const QFileInfo file(QDir::cleanPath(path));
    if (file.exists()) return file.isFile() ? canonicalFile(file.filePath()) : QString();
    const QString folder = QFileInfo(file.absolutePath()).canonicalFilePath();
    if (folder.isEmpty() || file.fileName().isEmpty()) return {};
    return QDir::cleanPath(QDir(folder).filePath(file.fileName()));
}

// The editable OOXML format the native converter writes for a kind; 0 when
// this host does not save that kind.
int editableFormat(DocumentKind kind)
{
    switch (kind) {
    case DocumentKind::Text: return AVS_OFFICESTUDIO_FILE_DOCUMENT_DOCX;
    case DocumentKind::Spreadsheet: return AVS_OFFICESTUDIO_FILE_SPREADSHEET_XLSX;
    case DocumentKind::Presentation: return AVS_OFFICESTUDIO_FILE_PRESENTATION_PPTX;
    default: return 0;
    }
}

// The title becomes the new document's name in the native recovery folder and
// the editor header, so it must be a plain file name.
bool validTitle(const QString& title, DocumentKind kind)
{
    if (title.isEmpty() || title.size() > 120 || title.trimmed() != title) return false;
    for (const QChar character : title)
        if (character.unicode() < 0x20 || QStringLiteral("\\/:*?\"<>|").contains(character)) return false;
    return classifyFileName(title) == kind;
}

// Pure-ASCII JavaScript string literal: the script crosses std::string and CEF
// string conversions, so every non-ASCII code unit is written as \uXXXX.
QString jsString(const QString& text)
{
    QString literal(QLatin1Char('"'));
    for (const QChar character : text) {
        const ushort unit = character.unicode();
        if (unit == '"' || unit == '\\') {
            literal += QLatin1Char('\\');
            literal += character;
        } else if (unit >= 0x20 && unit < 0x7F) {
            literal += character;
        } else {
            literal += QStringLiteral("\\u%1").arg(unit, 4, 16, QLatin1Char('0'));
        }
    }
    return literal + QLatin1Char('"');
}

const QStringList& editorCommandNames()
{
    static const QStringList names{QStringLiteral("text.insert"), QStringLiteral("selection.getText"),
                                   QStringLiteral("edit.undo"), QStringLiteral("edit.redo")};
    return names;
}

// The editor answers through AscDesktopEditor.execCommand("openoffice:command"),
// which reaches the host as an EXECUTE_COMMAND event of the same view. The body
// uses the editor's own exported API (paste, selected text, undo/redo).
QString editorCommandScript(quint64 id, const QString& name, const QJsonObject& arguments)
{
    QString body;
    if (name == QStringLiteral("text.insert")) {
        body = QStringLiteral("var t=%1;api.asc_PasteData(1,t,undefined,undefined,undefined,"
                              "function(){reply(true,'inserted',{length:t.length});},undefined,"
                              "function(){reply(false,'selection_locked',null);});")
                   .arg(jsString(arguments.value(QStringLiteral("text")).toString()));
    } else if (name == QStringLiteral("selection.getText")) {
        body = QStringLiteral("var s=api.asc_GetSelectedText(false,{Numbering:true});"
                              "s=(s===undefined||s===null)?'':String(s);"
                              "reply(true,'ok',{text:s.slice(0,20000),length:s.length,truncated:s.length>20000});");
    } else if (name == QStringLiteral("edit.undo")) {
        body = QStringLiteral("if(api.asc_getCanUndo&&!api.asc_getCanUndo()){reply(false,'nothing_to_undo',null);return;}"
                              "api.Undo();reply(true,'undone',null);");
    } else if (name == QStringLiteral("edit.redo")) {
        body = QStringLiteral("if(api.asc_getCanRedo&&!api.asc_getCanRedo()){reply(false,'nothing_to_redo',null);return;}"
                              "api.Redo();reply(true,'redone',null);");
    }
    return QStringLiteral("(function(){var id=%1;"
        "function reply(ok,code,value){try{window.AscDesktopEditor.execCommand('openoffice:command',"
        "JSON.stringify({id:id,ok:ok,code:code,value:value}));}catch(e){}}"
        "try{var api=(window.Asc&&window.Asc.editor)||window.editor;"
        "if(!api){reply(false,'editor_not_ready',null);return;}%2"
        "}catch(e){reply(false,'script_error',String((e&&e.message)||e).slice(0,300));}})();")
        .arg(jsString(QString::number(id)), body);
}

// The editor's own theme machinery (web-apps Common.UI.Themes): going dark
// remembers the light theme in use and picks its dark partner (the modern
// "White" theme pairs with "Night", the others with "Dark"), then turns on the
// editor's dark document mode; going light restores the remembered theme.
// Display only; the document file is unaffected.
QString appearanceScript(bool dark)
{
    if (dark)
        return QStringLiteral("(function(){try{var t=window.Common&&Common.UI&&Common.UI.Themes;if(!t)return;"
            "var current=t.currentThemeId();if(!t.isDarkTheme(current))window.__openOfficeLightTheme=current;"
            "var light=window.__openOfficeLightTheme||current;"
            "t.setTheme(/white/.test(light)?'theme-night':'theme-dark','native');"
            "t.setContentTheme('dark',true);}catch(e){}})();");
    return QStringLiteral("(function(){try{var t=window.Common&&Common.UI&&Common.UI.Themes;if(!t)return;"
        "if(t.isDarkTheme(t.currentThemeId()))t.setTheme(window.__openOfficeLightTheme||'theme-light','native');"
        "}catch(e){}})();");
}

QString commandMessage(const QString& code, const QJsonObject& value, const QString& detail)
{
    if (code == QStringLiteral("inserted"))
        return QStringLiteral("已插入 %1 个字符。").arg(value.value(QStringLiteral("length")).toInt());
    if (code == QStringLiteral("ok")) {
        const int length = value.value(QStringLiteral("length")).toInt();
        return length ? QStringLiteral("已读取选中文字（%1 个字符）。").arg(length)
                      : QStringLiteral("当前没有选中文字。");
    }
    if (code == QStringLiteral("undone")) return QStringLiteral("已撤销上一步。");
    if (code == QStringLiteral("redone")) return QStringLiteral("已重做上一步。");
    if (code == QStringLiteral("nothing_to_undo")) return QStringLiteral("没有可撤销的操作。");
    if (code == QStringLiteral("nothing_to_redo")) return QStringLiteral("没有可重做的操作。");
    if (code == QStringLiteral("selection_locked")) return QStringLiteral("当前位置不能插入文字。");
    if (code == QStringLiteral("editor_not_ready")) return QStringLiteral("编辑器尚未就绪。");
    return QStringLiteral("编辑器执行命令时出错：") + detail.left(300);
}

// QCefView does not clear its protected native pointer in _closedSlot().
// Detach on the Qt thread; OnRelease/_closed happens before browser destruction.
class EditorHost final : public QCefView {
public:
    explicit EditorHost(QWidget* parent) : QCefView(parent)
    {
        QObject::connect(this, &QCefView::_closed, this,
                         [this] { detachNativePointer(); }, Qt::QueuedConnection);
        QObject::connect(this, &QCefView::_loaded, this,
                         [this] { UpdateSize(); }, Qt::QueuedConnection);
    }
    void detachNativePointer()
    {
        m_pCefView = nullptr;
        setAcceptDrops(false);
        setEnabled(false);
    }
    void hideNativeBrowser()
    {
#ifdef Q_OS_WIN
        // Upstream DoClose() reparents the browser window to the desktop before
        // closing it. Hide it first so a retired document cannot flash on top.
        if (!cef_handle) return;
        if (const HWND child = ::GetWindow(reinterpret_cast<HWND>(cef_handle), GW_CHILD))
            ::ShowWindow(child, SW_HIDE);
#endif
    }
    void UpdateSize() override
    {
#ifdef Q_OS_WIN
        if (!m_pCefView || !cef_handle) return;
        const HWND parent = reinterpret_cast<HWND>(cef_handle);
        RECT client = {};
        if (!::GetClientRect(parent, &client)) return;
        // Qt6 widget sizes are logical pixels. The upstream Windows wrapper
        // passes them straight to SetWindowPos, making the CEF child 2/3 size
        // at 150% scaling. Use the native parent's client pixels directly;
        // do not multiply an already scaled native size by Qt's DPR again.
        cef_width = static_cast<unsigned int>(client.right - client.left);
        cef_height = static_cast<unsigned int>(client.bottom - client.top);
        if (const HWND child = ::GetWindow(parent, GW_CHILD))
            ::SetWindowPos(child, nullptr, 0, 0, static_cast<int>(cef_width),
                           static_cast<int>(cef_height), SWP_NOZORDER | SWP_NOACTIVATE);
#else
        QCefView::UpdateSize();
#endif
    }
protected:
    bool event(QEvent* event) override
    {
        const bool handled = QCefView::event(event);
#if defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        if (event->type() == QEvent::DevicePixelRatioChange) UpdateSize();
#endif
        return handled;
    }
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (!m_pCefView) { event->ignore(); return; }
        QCefView::dragEnterEvent(event);
    }
    void dropEvent(QDropEvent* event) override
    {
        if (!m_pCefView) { event->ignore(); return; }
        QCefView::dropEvent(event);
    }
};

class ApplicationManager final : public QAscApplicationManager {
public:
    bool externalLoopInitialized = false;
    bool IsExternalEventLoop() override
    {
        // Upstream's method creates/binds the pump; it is NOT a read-only
        // getter. Record the one call made by Init_CEF instead of calling it
        // again after initialization and accidentally replacing the pump.
        externalLoopInitialized = QAscApplicationManager::IsExternalEventLoop();
        return externalLoopInitialized;
    }
};

struct EventSnapshot {
    int type = -1;
    int viewId = -1;
    bool flag = false;
    QString command;
    QString detail;
    QString path;
    int messageType = 0;
    std::vector<std::wstring> arguments;
    std::vector<int> formats;
};

// Used only if teardown is requested while a browser is still alive. It has
// process lifetime and cannot call back into a destroyed adapter.
CAscCefMenuEventListener& droppedEventSink()
{
    static CAscCefMenuEventListener sink;
    return sink;
}
} // namespace

struct OnlyOfficeEngineAdapter::Native final : CAscCefMenuEventListener {
    explicit Native(OnlyOfficeEngineAdapter* adapter) : owner(adapter)
    {
        commandTimer = new QTimer(owner);
        commandTimer->setSingleShot(true);
        QObject::connect(commandTimer, &QTimer::timeout, owner, [this] {
            // Not a session fault: commands are tagged, a late reply is ignored.
            finishCommand(refused("editor_command_timeout",
                QStringLiteral("编辑器没有在 10 秒内回报结果，无法确认命令是否已执行。")));
        });
        timer = new QTimer(owner);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, owner, [this] {
            log("operation_timeout");
            if (pending.active && incomingViewId >= 0) {
                // The replacement view is closed by its own ID; late events
                // from it are ignored and the current document stays usable.
                abandonIncoming(refused("native_timeout", QStringLiteral("文档加载超时；已保留当前文档。")));
                return;
            }
            faulted = true; // Late untagged native callbacks cannot service a new request.
            finish(refused("native_timeout", QStringLiteral("引擎操作超时；会话已停止接受新的打开和保存请求。")));
            emit owner->capabilitiesChanged();
        });
    }

    OnlyOfficeEngineAdapter* owner;
    std::mutex callbackMutex;
    bool acceptsCallbacks = true;
    std::unique_ptr<ApplicationManager> manager;
    std::unique_ptr<CApplicationCEF> application;
    // The UI receives one container. Each document has its own CEF view: a
    // replacement loads beside the current view, and the old view is destroyed
    // only after the new document is confirmed. A replacement therefore never
    // closes the last browser, whose closure ends the native message loop.
    QPointer<QWidget> container;
    QStackedLayout* stack = nullptr; // Owned by container.
    QPointer<EditorHost> host;       // Current document view.
    int viewId = -1;
    QPointer<EditorHost> incomingHost; // Replacement view while Open/Create loads.
    int incomingViewId = -1;
    bool incomingModified = false;
    QHash<int, QPointer<EditorHost>> retiring; // Destroyed views awaiting DESTROYWINDOW.
    QTimer* timer = nullptr;
    QFile eventLog;
    std::vector<QByteArray> argumentStorage;
    std::vector<char*> arguments;
    QSet<quint64> acceptedIds;
    bool initialized = false;
    bool subprocess = false;
    int subprocessExitCode = 0;
    bool loopRunning = false;
    bool loopReturned = false;
    bool faulted = false;
    bool ready = false;
    bool modified = false;
    bool loadRequested = false; // The current view was asked to load a document.
    bool destroyRequested = false;
    bool closed = false;
    QString currentPath; // Empty for a new document that was never saved.
    DocumentKind currentKind = DocumentKind::Unknown;
    QString lastNativeError;
    int dialogViewId = -1; // Editor-initiated save dialog waiting for the UI.
    bool darkAppearance = false;  // the window's appearance, applied to each loaded view
    bool appearanceChanged = false; // light views need a script only after a dark phase
    struct CommandPending {
        bool active = false;
        quint64 id = 0;
        QString name;
    } command; // One editor command at a time, separate from document operations.
    QTimer* commandTimer = nullptr;
    struct Pending {
        bool active = false;
        quint64 id = 0;
        EngineOperation operation = EngineOperation::Open;
        QString path;
        QString confirmedSavePath;
        bool discard = false;
        DocumentKind kind = DocumentKind::Unknown;
    } pending;

    bool onOwnerThread() const { return QThread::currentThread() == owner->thread(); }
    void stopCallbacks()
    {
        // Synchronize with an already-entered callback before QObject teardown.
        std::lock_guard<std::mutex> lock(callbackMutex);
        acceptsCallbacks = false;
    }
    static CCefViewEditor* editorOf(EditorHost* view)
    {
        return view && view->GetCefView()
            ? static_cast<CCefViewEditor*>(view->GetCefView()) : nullptr;
    }
    CCefViewEditor* editor() const { return editorOf(host); }
    bool currentModified() const { return modified || (editor() && editor()->GetModified()); }

    void log(const char* action, int eventType = -1, int eventView = -1)
    {
        if (!eventLog.isOpen()) return;
        // Event metadata only: no document text, renderer parameters or file paths.
        const QByteArray line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8()
            + " action=" + action + " event=" + QByteArray::number(eventType)
            + " view=" + QByteArray::number(eventView)
            + " request=" + QByteArray::number(pending.active ? pending.id : 0) + '\n';
        eventLog.write(line);
        eventLog.flush();
    }

    void logCommand(const char* action, quint64 id)
    {
        if (!eventLog.isOpen()) return;
        // Command name and id only: never the inserted or selected text.
        eventLog.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8()
            + " action=" + action + " command=" + command.name.toLatin1()
            + " command_request=" + QByteArray::number(id) + '\n');
        eventLog.flush();
    }

    void finishCommand(EngineResult result, QJsonObject value = {})
    {
        if (!command.active) return;
        commandTimer->stop();
        const CommandPending completed = command;
        logCommand(result.ok ? "editor_command_completed" : "editor_command_failed", completed.id);
        command = {};
        emit owner->editorCommandFinished(completed.id, std::move(result), std::move(value));
    }

    void handleCommandReply(const QString& detail)
    {
        const QJsonObject reply = QJsonDocument::fromJson(detail.toUtf8()).object();
        if (!command.active || reply.value(QStringLiteral("id")).toString() != QString::number(command.id)) {
            log("stale_editor_command_reply");
            return;
        }
        const QString code = reply.value(QStringLiteral("code")).toString();
        const QJsonObject value = reply.value(QStringLiteral("value")).toObject();
        const QString message = commandMessage(code, value, reply.value(QStringLiteral("value")).toString());
        if (reply.value(QStringLiteral("ok")).toBool()) finishCommand({true, code, message}, value);
        else finishCommand({false, code.isEmpty() ? QStringLiteral("editor_command_failed") : code, message});
    }

    EngineResult admissible(quint64 id) const
    {
        if (!onOwnerThread()) return refused("wrong_thread", QStringLiteral("必须在界面线程请求引擎操作。"));
        if (!id || acceptedIds.contains(id)) return refused("invalid_request_id", QStringLiteral("请求编号无效或已使用。"));
        if (pending.active) return refused("engine_busy", QStringLiteral("上一个引擎操作尚未结束。"));
        return {true, {}, {}};
    }

    EngineResult begin(quint64 id, EngineOperation operation, const QString& path = {}, bool discard = false,
                       DocumentKind kind = DocumentKind::Unknown)
    {
        const EngineResult allowed = admissible(id);
        if (!allowed.ok) return allowed;
        acceptedIds.insert(id);
        pending = {true, id, operation, path, {}, discard, kind};
        timer->start(operation == EngineOperation::Close ? 30000 : 180000);
        log("request_accepted");
        return {true, QStringLiteral("accepted"), QStringLiteral("请求已交给原生引擎，等待完成事件。")};
    }

    void finish(EngineResult result, const QString& actualPath = {})
    {
        if (!pending.active) return;
        timer->stop();
        const Pending completed = pending;
        log(result.ok ? "operation_completed" : "operation_failed");
        pending = {};
        emit owner->operationFinished(completed.id, completed.operation, std::move(result), actualPath,
            completed.operation == EngineOperation::Close ? DocumentKind::Unknown : completed.kind);
    }

    void replySavePath(int id, const QString& path, int fileType)
    {
        auto* response = new CAscMenuEvent(ASC_MENU_EVENT_TYPE_CEF_LOCALFILE_SAVE_PATH);
        auto* data = new CAscLocalSaveFileDialog;
        data->put_Id(id);
        data->put_Path(path.toStdWString());
        data->put_FileType(fileType ? fileType : AVS_OFFICESTUDIO_FILE_DOCUMENT_DOCX);
        response->m_pData = data;
        manager->Apply(response); // Ownership is transferred; manager/view release it.
    }

    void cancelDialog()
    {
        if (dialogViewId < 0) return;
        const int view = dialogViewId;
        dialogViewId = -1;
        replySavePath(view, QString(), editableFormat(currentKind)); // Empty path = cancelled.
        log("save_destination_cancelled", -1, view);
    }

    void answerSaveQuestion(int id)
    {
        // Renderer actions such as some encryption operations ask the host for
        // permission to save. This host does not support those actions:
        // return an explicit negative response.
        if (CCefView* view = manager->GetViewById(id)) {
            auto* response = new CAscMenuEvent(ASC_MENU_EVENT_TYPE_DOCUMENTEDITORS_SAVE_YES_NO);
            auto* data = new CAscEditorSaveQuestion;
            data->put_Id(id);
            data->put_Value(false);
            response->m_pData = data;
            view->Apply(response);
        }
    }

    // The same route the upstream desktop app uses for its menu commands:
    // web-apps' Desktop controller maps file:saveas to the editor's Save As.
    static void sendEditorCommand(CCefViewEditor* view, const std::wstring& command)
    {
        auto* data = new CAscExecCommandJS;
        data->put_Command(command);
        data->put_FrameName(L"frameEditor");
        auto* event = new CAscMenuEvent(ASC_MENU_EVENT_TYPE_CEF_EXECUTE_COMMAND_JS);
        event->m_pData = data;
        view->Apply(event);
    }

    EditorHost* createHost()
    {
        auto* view = new EditorHost(container);
        stack->addWidget(view);
        view->Create(manager.get(), cvwtEditor);
        if (!view->GetCefView()) {
            delete view;
            return nullptr;
        }
        log("host_created", -1, view->GetCefView()->GetId());
        return view;
    }

    void show(EditorHost* view)
    {
        if (!view || !stack || !container) return;
        stack->setCurrentWidget(view);
        view->setGeometry(container->contentsRect());
        view->UpdateSize(); // Supply native client pixels before browser creation.
    }

    void retire(EditorHost* view, int id)
    {
        if (id < 0) return;
        retiring.insert(id, view);
        if (view) view->hideNativeBrowser();
        log("view_retired", -1, id);
        manager->DestroyCefView(id, true);
        // DESTROY_SAFE has released the widget link; never touch the view again.
        if (view) view->detachNativePointer();
    }

    void abandonIncoming(EngineResult result)
    {
        EditorHost* view = incomingHost;
        const int id = incomingViewId;
        incomingHost = nullptr;
        incomingViewId = -1;
        incomingModified = false;
        retire(view, id);
        show(host);
        finish(std::move(result));
        emit owner->capabilitiesChanged();
    }

    void fault(EngineResult result)
    {
        faulted = true;
        ready = false;
        if (incomingViewId >= 0) {
            EditorHost* view = incomingHost;
            const int id = incomingViewId;
            incomingHost = nullptr;
            incomingViewId = -1;
            retire(view, id);
            show(host);
        }
        if (pending.active) finish(std::move(result));
        finishCommand(refused("native_session_faulted", QStringLiteral("编辑器会话已停止，无法确认命令是否已执行。")));
        emit owner->capabilitiesChanged();
    }

    void applyAppearance()
    {
        if (!ready || !editor() || (!darkAppearance && !appearanceChanged)) return;
        editor()->ExecuteEditorScript(appearanceScript(darkAppearance).toStdString());
        log(darkAppearance ? "editor_appearance_dark" : "editor_appearance_light", -1, viewId);
    }

    void completeLoad(bool replacing)
    {
        CCefViewEditor* loaded = editorOf(replacing ? incomingHost : host);
        QString actual;
        if (pending.operation == EngineOperation::Open) {
            actual = loaded ? canonicalFile(QString::fromStdWString(loaded->GetLocalFilePath())) : QString();
            if (!samePath(actual, pending.path)) {
                const EngineResult failed = refused("open_path_unconfirmed", QStringLiteral("引擎就绪事件未确认请求的 DOCX 路径。"));
                if (replacing) abandonIncoming(failed);
                else fault(failed);
                return;
            }
        }
        const bool wasModified = modified;
        if (replacing) {
            EditorHost* previous = host;
            const int previousId = viewId;
            host = incomingHost;
            viewId = incomingViewId;
            incomingHost = nullptr;
            incomingViewId = -1;
            modified = incomingModified;
            incomingModified = false;
            show(host);
            retire(previous, previousId); // Only now: the new document is confirmed.
            loadRequested = true;
        }
        currentPath = actual;
        currentKind = pending.kind;
        ready = true;
        applyAppearance(); // a fresh view starts in the editor's default theme
        if (wasModified != modified) emit owner->modifiedChanged(modified);
        if (pending.operation == EngineOperation::Open)
            finish({true, QStringLiteral("opened"), QStringLiteral("文档已由 ONLYOFFICE 打开。")}, currentPath);
        else
            finish({true, QStringLiteral("created"), QStringLiteral("新文档已由 ONLYOFFICE 创建。")});
        emit owner->capabilitiesChanged();
    }

    void OnEvent(CAscCefMenuEvent* event) override
    {
        if (!event) return;
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (!acceptsCallbacks) { event->Release(); return; }
        EventSnapshot snapshot;
        snapshot.type = event->m_nType;
        snapshot.viewId = event->get_SenderId();
        if (event->m_pData) {
            switch (snapshot.type) {
            case ASC_MENU_EVENT_TYPE_CEF_EXECUTE_COMMAND: {
                auto* data = static_cast<CAscExecCommand*>(event->m_pData);
                snapshot.command = QString::fromStdWString(data->get_Command());
                if (snapshot.command == QStringLiteral("error:page")
                    || snapshot.command == QStringLiteral("openoffice:command"))
                    snapshot.detail = QString::fromStdWString(data->get_Param());
                break;
            }
            case ASC_MENU_EVENT_TYPE_CEF_DOCUMENT_NAME: {
                auto* data = static_cast<CAscDocumentName*>(event->m_pData);
                snapshot.viewId = data->get_Id();
                snapshot.path = QString::fromStdWString(data->get_Path());
                break;
            }
            case ASC_MENU_EVENT_TYPE_CEF_MODIFY_CHANGED: {
                auto* data = static_cast<CAscDocumentModifyChanged*>(event->m_pData);
                snapshot.viewId = data->get_Id(); snapshot.flag = data->get_Changed();
                break;
            }
            case ASC_MENU_EVENT_TYPE_CEF_ONSAVE: {
                auto* data = static_cast<CAscDocumentOnSaveData*>(event->m_pData);
                snapshot.viewId = data->get_Id(); snapshot.flag = data->get_IsCancel();
                break;
            }
            case ASC_MENU_EVENT_TYPE_CEF_LOCALFILE_SAVE: {
                auto* data = static_cast<CAscLocalSaveFileDialog*>(event->m_pData);
                snapshot.viewId = data->get_Id();
                snapshot.path = QString::fromStdWString(data->get_Path());
                snapshot.formats = data->get_SupportFormats();
                break;
            }
            case ASC_MENU_EVENT_TYPE_DOCUMENTEDITORS_SAVE_YES_NO:
                snapshot.viewId = static_cast<CAscEditorSaveQuestion*>(event->m_pData)->get_Id();
                break;
            case ASC_MENU_EVENT_TYPE_UI_THREAD_MESSAGE: {
                auto* data = static_cast<CAscUIThreadMessage*>(event->m_pData);
                snapshot.messageType = data->get_Type();
                snapshot.arguments = data->GetArgs();
                break;
            }
            case ASC_MENU_EVENT_TYPE_CEF_ONCLOSE:
            case ASC_MENU_EVENT_TYPE_CEF_ONBEFORECLOSE:
                snapshot.viewId = static_cast<CAscTypeId*>(event->m_pData)->get_Id();
                break;
            default: break;
            }
        }
        event->Release(); // Exactly one release; queued work owns only value copies.
        QMetaObject::invokeMethod(owner, [this, snapshot] { handle(snapshot); }, Qt::QueuedConnection);
    }

    void handleRetiring(const EventSnapshot& event)
    {
        switch (event.type) {
        case ASC_MENU_EVENT_TYPE_CEF_DESTROYWINDOW:
            if (QPointer<EditorHost> view = retiring.take(event.viewId)) view->deleteLater();
            log("retired_view_destroyed", event.type, event.viewId);
            break;
        case ASC_MENU_EVENT_TYPE_CEF_LOCALFILE_SAVE:
            replySavePath(event.viewId, QString(), 0); // Never leave a closing view waiting.
            break;
        case ASC_MENU_EVENT_TYPE_DOCUMENTEDITORS_SAVE_YES_NO:
            answerSaveQuestion(event.viewId);
            break;
        default: break;
        }
    }

    void handleIncoming(const EventSnapshot& event)
    {
        const bool loading = pending.active
            && (pending.operation == EngineOperation::Open || pending.operation == EngineOperation::Create);
        switch (event.type) {
        case ASC_MENU_EVENT_TYPE_CEF_MODIFY_CHANGED:
            incomingModified = event.flag;
            break;
        case ASC_MENU_EVENT_TYPE_CEF_LOCALFILE_SAVE:
            replySavePath(event.viewId, QString(), 0); // A loading document cannot be saved yet.
            break;
        case ASC_MENU_EVENT_TYPE_DOCUMENTEDITORS_SAVE_YES_NO:
            answerSaveQuestion(event.viewId);
            break;
        case ASC_MENU_EVENT_TYPE_CEF_EXECUTE_COMMAND:
            if (event.command == QStringLiteral("error:page")) {
                lastNativeError = event.detail;
                abandonIncoming(refused("native_page_error", QStringLiteral("编辑器加载失败：")
                    + event.detail.left(2048) + QStringLiteral("；已保留当前文档。")));
            } else if (event.command.contains(QStringLiteral("doc:onready")) && loading) {
                completeLoad(true);
            }
            break;
        case ASC_MENU_EVENT_TYPE_CEF_DESTROYWINDOW:
            if (incomingHost) {
                incomingHost->detachNativePointer();
                incomingHost->deleteLater();
            }
            incomingHost = nullptr;
            incomingViewId = -1;
            incomingModified = false;
            show(host);
            finish(refused("native_view_closed", QStringLiteral("新文档视图在加载完成前关闭；已保留当前文档。")));
            emit owner->capabilitiesChanged();
            break;
        case ASC_MENU_EVENT_TYPE_PAGE_CRASH:
            abandonIncoming(refused("native_renderer_crashed", QStringLiteral("新文档的编辑器进程已停止；已保留当前文档。")));
            break;
        default: break;
        }
    }

    void handleSaveDialog(const EventSnapshot& event)
    {
        const int format = editableFormat(currentKind);
        const bool editable = format && (event.formats.empty()
            || std::find(event.formats.begin(), event.formats.end(), format) != event.formats.end());
        const bool current = event.viewId == viewId && viewId >= 0 && !faulted;
        if (current && pending.active && pending.operation == EngineOperation::Save) {
            // Host-requested first save or Save As: hand over the destination
            // the user chose; the native side converts and reports back.
            replySavePath(event.viewId, editable ? pending.path : QString(), format);
            if (!editable)
                finish(refused("save_format_unsupported", QStringLiteral("编辑器没有提供与文档相同的保存格式，已取消保存。")));
            return;
        }
        if (current && !pending.active && ready && editable && dialogViewId < 0) {
            // The editor's own Save As command, or saving a new document from
            // inside the editor. The UI answers through requestSave/cancel.
            dialogViewId = event.viewId;
            log("save_destination_requested", event.type, event.viewId);
            emit owner->saveDestinationRequested(event.path, currentKind);
            return;
        }
        // Exports to other formats and requests during other work are not
        // supported yet: never leave the native dialog waiting.
        replySavePath(event.viewId, QString(), format);
    }

    void handle(const EventSnapshot& event)
    {
        if (!manager) return;
        if (event.type == ASC_MENU_EVENT_TYPE_CEF_CHECK_KEYBOARD) {
            // Keep the native polling work, but do not write/flush a log line
            // every 100 ms. Document, error and lifecycle events remain logged.
            manager->CheckKeyboard();
            return;
        }
        log("native_event", event.type, event.viewId);
        if (event.type == ASC_MENU_EVENT_TYPE_UI_THREAD_MESSAGE) {
            // Preserve the upstream host's UI-thread routing without carrying
            // a borrowed event/payload pointer across the thread boundary.
            auto* routed = new CAscCefMenuEvent(ASC_MENU_EVENT_TYPE_UI_THREAD_MESSAGE);
            routed->put_SenderId(event.viewId);
            auto* data = new CAscUIThreadMessage;
            data->put_Type(event.messageType);
            data->GetArgs() = event.arguments;
            routed->m_pData = data;
            manager->Apply(routed);
            return;
        }
        if (retiring.contains(event.viewId)) { handleRetiring(event); return; }
        if (incomingViewId >= 0 && event.viewId == incomingViewId) { handleIncoming(event); return; }
        if (event.type == ASC_MENU_EVENT_TYPE_CEF_LOCALFILE_SAVE) { handleSaveDialog(event); return; }
        if (event.type == ASC_MENU_EVENT_TYPE_DOCUMENTEDITORS_SAVE_YES_NO) {
            answerSaveQuestion(event.viewId);
            if (event.viewId == viewId && pending.active && pending.operation == EngineOperation::Save)
                finish(refused("save_confirmation_unsupported", QStringLiteral("此操作需要额外的保存确认，当前原生阶段已取消该操作。")));
            return;
        }
        if (event.viewId != viewId || viewId < 0) return;
        switch (event.type) {
        case ASC_MENU_EVENT_TYPE_CEF_MODIFY_CHANGED:
            if (modified != event.flag) {
                modified = event.flag;
                emit owner->modifiedChanged(modified);
            }
            break;
        case ASC_MENU_EVENT_TYPE_CEF_EXECUTE_COMMAND:
            if (event.command == QStringLiteral("openoffice:command")) {
                handleCommandReply(event.detail);
            } else if (event.command == QStringLiteral("error:page")) {
                lastNativeError = event.detail;
                fault(refused("native_page_error", QStringLiteral("编辑器加载失败：") + event.detail.left(2048)));
            } else if (event.command.contains(QStringLiteral("doc:onready")) && pending.active && !faulted
                && (pending.operation == EngineOperation::Open || pending.operation == EngineOperation::Create)) {
                completeLoad(false);
            }
            break;
        case ASC_MENU_EVENT_TYPE_CEF_DOCUMENT_NAME:
            if (pending.active && pending.operation == EngineOperation::Save && !faulted)
                pending.confirmedSavePath = canonicalFile(event.path);
            break;
        case ASC_MENU_EVENT_TYPE_CEF_ONSAVE:
            if (pending.active && pending.operation == EngineOperation::Save && !faulted) {
                const QString actual = pending.confirmedSavePath;
                if (event.flag) {
                    finish(refused("native_save_cancelled_or_failed", QStringLiteral("引擎未确认保存；可能已取消或转换失败。")));
                } else if (!samePath(actual, canonicalFile(pending.path)) || !QFileInfo(actual).isFile()) {
                    fault(refused("save_path_unconfirmed", QStringLiteral("引擎未确认保存到所选路径，会话已停止新的保存请求。")));
                } else {
                    currentPath = actual;
                    finish({true, QStringLiteral("saved"), QStringLiteral("引擎已确认保存。")}, actual);
                    emit owner->capabilitiesChanged();
                }
            }
            break;
        case ASC_MENU_EVENT_TYPE_CEF_ONCLOSE:
        case ASC_MENU_EVENT_TYPE_CEF_ONBEFORECLOSE:
            if (pending.active && pending.operation == EngineOperation::Close && !destroyRequested) {
                if (event.type == ASC_MENU_EVENT_TYPE_CEF_ONBEFORECLOSE && !pending.discard) {
                    finish(refused("unsaved_changes", QStringLiteral("引擎仍有未保存修改，已保留当前文档。")));
                } else {
                    destroyRequested = true;
                    manager->DestroyCefView(viewId, true);
                }
            }
            break;
        case ASC_MENU_EVENT_TYPE_CEF_DESTROYWINDOW:
            if (host) host->detachNativePointer();
            closed = true;
            ready = false;
            viewId = -1;
            dialogViewId = -1;
            finishCommand(refused("native_view_closed", QStringLiteral("编辑器已关闭，无法确认命令是否已执行。")));
            if (pending.active && pending.operation == EngineOperation::Close)
                finish({true, QStringLiteral("closed"), QStringLiteral("原生编辑器已关闭。")});
            else if (pending.active)
                finish(refused("native_view_closed", QStringLiteral("编辑器在操作完成前关闭。")));
            emit owner->capabilitiesChanged();
            break;
        case ASC_MENU_EVENT_TYPE_PAGE_CRASH:
            fault(refused("native_renderer_crashed", QStringLiteral("编辑器进程已停止；未确认操作成功。")));
            break;
        default: break;
        }
    }
};

OnlyOfficeEngineAdapter::OnlyOfficeEngineAdapter(QObject* parent) : OfficeEngine(parent) {}

OnlyOfficeEngineAdapter::~OnlyOfficeEngineAdapter()
{
    if (native_) native_->stopCallbacks();
    shutdownNative();
    if (native_ && native_->viewId >= 0 && !native_->closed) {
        // A caller violated the close handshake. Avoid destroying a manager or
        // host that CEF can still reference; retain native state until process
        // exit, detach the QObject and widget, and explicitly log the failure.
        native_->log("unsafe_teardown_retained_until_process_exit");
        // Keep this listener itself alive: a native callback may already hold
        // its raw address. stopCallbacks() prevents any further QObject use.
        if (native_->editor()) native_->editor()->OnDestroyWidgetImpl();
        if (native_->host) native_->host->detachNativePointer();
        native_.release();
    }
}

EngineResult OnlyOfficeEngineAdapter::initializeNative(const QString& runtimeRoot,
    const QString& profileRoot, int argc, char** argv)
{
    if (QThread::currentThread() != thread() || !qobject_cast<QApplication*>(QCoreApplication::instance()))
        return refused("wrong_thread", QStringLiteral("原生编辑器必须在 QApplication 的界面线程初始化。"));
    if (native_) return refused("already_initialized", QStringLiteral("原生初始化已尝试；请重新启动程序。"));
    const QString runtime = QFileInfo(runtimeRoot).canonicalFilePath();
    if (runtime.isEmpty() || !samePath(runtime, QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath()))
        return refused("runtime_location_mismatch", QStringLiteral("运行依赖必须与本程序入口位于同一目录。"));
    const QDir directory(runtime);
    // converter/empty/<lang>/new.docx is the native blank-document seed used by
    // CreateLocalFile; the optional converter/templates gallery is not needed.
    for (const QString& relative : {QStringLiteral("editors_helper.exe"), QStringLiteral("libcef.dll"),
             QStringLiteral("ascdocumentscore.dll"), QStringLiteral("converter/x2t.exe"),
             QStringLiteral("converter/empty/zh-CN/new.docx"),
             QStringLiteral("editors/web-apps/apps/api/documents/index.html")}) {
        const QFileInfo file(directory.filePath(relative));
        if (!file.isFile() || !file.isReadable())
            return refused("runtime_incomplete", QStringLiteral("缺少必要的运行依赖：") + relative);
    }
    for (const QString& relative : {QStringLiteral("dictionaries")})
        if (!QFileInfo(directory.filePath(relative)).isDir())
            return refused("runtime_incomplete", QStringLiteral("缺少必要的运行目录：") + relative);
    if (profileRoot.isEmpty() || !QDir::isAbsolutePath(profileRoot) || samePath(QDir::cleanPath(profileRoot), runtime))
        return refused("invalid_profile", QStringLiteral("请提供独立的绝对路径作为引擎用户目录。"));
    if (!QDir().mkpath(profileRoot)) return refused("profile_unwritable", QStringLiteral("无法创建引擎用户目录。"));

    native_ = std::make_unique<Native>(this);
    Native& state = *native_;
    const QString profile = QFileInfo(profileRoot).canonicalFilePath();
    state.eventLog.setFileName(QDir(profile).filePath(QStringLiteral("native-events.log")));
    if (!state.eventLog.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return refused("profile_unwritable", QStringLiteral("无法写入引擎开发日志。"));
    for (const QString& relative : {QStringLiteral("data/fonts"), QStringLiteral("data/cache"),
             QStringLiteral("data/recover"), QStringLiteral("templates"), QStringLiteral("disabled-plugins")})
        if (!QDir(profile).mkpath(relative))
            return refused("profile_unwritable", QStringLiteral("无法创建引擎缓存目录。"));

    state.manager = std::make_unique<ApplicationManager>();
    state.manager->SetEventListener(&state);
    auto& settings = state.manager->m_oSettings;
    settings.SetUserDataPath(profile.toStdWString());
    settings.user_templates_path = QDir(profile).filePath(QStringLiteral("templates")).toStdWString();
    settings.spell_dictionaries_path = directory.filePath(QStringLiteral("dictionaries")).toStdWString();
    settings.file_converter_path = directory.filePath(QStringLiteral("converter")).toStdWString();
    settings.local_editors_path = directory.filePath(QStringLiteral("editors/web-apps/apps/api/documents/index.html")).toStdWString();
    // Preserve the upstream gallery location; converter/empty is a distinct
    // native resource used for blank documents, not a substitute gallery.
    settings.system_templates_path = directory.filePath(QStringLiteral("converter/templates")).toStdWString();
    settings.connection_error_path = directory.filePath(QStringLiteral("editors/webext/noconnect.html")).toStdWString();
    settings.additional_fonts_folder.push_back(directory.filePath(QStringLiteral("fonts")).toStdWString());
    settings.system_plugins_path = QDir(profile).filePath(QStringLiteral("disabled-plugins")).toStdWString();
    settings.macroses_support = false;
    settings.plugins_support = false;
    settings.country = "CN";
    // Editor URL parameters for every view, as the upstream host sets them.
    // CreateLocalFile uses them for the UI language and the zh-CN blank seed;
    // the bundled editor maps zh-CN to its Simplified Chinese "zh" locale.
    std::wstring editorParameters = L"lang=zh-CN";
    state.manager->InitAdditionalEditorParams(editorParameters);

    for (int i = 0; i < argc; ++i) {
        const QByteArray argument = argv && argv[i] ? QByteArray(argv[i]) : QByteArray();
        if (!argument.startsWith("--external-message-loop")) state.argumentStorage.push_back(argument);
    }
    if (state.argumentStorage.empty()) state.argumentStorage.push_back(QFile::encodeName(QCoreApplication::applicationFilePath()));
    state.argumentStorage.push_back(QByteArray("--external-message-loop=1"));
    for (auto& argument : state.argumentStorage) state.arguments.push_back(argument.data());
    state.arguments.push_back(nullptr);
    const int nativeArgc = static_cast<int>(state.arguments.size()) - 1;
    CApplicationCEF::Prepare(nativeArgc, state.arguments.data());
    state.application = std::make_unique<CApplicationCEF>();
    const int result = state.application->Init_CEF(state.manager.get(), nativeArgc, state.arguments.data());
    if (state.application->IsChromiumSubprocess()) {
        state.subprocess = true;
        state.subprocessExitCode = result;
        state.log("subprocess_completed");
        return refused("cef_subprocess_completed", QStringLiteral("CEF 子进程已结束。"));
    }
    if (result != 0 || !state.manager->externalLoopInitialized) {
        state.faulted = true;
        state.log("initialization_failed");
        return refused("native_initialization_failed", QStringLiteral("原生引擎初始化或 Qt 事件循环配置失败。"));
    }
    state.manager->CheckFonts(false);
    state.manager->StartSpellChecker();
    state.manager->StartKeyboardChecker();
    state.initialized = true;
    state.log("initialized_not_roundtrip_verified");
    emit capabilitiesChanged();
    return {true, QStringLiteral("initialized"), QStringLiteral("原生引擎已初始化，可以打开 DOCX 或新建文档。")};
}

EngineCapabilities OnlyOfficeEngineAdapter::capabilities() const noexcept
{
    if (!native_ || !native_->initialized || native_->faulted || native_->closed) return {};
    return {true, true, native_->ready, native_->ready, true, native_->ready};
}

bool OnlyOfficeEngineAdapter::canCreate(DocumentKind kind) const noexcept
{
    // Spreadsheet and presentation editing is a later, separately verified step.
    return capabilities().create && kind == DocumentKind::Text;
}

bool OnlyOfficeEngineAdapter::documentModified() const noexcept
{
    return native_ && native_->modified;
}

bool OnlyOfficeEngineAdapter::needsCloseHandshake() const noexcept
{
    return native_ && native_->viewId >= 0 && !native_->closed;
}

QWidget* OnlyOfficeEngineAdapter::createEditorWidget(QWidget* parent)
{
    if (!capabilities().available || !native_->onOwnerThread()) return nullptr;
    Native& state = *native_;
    if (state.container) return state.container;
    auto* box = new QWidget(parent);
    box->setObjectName(QStringLiteral("nativeEditorContainer"));
    auto* stack = new QStackedLayout(box);
    stack->setContentsMargins(0, 0, 0, 0);
    state.container = box;
    state.stack = stack;
    EditorHost* view = state.createHost();
    if (!view) {
        state.faulted = true;
        state.container = nullptr;
        state.stack = nullptr;
        delete box;
        emit capabilitiesChanged();
        return nullptr;
    }
    state.host = view;
    state.viewId = view->GetCefView()->GetId();
    view->UpdateSize(); // Supply native client pixels before browser creation.
    return box;
}

EngineResult OnlyOfficeEngineAdapter::requestOpen(quint64 requestId, const QString& path,
                                                  bool discardCurrentChanges)
{
    if (!capabilities().available) return unavailable();
    Native& state = *native_;
    const EngineResult allowed = state.admissible(requestId);
    if (!allowed.ok) return allowed;
    if (!state.editor()) return refused("editor_host_missing", QStringLiteral("请先创建原生编辑视图。"));
    if (state.dialogViewId >= 0) return refused("engine_busy", QStringLiteral("编辑器正在等待选择保存位置。"));
    const bool replacing = state.loadRequested;
    if (replacing && !discardCurrentChanges && state.currentModified())
        return refused("unsaved_changes", QStringLiteral("当前文档仍有未保存修改。"));
    if (classifyDocument(path) != DocumentKind::Text)
        return refused("unsupported_document", QStringLiteral("当前原生阶段只支持已有、可读的 DOCX 文件。"));
    const QString canonical = canonicalFile(path);
    const int format = CCefViewEditor::GetFileFormat(canonical.toStdWString());
    if (format != AVS_OFFICESTUDIO_FILE_DOCUMENT_DOCX)
        return refused("unsupported_document", QStringLiteral("引擎未将此文件识别为 DOCX。"));
    if (replacing && samePath(canonical, state.currentPath))
        return refused("already_open", QStringLiteral("该文档已在当前窗口打开。"));
    EditorHost* target = state.host;
    if (replacing) {
        // OpenLocalFile mutates the native source/recovery paths of its view:
        // load into a fresh view so a failed open cannot damage the current one.
        target = state.createHost();
        if (!target) return refused("editor_host_failed", QStringLiteral("无法为新文档创建编辑视图；已保留当前文档。"));
        state.incomingHost = target;
        state.incomingViewId = target->GetCefView()->GetId();
        state.incomingModified = false;
    }
    EngineResult accepted = state.begin(requestId, EngineOperation::Open, canonical, discardCurrentChanges,
                                        DocumentKind::Text);
    if (!accepted.ok) return accepted; // admissible() above makes this unreachable.
    if (!replacing) {
        state.ready = false;
        state.loadRequested = true;
    }
    state.show(target); // CEF needs a visible, sized parent while the document loads.
    Native::editorOf(target)->OpenLocalFile(canonical.toStdWString(), format, L"lang=zh-CN");
    emit capabilitiesChanged();
    return accepted;
}

EngineResult OnlyOfficeEngineAdapter::requestSave(quint64 requestId, const QString& path)
{
    if (!native_) return unavailable();
    if (!native_->onOwnerThread())
        return refused("wrong_thread", QStringLiteral("必须在界面线程请求引擎操作。"));
    Native& state = *native_;
    const bool answeringDialog = state.dialogViewId >= 0;
    const auto refuse = [&state](EngineResult result) {
        state.cancelDialog(); // The editor must not keep waiting for an answer.
        return result;
    };
    if (!capabilities().save || !state.editor()) return refuse(unavailable());
    const EngineResult allowed = state.admissible(requestId);
    if (!allowed.ok) return refuse(allowed);
    if (!answeringDialog && (state.editor()->IsBuilding() || state.editor()->IsSaveLocked()))
        return refused("engine_busy", QStringLiteral("引擎仍在转换或保存文档。"));
    const QString target = saveTarget(path);
    const int format = editableFormat(state.currentKind);
    if (target.isEmpty())
        return refuse(refused("invalid_save_path", QStringLiteral("请选择已有文件夹中的有效保存位置。")));
    if (!format || classifyFileName(target) != state.currentKind)
        return refuse(refused("save_format_unsupported", QStringLiteral("当前只支持保存为与文档相同的格式。")));
    const QFileInfo targetInfo(target);
    if (targetInfo.exists() && !targetInfo.isWritable())
        return refuse(refused("document_read_only", QStringLiteral("所选文件不可写。")));
    const bool sameFile = samePath(target, state.currentPath);
    if (answeringDialog) {
        EngineResult accepted = state.begin(requestId, EngineOperation::Save, target, false, state.currentKind);
        if (!accepted.ok) return refuse(accepted);
        const int view = state.dialogViewId;
        state.dialogViewId = -1;
        state.replySavePath(view, target, format);
        return accepted;
    }
    EngineResult accepted = state.begin(requestId, EngineOperation::Save, target, false, state.currentKind);
    if (!accepted.ok) return accepted;
    if (sameFile && !state.currentModified()) {
        // The renderer ignores a save request without changes and reports
        // nothing; the file on disk already is this document.
        QMetaObject::invokeMethod(this, [this, requestId] {
            if (native_ && native_->pending.active && native_->pending.id == requestId)
                native_->finish({true, QStringLiteral("unchanged"), QStringLiteral("文档没有新的修改，无需再次写入。")},
                                native_->currentPath);
        }, Qt::QueuedConnection);
    } else if (sameFile) {
        state.editor()->Apply(new CAscMenuEvent(ASC_MENU_EVENT_TYPE_CEF_SAVE));
    } else {
        // First save of a new document, or Save As: the editor's own Save As
        // route also works without changes; it asks for the destination with
        // LOCALFILE_SAVE, answered in handleSaveDialog().
        Native::sendEditorCommand(state.editor(), L"file:saveas");
    }
    return accepted;
}

void OnlyOfficeEngineAdapter::cancelSaveDestination()
{
    if (native_ && native_->onOwnerThread()) native_->cancelDialog();
}

void OnlyOfficeEngineAdapter::setEditorAppearance(bool dark)
{
    if (!native_ || !native_->onOwnerThread()) return;
    if (native_->darkAppearance == dark && !native_->appearanceChanged) {
        if (!dark) return; // nothing to undo in a view that never went dark
    }
    native_->appearanceChanged = native_->appearanceChanged || dark != native_->darkAppearance;
    native_->darkAppearance = dark;
    native_->applyAppearance();
}

bool OnlyOfficeEngineAdapter::supportsEditorCommand(const QString& name) const noexcept
{
    return capabilities().edit && editorCommandNames().contains(name);
}

EngineResult OnlyOfficeEngineAdapter::requestEditorCommand(quint64 requestId, const QString& name,
                                                           const QJsonObject& arguments)
{
    if (!capabilities().edit || !native_->editor()) return unavailable();
    Native& state = *native_;
    if (!state.onOwnerThread()) return refused("wrong_thread", QStringLiteral("必须在界面线程请求引擎操作。"));
    if (!supportsEditorCommand(name)) return refused("unsupported_command", QStringLiteral("编辑器不支持这个命令。"));
    if (!requestId || state.acceptedIds.contains(requestId))
        return refused("invalid_request_id", QStringLiteral("请求编号无效或已使用。"));
    if (state.pending.active || state.incomingViewId >= 0 || state.dialogViewId >= 0)
        return refused("engine_busy", QStringLiteral("文档操作进行中，请稍后再执行命令。"));
    if (state.command.active) return refused("engine_busy", QStringLiteral("上一条编辑器命令尚未完成。"));
    if (state.editor()->IsBuilding() || state.editor()->IsSaveLocked())
        return refused("engine_busy", QStringLiteral("引擎仍在转换或保存文档。"));
    // The engine re-checks what it executes; it does not rely on the caller.
    const QString text = arguments.value(QStringLiteral("text")).toString();
    const bool takesText = name == QStringLiteral("text.insert");
    if (takesText ? (arguments.size() != 1 || text.isEmpty() || text.size() > 10000) : !arguments.isEmpty())
        return refused("invalid_argument", QStringLiteral("命令参数无效。"));
    state.acceptedIds.insert(requestId);
    state.command = {true, requestId, name};
    state.commandTimer->start(10000);
    state.logCommand("editor_command_accepted", requestId);
    state.editor()->ExecuteEditorScript(editorCommandScript(requestId, name, arguments).toStdString());
    return {true, QStringLiteral("accepted"), QStringLiteral("命令已交给编辑器，等待它回报结果。")};
}

EngineResult OnlyOfficeEngineAdapter::requestCreate(quint64 requestId, DocumentKind kind, const QString& title,
                                                    bool discardCurrentChanges)
{
    if (!capabilities().available) return unavailable();
    Native& state = *native_;
    const EngineResult allowed = state.admissible(requestId);
    if (!allowed.ok) return allowed;
    if (!canCreate(kind))
        return refused("create_unsupported", QStringLiteral("当前版本只支持新建文字文档。"));
    if (!validTitle(title, kind))
        return refused("invalid_title", QStringLiteral("新文档名称无效。"));
    if (!state.editor()) return refused("editor_host_missing", QStringLiteral("请先创建原生编辑视图。"));
    if (state.dialogViewId >= 0) return refused("engine_busy", QStringLiteral("编辑器正在等待选择保存位置。"));
    const bool replacing = state.loadRequested;
    if (replacing && !discardCurrentChanges && state.currentModified())
        return refused("unsaved_changes", QStringLiteral("当前文档仍有未保存修改。"));
    EditorHost* target = state.host;
    if (replacing) {
        target = state.createHost();
        if (!target) return refused("editor_host_failed", QStringLiteral("无法为新文档创建编辑视图；已保留当前文档。"));
        state.incomingHost = target;
        state.incomingViewId = target->GetCefView()->GetId();
        state.incomingModified = false;
    }
    EngineResult accepted = state.begin(requestId, EngineOperation::Create, {}, discardCurrentChanges, kind);
    if (!accepted.ok) return accepted;
    if (!replacing) {
        state.ready = false;
        state.loadRequested = true;
    }
    state.show(target);
    // Blank seed: converter/empty/zh-CN/new.docx (selected by the lang editor
    // parameter). The title names the document until its first save.
    Native::editorOf(target)->CreateLocalFile(AscEditorType::etDocument, title.toStdWString());
    emit capabilitiesChanged();
    return accepted;
}

EngineResult OnlyOfficeEngineAdapter::requestClose(quint64 requestId, bool discardChanges)
{
    if (!native_ || !native_->initialized) return unavailable();
    Native& state = *native_;
    if (!state.onOwnerThread()) return refused("wrong_thread", QStringLiteral("必须在界面线程请求引擎操作。"));
    if (!needsCloseHandshake()) return refused("no_native_view", QStringLiteral("当前没有待关闭的原生视图。"));
    if (state.dialogViewId >= 0) return refused("engine_busy", QStringLiteral("编辑器正在等待选择保存位置。"));
    if (state.currentModified() && !discardChanges)
        return refused("unsaved_changes", QStringLiteral("当前文档仍有未保存修改。"));
    const EngineResult accepted = state.begin(requestId, EngineOperation::Close, {}, discardChanges);
    if (!accepted.ok) return accepted;
    if (!state.loadRequested) {
        state.destroyRequested = true;
        state.manager->DestroyCefView(state.viewId, true);
    } else if (state.editor()) {
        state.editor()->Apply(new CAscMenuEvent(ASC_MENU_EVENT_TYPE_CEF_CLOSE));
    } else {
        // Destruction may already be in progress after a timeout. Wait for the
        // authoritative DESTROYWINDOW callback; never synthesize success.
        state.log("awaiting_native_destruction");
    }
    return accepted;
}

int OnlyOfficeEngineAdapter::runNativeEventLoop(bool& used)
{
    used = false;
    if (!native_) return 0;
    Native& state = *native_;
    if (state.subprocess) { used = true; return state.subprocessExitCode; }
    if (!state.initialized || !state.application || !state.onOwnerThread()) return 0;
    state.loopRunning = true;
    const int result = state.application->RunMessageLoop(used);
    state.loopRunning = false;
    state.loopReturned = true;
    // Upstream keeps m_bIsMessageLoopRunned set after Run() returns. Clear it
    // before CloseApplication so Close() can perform actual context shutdown.
    state.application->ExitMessageLoop();
    // The final browser can quit Qt before the queued DESTROYWINDOW snapshot
    // and UI completion reach their receivers. Drain those queued metacalls.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    state.log("message_loop_returned");
    return result;
}

void OnlyOfficeEngineAdapter::shutdownNative()
{
    if (!native_) return;
    Native& state = *native_;
    if (!state.onOwnerThread()) return;
    if (state.loopRunning || needsCloseHandshake()) {
        state.log("shutdown_waits_for_close_handshake");
        return;
    }
    state.timer->stop();
    state.stopCallbacks();
    if (state.manager && !state.subprocess) state.manager->CloseApplication();
    state.application.reset();
    if (state.manager) state.manager->SetEventListener(&droppedEventSink());
    state.manager.reset();
    state.initialized = false;
    state.log("shutdown_completed");
    emit capabilitiesChanged();
}
} // namespace openoffice
