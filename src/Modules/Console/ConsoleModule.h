#pragma once

#include "Editor/IEditorModule.h"
#include "Editor/IEditorLog.h"
#include "Editor/ConsoleCommands.h"
#include <QPointer>

class EditorContext;
class EditorConsole;
class QDockWidget;

// Explicit Shutdown must run while the borrowed EditorContext is still alive.
class ConsoleModule final : public IEditorModule, public IEditorLog
{
public:
    ConsoleModule() = default;
    ~ConsoleModule() override { Shutdown(); }
    ConsoleModule(const ConsoleModule&) = delete;
    ConsoleModule& operator=(const ConsoleModule&) = delete;

    void Initialize(EditorContext& context) override;
    void Shutdown() override;
    QDockWidget* Dock() const;
    void Log(ConsoleMessageType type, const QString& message) override;
    void Show() override;

private:
    EditorContext* context_ = nullptr;
    QPointer<QDockWidget> dock_;
    QPointer<EditorConsole> output_;
    QPointer<ConsoleCommands> commands_;
    std::vector<ConsoleCommands::Registration> registrations_;
};
