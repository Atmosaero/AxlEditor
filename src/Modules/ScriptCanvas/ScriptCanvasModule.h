#pragma once

#include "Editor/IEditorModule.h"
#include <QPointer>

class QDockWidget;

class ScriptCanvasModule final : public IEditorModule
{
public:
    ScriptCanvasModule() = default;
    ~ScriptCanvasModule() override { Shutdown(); }
    ScriptCanvasModule(const ScriptCanvasModule&) = delete;
    ScriptCanvasModule& operator=(const ScriptCanvasModule&) = delete;
    void Initialize(EditorContext& context) override;
    void Shutdown() override;
    QDockWidget* Dock() const;

private:
    QPointer<QDockWidget> dock_;
};
