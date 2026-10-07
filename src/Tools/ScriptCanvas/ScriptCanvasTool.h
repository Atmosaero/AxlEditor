#pragma once

#include "Editor/Application/IEditorTool.h"
#include <QPointer>

class QDockWidget;
class GraphDocument;

class ScriptCanvasTool final : public IEditorTool
{
public:
    ScriptCanvasTool() = default;
    ~ScriptCanvasTool() override { Shutdown(); }
    ScriptCanvasTool(const ScriptCanvasTool&) = delete;
    ScriptCanvasTool& operator=(const ScriptCanvasTool&) = delete;
    void Initialize(EditorContext& context) override;
    void Shutdown() override;
    QDockWidget* Dock() const;

private:
    QPointer<QDockWidget> dock_;
    QPointer<GraphDocument> document_;
};
