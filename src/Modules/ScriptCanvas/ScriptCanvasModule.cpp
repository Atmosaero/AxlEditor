#include "Modules/ScriptCanvas/ScriptCanvasModule.h"
#include "Modules/ScriptCanvas/ScriptCanvasView.h"
#include "Editor/EditorContext.h"

void ScriptCanvasModule::Initialize(EditorContext& context)
{
    if (dock_) return;
    dock_ = new QDockWidget("Script Canvas");
    auto* view = new ScriptCanvasView;
    dock_->setWidget(view);
    if (!context.RegisterDock("ScriptCanvasDock", dock_.data(), Qt::BottomDockWidgetArea)) { Shutdown(); return; }
    dock_->hide();
    auto* open = new QAction("Script Canvas", dock_.data());
    open->setObjectName("OpenScriptCanvasAction");
    QObject::connect(open, &QAction::triggered, dock_.data(), [dock = dock_.data(), view] {
        dock->show();
        dock->raise();
        view->setFocus();
    });
    context.RegisterAction(open);
}

void ScriptCanvasModule::Shutdown()
{
    delete dock_.data();
    dock_ = nullptr;
}

QDockWidget* ScriptCanvasModule::Dock() const { return dock_.data(); }
