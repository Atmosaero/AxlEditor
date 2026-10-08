#include "Tools/ScriptCanvas/ScriptCanvasTool.h"
#include "Tools/ScriptCanvas/ScriptCanvasView.h"
#include "Editor/Qt/EditorContext.h"
#include "Tools/ScriptCanvas/GraphDocument.h"
#include <QToolBar>

void ScriptCanvasTool::Initialize(EditorContext& context)
{
    if (dock_) return;
    dock_ = new QDockWidget("Script Canvas");
    dock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    dock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock_->setProperty("keepDockTitleBar", true);
    auto* view = new ScriptCanvasView;
    auto* panel = new QWidget; auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto* toolbar = new QToolBar(panel); toolbar->setObjectName("ScriptCanvasToolbar");
    auto* title = new QLabel(toolbar); toolbar->addWidget(title);
    layout->addWidget(toolbar); layout->addWidget(view, 1); dock_->setWidget(panel);
    if (!context.RegisterDock("ScriptCanvasDock", dock_.data(), Qt::BottomDockWidgetArea)) { Shutdown(); return; }
    dock_->hide();
    dock_->setFloating(true);
    dock_->resize(900, 600);
    document_ = new GraphDocument(*view, context, *title);
    context.RegisterService<GraphDocument>(*document_);
    QObject::connect(document_, &QObject::destroyed, panel, [&context] {
        if (!context.GetService<GraphDocument>()) context.UnregisterService<GraphDocument>();
    });
    auto* fresh = toolbar->addAction("New"); auto* load = toolbar->addAction("Open..."); auto* save = toolbar->addAction("Save");
    QObject::connect(fresh, &QAction::triggered, document_, [doc = document_.data()] { doc->New(); });
    QObject::connect(load, &QAction::triggered, document_, [doc = document_.data()] { doc->OpenDialog(); });
    QObject::connect(save, &QAction::triggered, document_, [doc = document_.data()] { doc->Save(); });
    auto* open = new QAction("Script Canvas", dock_.data());
    open->setObjectName("OpenScriptCanvasAction");
    QObject::connect(open, &QAction::triggered, dock_.data(), [dock = dock_.data(), view] {
        dock->show();
        dock->raise();
        view->setFocus();
    });
    context.RegisterAction(open);
}

void ScriptCanvasTool::Shutdown()
{
    delete document_.data(); document_ = nullptr; // Unregister document while its view and context are alive.
    delete dock_.data();
    dock_ = nullptr;
}

QDockWidget* ScriptCanvasTool::Dock() const { return dock_.data(); }
