#pragma once

#include "Editor/Application/IEditorTool.h"
#include "Editor/Qt/IEditorLog.h"
#include "Editor/Qt/ConsoleCommands.h"
#include <QPointer>

class EditorContext;
class EditorConsole;
class QDockWidget;

// Explicit Shutdown must run while the borrowed EditorContext is still alive.
class ConsoleTool final : public IEditorTool, public IEditorLog
{
public:
    ConsoleTool() = default;
    ~ConsoleTool() override { Shutdown(); }
    ConsoleTool(const ConsoleTool&) = delete;
    ConsoleTool& operator=(const ConsoleTool&) = delete;

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
