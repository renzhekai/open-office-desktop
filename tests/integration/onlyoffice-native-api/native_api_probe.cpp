// Compile against the actual pinned upstream headers. No main(), runtime,
// replacement declarations, dummy native definitions, or application adapter.
#include <qcefview.h>
#include <qascapplicationmanager.h>
#include <cefapplication.h>

#include <type_traits>
#include <utility>

namespace onlyoffice_native_api_probe {
using namespace NSEditorApi;

// Host creation and CEF process/message-loop declarations.
static_assert(std::is_base_of_v<QWidget, QCefView>);
static_assert(std::is_base_of_v<CCefViewWidgetImpl, QCefView>);
static_assert(std::is_base_of_v<CAscApplicationManager, QAscApplicationManager>);
static_assert(std::is_constructible_v<QCefView, QWidget*>);
static_assert(std::is_default_constructible_v<QAscApplicationManager>);
static_assert(std::is_default_constructible_v<CApplicationCEF>);
static_assert(std::is_same_v<decltype(&QCefView::Create),
    void (QCefView::*)(CAscApplicationManager*, CefViewWrapperType)>);
static_assert(std::is_same_v<decltype(&QCefView::GetCefView), CCefView* (QCefView::*)()>);
static_assert(std::is_same_v<decltype(&CApplicationCEF::Prepare), void (*)(int, char**)>);
static_assert(std::is_same_v<decltype(&CApplicationCEF::Init_CEF),
    int (CApplicationCEF::*)(CAscApplicationManager*, int, char**)>);
static_assert(std::is_same_v<decltype(&CApplicationCEF::RunMessageLoop),
    int (CApplicationCEF::*)(bool&)>);
static_assert(std::is_same_v<decltype(&CApplicationCEF::IsChromiumSubprocess),
    bool (CApplicationCEF::*)()>);
static_assert(std::is_same_v<decltype(&CAscApplicationManager::CloseApplication),
    void (CAscApplicationManager::*)()>);
static_assert(std::is_same_v<decltype(&CAscApplicationManager::SetEventListener),
    void (CAscApplicationManager::*)(CAscCefMenuEventListener*)>);
static_assert(std::is_same_v<decltype(&CAscApplicationManager::GetEventListener),
    CAscCefMenuEventListener* (CAscApplicationManager::*)()>);
static_assert(std::is_same_v<decltype(&CAscApplicationManager::GetViewById),
    CCefView* (CAscApplicationManager::*)(int)>);

// OpenLocalFile's void return cannot be interpreted as "document is ready".
static_assert(std::is_same_v<decltype(&CCefViewEditor::OpenLocalFile),
    void (CCefViewEditor::*)(const std::wstring&, const int&, const std::wstring&)>);
static_assert(std::is_same_v<decltype(&CCefViewEditor::GetFileFormat),
    int (*)(const std::wstring&)>);
static_assert(std::is_same_v<decltype(&CCefView::GetId), int (CCefView::*)()>);
static_assert(std::is_same_v<decltype(&CCefView::GetModified), bool (CCefView::*)()>);
static_assert(std::is_same_v<decltype(&CCefViewEditor::GetLocalFilePath),
    std::wstring (CCefViewEditor::*)()>);
static_assert(std::is_same_v<decltype(&CCefView::Apply),
    void (CCefView::*)(CAscMenuEvent*)>);
static_assert(std::is_same_v<decltype(&CAscApplicationManager::Apply),
    void (CAscApplicationManager::*)(CAscMenuEvent*)>);

// Incoming doc:onready uses CAscExecCommand, with singular Param.
static_assert(std::is_same_v<decltype(&CAscExecCommand::get_Command),
    std::wstring (CAscExecCommand::*)()>);
static_assert(std::is_same_v<decltype(&CAscExecCommand::get_Param),
    std::wstring (CAscExecCommand::*)()>);
// Native editor commands use a different class, with plural Params.
static_assert(std::is_same_v<decltype(&CAscEditorExecuteCommand::put_Command),
    void (CAscEditorExecuteCommand::*)(const std::wstring&)>);
static_assert(std::is_same_v<decltype(&CAscEditorExecuteCommand::put_Params),
    void (CAscEditorExecuteCommand::*)(const std::wstring&)>);
// The upstream macOS Save As menu uses this separate script-command payload.
static_assert(std::is_same_v<decltype(&CAscExecCommandJS::put_Command),
    void (CAscExecCommandJS::*)(const std::wstring&)>);
static_assert(std::is_same_v<decltype(&CAscExecCommandJS::put_FrameName),
    void (CAscExecCommandJS::*)(const std::wstring&)>);
static_assert(std::is_same_v<decltype(&CAscLocalSaveFileDialog::put_Id),
    void (CAscLocalSaveFileDialog::*)(const int&)>);
static_assert(std::is_same_v<decltype(&CAscLocalSaveFileDialog::put_Path),
    void (CAscLocalSaveFileDialog::*)(const std::wstring&)>);
static_assert(std::is_same_v<decltype(&CAscLocalSaveFileDialog::put_FileType),
    void (CAscLocalSaveFileDialog::*)(const int&)>);

// Callback payload, view correlation and ownership declarations.
static_assert(std::is_same_v<decltype(&CAscCefMenuEvent::get_SenderId),
    int (CAscCefMenuEvent::*)()>);
static_assert(std::is_same_v<decltype(&CAscDocumentOnSaveData::get_Id),
    int (CAscDocumentOnSaveData::*)()>);
static_assert(std::is_same_v<decltype(&CAscDocumentOnSaveData::get_IsCancel),
    bool (CAscDocumentOnSaveData::*)()>);
static_assert(std::is_same_v<decltype(&CAscDocumentName::get_Path),
    std::wstring (CAscDocumentName::*)()>);
static_assert(std::is_same_v<decltype(&CAscDocumentModifyChanged::get_Changed),
    bool (CAscDocumentModifyChanged::*)()>);
static_assert(std::is_same_v<decltype(&IMenuEventDataBase::AddRef),
    unsigned int (IMenuEventDataBase::*)()>);
static_assert(std::is_same_v<decltype(&IMenuEventDataBase::Release),
    unsigned int (IMenuEventDataBase::*)()>);
static_assert(std::is_same_v<decltype(CAscMenuEvent::m_pData), IMenuEventDataBase*>);

// Real calls emit imported references into the object file. They are never
// linked or executed here; each function checks just one declaration boundary.
void compile_view_creation(QCefView& view, QAscApplicationManager& manager)
{
    view.Create(&manager, cvwtEditor);
    (void)static_cast<CCefViewEditor*>(view.GetCefView());
}

void compile_open(CCefViewEditor& editor, const std::wstring& path)
{
    editor.OpenLocalFile(path, CCefViewEditor::GetFileFormat(path), L"");
}

void compile_save(CCefView& view)
{
    view.Apply(new CAscMenuEvent(ASC_MENU_EVENT_TYPE_CEF_SAVE));
}

void compile_editor_command(CCefView& view, const std::wstring& name,
                            const std::wstring& params)
{
    auto* event = new CAscMenuEvent(ASC_MENU_EVENT_TYPE_CEF_EDITOR_EXECUTE_COMMAND);
    auto* command = new CAscEditorExecuteCommand;
    command->put_Command(name);
    command->put_Params(params);
    event->m_pData = command;
    view.Apply(event);
}

// Matches the upstream macOS menu declaration usage. This does not establish
// that the Windows renderer has loaded the handler, or that Save As succeeds.
void compile_save_as_script_command(CCefView& view)
{
    auto* event = new CAscMenuEvent(ASC_MENU_EVENT_TYPE_CEF_EXECUTE_COMMAND_JS);
    auto* command = new CAscExecCommandJS;
    command->put_Command(L"file:saveas");
    command->put_FrameName(L"frameEditor");
    event->m_pData = command;
    view.Apply(event);
}

void compile_save_path_reply(CAscApplicationManager& manager, int viewId,
                             const std::wstring& path, int fileType)
{
    auto* event = new CAscMenuEvent(ASC_MENU_EVENT_TYPE_CEF_LOCALFILE_SAVE_PATH);
    auto* data = new CAscLocalSaveFileDialog;
    data->put_Id(viewId);
    data->put_Path(path);
    data->put_FileType(fileType);
    event->m_pData = data;
    manager.Apply(event);
}

// This is an override-signature check, not the application's event listener.
// A real bridge must retain the host's existing event dispatch, snapshot data
// before Release(), and queue completion to the Qt object's thread.
class ListenerSignatureProbe final : public CAscCefMenuEventListener {
public:
    void OnEvent(CAscCefMenuEvent* event) override
    {
        CAscCefMenuEventListener::OnEvent(event);
    }
    bool IsSupportEvent(int eventType) override
    {
        switch (eventType) {
        case ASC_MENU_EVENT_TYPE_CEF_EXECUTE_COMMAND:
        case ASC_MENU_EVENT_TYPE_CEF_DOCUMENT_NAME:
        case ASC_MENU_EVENT_TYPE_CEF_MODIFY_CHANGED:
        case ASC_MENU_EVENT_TYPE_CEF_ONSAVE:
        case ASC_MENU_EVENT_TYPE_CEF_LOCALFILE_SAVE:
        case ASC_MENU_EVENT_TYPE_CEF_DESTROYWINDOW:
        case ASC_MENU_EVENT_TYPE_PAGE_CRASH:
            return true;
        default:
            return false;
        }
    }
};
static_assert(!std::is_abstract_v<ListenerSignatureProbe>);
} // namespace onlyoffice_native_api_probe
