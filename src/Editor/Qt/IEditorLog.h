#pragma once

#include <QString>

// Kept as the existing severity type so tool callers remain compatible.
enum class ConsoleMessageType { Info, Warning, Error, Success };

// Borrowed editor-thread service; its tool unregisters it before destruction.
// Show exposes the output UI without making the shell depend on a console widget.
class IEditorLog
{
public:
    virtual ~IEditorLog() = default;
    virtual void Log(ConsoleMessageType type, const QString& message) = 0;
    virtual void Show() {}
};
