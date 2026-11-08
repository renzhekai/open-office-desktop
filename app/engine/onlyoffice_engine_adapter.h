#pragma once

#include "office_engine.h"
#include <memory>

namespace openoffice {

// The build chooses the unavailable baseline or the native implementation.
// Construction never initializes CEF or advertises a usable engine by itself.
class OnlyOfficeEngineAdapter final : public OfficeEngine {
public:
    explicit OnlyOfficeEngineAdapter(QObject* parent = nullptr);
    ~OnlyOfficeEngineAdapter() override;

    EngineResult initializeNative(const QString& runtimeRoot, const QString& profileRoot,
                                  int argc, char** argv);
    int runNativeEventLoop(bool& used);
    void shutdownNative();

    EngineCapabilities capabilities() const noexcept override;
    bool documentModified() const noexcept override;
    bool needsCloseHandshake() const noexcept override;
    bool canCreate(DocumentKind kind) const noexcept override;
    EngineResult requestOpen(quint64 requestId, const QString& path, bool discardCurrentChanges) override;
    EngineResult requestSave(quint64 requestId, const QString& path) override;
    EngineResult requestCreate(quint64 requestId, DocumentKind kind, const QString& title,
                               bool discardCurrentChanges) override;
    EngineResult requestClose(quint64 requestId, bool discardChanges) override;
    void cancelSaveDestination() override;
    void setEditorAppearance(bool dark) override;
    bool supportsEditorCommand(const QString& name) const noexcept override;
    EngineResult requestEditorCommand(quint64 requestId, const QString& name,
                                      const QJsonObject& arguments) override;
    QWidget* createEditorWidget(QWidget* parent) override;

private:
    struct Native;
    std::unique_ptr<Native> native_;
};

} // namespace openoffice
