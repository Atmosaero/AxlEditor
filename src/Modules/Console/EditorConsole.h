#pragma once

#include "Editor/EditorTheme.h"
#include "Editor/IEditorLog.h"
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

// Small typed console service, available to editor modules through EditorContext.
// Log is called on the editor thread; messages are plain text, never HTML.
class EditorConsole final : public QPlainTextEdit
{
public:
    explicit EditorConsole(QWidget* parent = nullptr) : QPlainTextEdit(parent)
    {
        setObjectName("ConsoleOutput");
        setReadOnly(true);
        setMaximumBlockCount(2000);
    }

    void Log(ConsoleMessageType type, const QString& message)
    {
        QString label;
        QColor color;
        const auto& theme = DarkTheme();
        switch (type) {
        case ConsoleMessageType::Info:    label = "Info";    color = theme.info; break;
        case ConsoleMessageType::Warning: label = "Warning"; color = theme.warning; break;
        case ConsoleMessageType::Error:   label = "Error";   color = theme.error; break;
        case ConsoleMessageType::Success: label = "Success"; color = theme.success; break;
        }

        const bool follow = verticalScrollBar()->value() == verticalScrollBar()->maximum();
        QTextCursor cursor(document());
        cursor.movePosition(QTextCursor::End);
        QTextCharFormat format;
        format.setForeground(color);
        // Prefix each line so multiline messages keep their severity when copied.
        for (const auto& line : message.split('\n')) {
            if (!document()->isEmpty())
                cursor.insertBlock();
            cursor.insertText("[" + label + "] " + line, format);
        }
        if (follow)
            verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }
};
