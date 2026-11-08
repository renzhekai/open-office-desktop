#include "onlyoffice_engine_adapter.h"

namespace openoffice {
struct OnlyOfficeEngineAdapter::Native {};
namespace {

EngineResult unavailable()
{
    return {false,
            QStringLiteral("engine_unavailable"),
            QStringLiteral("ONLYOFFICE 编辑引擎尚未接入，本次操作未执行。")};
}

} // namespace

OnlyOfficeEngineAdapter::OnlyOfficeEngineAdapter(QObject* parent)
    : OfficeEngine(parent)
{
}

OnlyOfficeEngineAdapter::~OnlyOfficeEngineAdapter() = default;

EngineResult OnlyOfficeEngineAdapter::initializeNative(const QString& runtimeRoot,
    const QString& profileRoot, int argc, char** argv)
{
    Q_UNUSED(runtimeRoot);
    Q_UNUSED(profileRoot);
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    return unavailable();
}

int OnlyOfficeEngineAdapter::runNativeEventLoop(bool& used)
{
    used = false;
    return 0;
}

void OnlyOfficeEngineAdapter::shutdownNative() {}

bool OnlyOfficeEngineAdapter::documentModified() const noexcept { return false; }
bool OnlyOfficeEngineAdapter::needsCloseHandshake() const noexcept { return false; }
bool OnlyOfficeEngineAdapter::canCreate(DocumentKind kind) const noexcept
{
    Q_UNUSED(kind);
    return false;
}
void OnlyOfficeEngineAdapter::cancelSaveDestination() {}
void OnlyOfficeEngineAdapter::setEditorAppearance(bool dark) { Q_UNUSED(dark); }
bool OnlyOfficeEngineAdapter::supportsEditorCommand(const QString& name) const noexcept
{
    Q_UNUSED(name);
    return false;
}

EngineResult OnlyOfficeEngineAdapter::requestEditorCommand(quint64 requestId, const QString& name,
                                                           const QJsonObject& arguments)
{
    Q_UNUSED(requestId);
    Q_UNUSED(name);
    Q_UNUSED(arguments);
    return unavailable();
}

EngineResult OnlyOfficeEngineAdapter::requestClose(quint64 requestId, bool discardChanges)
{
    Q_UNUSED(requestId);
    Q_UNUSED(discardChanges);
    return unavailable();
}

EngineCapabilities OnlyOfficeEngineAdapter::capabilities() const noexcept
{
    return {};
}

EngineResult OnlyOfficeEngineAdapter::requestOpen(quint64 requestId, const QString& path,
                                                  bool discardCurrentChanges)
{
    Q_UNUSED(requestId);
    Q_UNUSED(path);
    Q_UNUSED(discardCurrentChanges);
    return unavailable();
}

EngineResult OnlyOfficeEngineAdapter::requestSave(quint64 requestId, const QString& path)
{
    Q_UNUSED(requestId);
    Q_UNUSED(path);
    return unavailable();
}

EngineResult OnlyOfficeEngineAdapter::requestCreate(quint64 requestId, DocumentKind kind,
                                                    const QString& title, bool discardCurrentChanges)
{
    Q_UNUSED(requestId);
    Q_UNUSED(kind);
    Q_UNUSED(title);
    Q_UNUSED(discardCurrentChanges);
    return unavailable();
}

QWidget* OnlyOfficeEngineAdapter::createEditorWidget(QWidget* parent)
{
    Q_UNUSED(parent);
    return nullptr;
}

} // namespace openoffice
