#include "Editor/Qt/EditorWindow.h"
#include "Editor/Qt/EditorTheme.h"
#include "Editor/Qt/IEditorLog.h"
#include "Editor/Qt/InspectorProperties.h"
#include "Editor/Qt/IEditorViewport.h"
#include "Editor/Application/EditorSession.h"

#include <QApplication>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <set>
#include <QToolButton>
#include <QToolTip>
#include <QWidgetAction>
#include <QVBoxLayout>
#include <stdexcept>
#include <utility>

namespace {
// Numeric typing stays native; drag the axis label horizontally to scrub its value.
class TransformAxisLabel final : public QLabel
{
public:
    TransformAxisLabel(const QString& axis, QDoubleSpinBox& input, QWidget* parent, EditorOperations& operations)
        : QLabel(axis, parent), input_(input), operations_(operations)
    {
        setProperty("transformAxis", axis);
        setFixedWidth(12);
        setAlignment(Qt::AlignCenter);
        setCursor(Qt::SizeHorCursor);
        setBuddy(&input);
        setToolTip("Drag left/right to change " + input.toolTip());
        input.installEventFilter(this);
    }

    void CancelDrag(bool cancel = false)
    {
        const bool wasDragging = std::exchange(dragging_, false);
        if (wasDragging) {
            if (cancel) operations_.CancelTransformGesture();
            else operations_.CommitTransformGesture();
        }
        if (wasDragging && QWidget::mouseGrabber() == this) releaseMouse();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !input_.isEnabled()) {
            QLabel::mousePressEvent(event);
            return;
        }
        input_.interpretText();
        if (!isVisible() || !input_.isVisible()) return;
        input_.setFocus(Qt::MouseFocusReason);
        startX_ = event->globalPosition().x();
        startValue_ = input_.value();
        if (!operations_.BeginTransformGesture(operations_.Selection())) return;
        dragging_ = true;
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!dragging_) { QLabel::mouseMoveEvent(event); return; }
        if (!(event->buttons() & Qt::LeftButton)) { CancelDrag(); return; }
        input_.setValue(startValue_ + (event->globalPosition().x() - startX_) * input_.singleStep() / 8.0);
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && dragging_) {
            CancelDrag();
            event->accept();
        } else QLabel::mouseReleaseEvent(event);
    }

    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate
            || event->type() == QEvent::UngrabMouse)
            CancelDrag();
        return QLabel::event(event);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::FocusOut) CancelDrag();
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
            CancelDrag(true);
        return QLabel::eventFilter(watched, event);
    }

private:
    QDoubleSpinBox& input_;
    EditorOperations& operations_;
    bool dragging_ = false;
    double startX_ = 0;
    double startValue_ = 0;
};
}

EditorWindow::EditorWindow(ISceneProvider& scene, IAssetProvider& assets, QWidget* viewport,
    IObjectProperties* properties, ToolList tools)
    : scene_(scene), assets_(assets), properties_(properties), fileMenu_(menuBar()->addMenu("&File")),
      editMenu_(menuBar()->addMenu("&Edit")),
      viewMenu_(menuBar()->addMenu("&View")),
      toolsMenu_(menuBar()->addMenu("&Tools")),
      helpMenu_(menuBar()->addMenu("&Help")),
      context_(*this, *viewMenu_, *toolsMenu_),
      tools_(std::move(tools)),
      settings_(QCoreApplication::applicationDirPath() + "/editor-layout.ini", QSettings::IniFormat),
      viewport_(viewport)
{
    setObjectName("AxlEditorWindow");
    setWindowTitle("Axl Editor");
    resize(1440, 900);
    setDockNestingEnabled(true);
    setDocumentMode(true);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    context_.RegisterService<QMainWindow>(*this);
    context_.RegisterService<ISceneProvider>(scene_);
    context_.RegisterService<IAssetProvider>(assets_);
    if (properties_)
        context_.RegisterService<IObjectProperties>(*properties_);

    context_.RegisterService<EditorOperations>(operations_);
    context_.RegisterService<DocumentActions>(documents_);
    context_.RegisterService<AssetOpenHandlers>(assetHandlers_);
    CreateViewport();
    CreatePanels();

    // The application chooses tools; the shell knows only their lifecycle.
    size_t initialized = 0;
    try {
        for (auto& tool : tools_) {
            ++initialized;
            if (tool) tool->Initialize(context_);
        }
    } catch (...) {
        while (initialized > 0) {
            auto& tool = tools_[--initialized];
            if (tool) tool->Shutdown();
        }
        throw;
    }
    operations_.changed = [this](EditorOperations::Change change) {
        selectedObject_ = operations_.Selection();
        if (change == EditorOperations::Change::Structure || change == EditorOperations::Change::Selection
            || change == EditorOperations::Change::Session) RefreshScene();
        else QTimer::singleShot(0, this, [this] { UpdateInspector(); viewport_->update(); });
    };
    selectedObject_ = operations_.Selection();
    UpdateInspector();
    CreateActions();
    if (auto* commands = context_.GetService<ConsoleCommands>()) {
        sceneCommands_.push_back(commands->RegisterCommand("scene.create", "Create an object: scene.create [name]",
            [this](const QStringList& args, IEditorLog& log) {
                const auto result = operations_.Create(args.isEmpty() ? "Object" : args.join(' ').toStdString());
                log.Log(result ? ConsoleMessageType::Success : ConsoleMessageType::Error,
                    result ? QString("Created object %1").arg(result.object) : QString::fromStdString(result.error));
            }));
        sceneCommands_.push_back(commands->RegisterCommand("scene.delete", "Delete selected object",
            [this](const QStringList&, IEditorLog& log) {
                const auto result = operations_.Delete(operations_.Selection());
                log.Log(result ? ConsoleMessageType::Success : ConsoleMessageType::Error,
                    result ? "Deleted object" : QString::fromStdString(result.error));
            }));
        sceneCommands_.push_back(commands->RegisterCommand("scene.select", "Select object ID, or 0 to clear",
            [this](const QStringList& args, IEditorLog& log) {
                bool valid = false; const auto id = args.value(0).toULongLong(&valid);
                const auto result = valid ? operations_.Select(id) : EditorOperations::Result{false, 0, "Expected object ID"};
                log.Log(result ? ConsoleMessageType::Success : ConsoleMessageType::Error,
                    result ? "Selection updated" : QString::fromStdString(result.error));
            }));
    }
    for (auto* dock : findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly))
        if (dock != assetsDock_ && dockWidgetArea(dock) == Qt::BottomDockWidgetArea)
            tabifyDockWidget(assetsDock_, dock);
    assetsDock_->raise();
    ApplyDefaultDockSizes();
    defaultLayout_ = saveState(LayoutVersion);
    const bool restored = RestoreLayout();
    if (!restored)
        QTimer::singleShot(0, this, [this] {
            ApplyDefaultDockSizes();
            defaultLayout_ = saveState(LayoutVersion);
        });
    UpdateBottomDockTitles();
    connect(this, &QMainWindow::tabifiedDockWidgetActivated, this, [this] { UpdateBottomDockTitles(); });
    for (auto* dock : findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        connect(dock, &QDockWidget::topLevelChanged, this, [this] { UpdateBottomDockTitles(); });
        connect(dock, &QDockWidget::dockLocationChanged, this, [this] {
            QTimer::singleShot(0, this, [this] { UpdateBottomDockTitles(); });
        });
    }
    statusBar()->showMessage("Ready");
}

EditorWindow::~EditorWindow()
{
    operations_.changed = {};
    sceneCommands_.clear();
    if (auto* view = dynamic_cast<IEditorViewport*>(viewport_)) view->BindOperations(nullptr);
    context_.UnregisterService<EditorOperations>();
    context_.UnregisterService<DocumentActions>();
    context_.UnregisterService<AssetOpenHandlers>();
    // Shut tools down in reverse initialization order while context is alive.
    for (auto it = tools_.rbegin(); it != tools_.rend(); ++it)
        if (*it) (*it)->Shutdown();
    tools_.clear(); // Tool destructors also run before context and Qt children disappear.
    context_.UnregisterService<InspectorProperties>();
    context_.UnregisterService<IObjectProperties>();
    context_.UnregisterService<IAssetProvider>();
    context_.UnregisterService<ISceneProvider>();
    context_.UnregisterService<QMainWindow>();
    delete takeCentralWidget(); // The scene provider outlives its viewport.
}

void EditorWindow::RefreshScene(ObjectId preferredSelection)
{
    for (auto* label : transformGroup_->findChildren<QLabel*>())
        if (auto* axis = dynamic_cast<TransformAxisLabel*>(label)) axis->CancelDrag();
    const ObjectId wanted = preferredSelection != InvalidObjectId ? preferredSelection : selectedObject_;
    std::map<ObjectId, bool> expanded;
    for (QTreeWidgetItemIterator it(sceneTree_); *it; ++it)
        expanded[(*it)->data(0, Qt::UserRole).toULongLong()] = (*it)->isExpanded();
    if (!beforeSearchExpansion_.empty()) expanded = beforeSearchExpansion_;
    const QSignalBlocker blocker(sceneTree_);
    sceneTree_->clear();
    const auto* session = context_.GetService<EditorSession>();
    auto* root = new QTreeWidgetItem(sceneTree_, {session ? QString::fromStdString(session->sceneName) : "Untitled Scene"});
    root->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
    root->setExpanded(true);
    QTreeWidgetItem* selection = root;
    const auto append = [this, wanted, &selection, &expanded](auto&& self, ObjectId id, QTreeWidgetItem* parent) -> void {
        std::string name;
        std::vector<ObjectId> children;
        try {
            name = scene_.GetName(id);
            children = scene_.GetChildren(id);
        } catch (const std::out_of_range&) {
            return; // An object from the snapshot was removed by the backend.
        }
        auto* item = new QTreeWidgetItem(parent, {QString::fromStdString(name)});
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(id));
        item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        item->setExpanded(expanded.count(id) ? expanded[id] : true);
        if (id == wanted)
            selection = item;
        for (ObjectId child : children)
            self(self, child, item);
    };
    for (ObjectId id : scene_.GetRootObjects())
        append(append, id, root);
    sceneTree_->setCurrentItem(selection);
    FilterHierarchy();
    selectedObject_ = selection->data(0, Qt::UserRole).toULongLong();
    if (operations_.Selection() != selectedObject_) operations_.Select(selectedObject_);
    UpdateInspector();
    viewport_->update();
}

void EditorWindow::RefreshAssets()
{
    const auto* current = assetsTree_->currentItem();
    const bool hadSelection = current != nullptr;
    const AssetId selected = current ? current->data(0, Qt::UserRole).toULongLong() : 0;
    assets_.Refresh();
    const auto assets = assets_.GetAssets();
    const QSignalBlocker blocker(assetsTree_);
    assetsTree_->clear();
    for (const auto& asset : assets) {
        auto* item = new QTreeWidgetItem(assetsTree_, {QString::fromStdString(asset.name),
            QString::fromStdString(asset.type), QString::fromStdString(asset.sourcePath)});
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(asset.id));
        item->setToolTip(2, QString::fromStdString(asset.sourcePath));
        if (hadSelection && asset.id == selected)
            assetsTree_->setCurrentItem(item);
    }
    assetsStatus_->setText(assets.empty() ? "No assets" : QString("%1 assets").arg(assets.size()));
    const auto previousType = assetFilter_->currentData().toString();
    const QSignalBlocker filterBlocker(assetFilter_);
    assetFilter_->clear(); assetFilter_->addItem("All types", "");
    std::set<std::string> types;
    for (const auto& asset : assets) types.insert(asset.type);
    for (const auto& type : types) assetFilter_->addItem(QString::fromStdString(type), QString::fromStdString(type));
    const int index = assetFilter_->findData(previousType); assetFilter_->setCurrentIndex(index < 0 ? 0 : index);
    FilterAssetItems(*assetsTree_, assetSearch_->text(), assetFilter_->currentData().toString());
    propertySections_->Refresh();
}

void EditorWindow::closeEvent(QCloseEvent* event)
{
    if (QWidget* focused = QApplication::focusWidget()) focused->clearFocus(); // Finish native numeric/name editing.
    if (auto* view = dynamic_cast<IEditorViewport*>(viewport_)) view->CancelInteraction();
    if (!documents_.MayClose() || (canClose && !canClose())) { event->ignore(); return; }
    SaveLayout();
    QMainWindow::closeEvent(event);
}

void EditorWindow::ApplyDefaultDockSizes()
{
    resizeDocks({sceneDock_, inspectorDock_}, {250, 330}, Qt::Horizontal);
    resizeDocks({assetsDock_}, {qRound(height() * .22)}, Qt::Vertical);
}

void EditorWindow::UpdateBottomDockTitles()
{
    for (auto* dock : findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!dock) continue;
        if (!dock->isFloating() && dockWidgetArea(dock) == Qt::BottomDockWidgetArea
            && !tabifiedDockWidgets(dock).isEmpty()) {
            if (!dock->titleBarWidget()) {
                auto* title = new QWidget(dock);
                title->setFixedHeight(0); // The dock tab already provides the title/drag handle.
                title->setProperty("editorTabTitle", true);
                dock->setTitleBarWidget(title);
            }
        } else if (auto* title = dock->titleBarWidget(); title && title->property("editorTabTitle").toBool()) {
            dock->setTitleBarWidget(nullptr);
            title->deleteLater(); // Floating or standalone docks retain their normal title bar.
        }
    }
}

void EditorWindow::CreateViewport()
{
    if (!viewport_)
        viewport_ = new QWidget(this);
    viewport_->setObjectName("Viewport");
    viewport_->setMinimumSize(320, 240);
    editorViews_ = new QTabWidget(this);
    editorViews_->setObjectName("EditorViews");
    editorViews_->setDocumentMode(true);
    auto* sceneView = new QWidget(editorViews_);
    sceneView->setObjectName("SceneView");
    auto* sceneLayout = new QVBoxLayout(sceneView);
    sceneLayout->setContentsMargins(0, 0, 0, 0);
    sceneLayout->setSpacing(0);
    sceneToolbar_ = new QToolBar(sceneView);
    sceneToolbar_->setObjectName("SceneViewToolbar");
    sceneToolbar_->setIconSize(QSize(16, 16));
    sceneToolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    sceneToolbar_->setMovable(false);
    sceneLayout->addWidget(sceneToolbar_);
    sceneLayout->addWidget(viewport_, 1);
    editorViews_->addTab(sceneView, "Scene");

    auto* game = new QWidget(editorViews_);
    game->setObjectName("GameView");
    auto* gameLayout = new QVBoxLayout(game);
    gameLayout->addStretch();
    auto* gameTitle = new QLabel("Runtime unavailable", game);
    gameTitle->setObjectName("GamePreviewTitle");
    gameTitle->setAlignment(Qt::AlignCenter);
    gameLayout->addWidget(gameTitle);
    auto* gameDescription = new QLabel("Axl edits documents. Game execution is not implemented yet.", game);
    gameDescription->setAlignment(Qt::AlignCenter);
    gameDescription->setProperty("muted", true);
    gameLayout->addWidget(gameDescription);
    gameLayout->addStretch();
    editorViews_->addTab(game, "Game");
    setCentralWidget(editorViews_);
    if (auto* viewport = dynamic_cast<IEditorViewport*>(viewport_)) {
        viewport->BindOperations(&operations_);
        connect(&viewport->Events(), &EditorViewportEvents::ObjectPicked, this, [this](ObjectId id) { operations_.Select(id); });
        connect(&viewport->Events(), &EditorViewportEvents::TransformEdited, this, [this] { UpdateInspector(); });
        connect(&viewport->Events(), &EditorViewportEvents::RenderError, this, [this](const QString& message) {
            if (auto* console = context_.GetService<IEditorLog>())
                console->Log(ConsoleMessageType::Error, message);
        });
    }
}

void EditorWindow::CreatePanels()
{
    auto* tree = new QTreeWidget;
    sceneTree_ = tree;
    tree->setObjectName("SceneTree");
    tree->setHeaderHidden(true);
    tree->setAlternatingRowColors(true);
    tree->setIndentation(18);
    sceneDock_ = new QDockWidget("Hierarchy", this);
    auto* hierarchy = new QWidget(sceneDock_);
    auto* hierarchyLayout = new QVBoxLayout(hierarchy);
    hierarchyLayout->setContentsMargins(8, 8, 8, 8);
    hierarchySearch_ = new QLineEdit(hierarchy);
    hierarchySearch_->setObjectName("HierarchySearch"); hierarchySearch_->setPlaceholderText("Search objects");
    hierarchySearch_->setClearButtonEnabled(true);
    hierarchyLayout->addWidget(hierarchySearch_); hierarchyLayout->addWidget(tree);
    connect(hierarchySearch_, &QLineEdit::textChanged, this, [this] { FilterHierarchy(); });
    sceneDock_->setWidget(hierarchy);
    context_.RegisterDock("SceneDock", sceneDock_, Qt::LeftDockWidgetArea);

    auto* inspector = new QWidget;
    inspector->setObjectName("InspectorContent");
    auto* inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(12, 12, 12, 12);
    inspectorLayout->setSpacing(14);
    noSelection_ = new QLabel("No object selected", inspector);
    noSelection_->setWordWrap(true);
    inspectorLayout->addWidget(noSelection_);
    objectName_ = new QLineEdit(inspector);
    objectName_->setObjectName("ObjectName");
    objectName_->setToolTip("Object name");
    inspectorLayout->addWidget(objectName_);
    connect(objectName_, &QLineEdit::editingFinished, this, [this] {
        if (selectedObject_ == InvalidObjectId)
            return;
        try {
            const auto name = objectName_->text().toStdString();
            if (name == scene_.GetName(selectedObject_))
                return;
            operations_.Rename(selectedObject_, name);
        } catch (const std::out_of_range&) {
            // The runtime may have deleted the selection since the last refresh.
        }
        RefreshScene();
    });
    transformGroup_ = new QGroupBox("Transform", inspector);
    transformGroup_->setProperty("componentCard", true);
    auto* fields = new QGridLayout(transformGroup_);
    fields->setContentsMargins(10, 10, 10, 12);
    fields->setHorizontalSpacing(5);
    fields->setVerticalSpacing(8);
    const QString rows[] = {"Position", "Rotation", "Scale"};
    const QString axes[] = {"X", "Y", "Z"};
    for (int axis = 0; axis < 3; ++axis) fields->setColumnStretch(axis + 1, 1);
    for (int row = 0; row < 3; ++row) {
        fields->addWidget(new QLabel(rows[row], transformGroup_), row, 0);
        for (int axis = 0; axis < 3; ++axis) {
            auto* input = new QDoubleSpinBox(transformGroup_);
            input->setObjectName(rows[row] + axes[axis]);
            input->setProperty("transformField", true);
            input->setDecimals(3);
            input->setRange(row == 0 ? -10000.0 : -1000.0, row == 0 ? 10000.0 : 1000.0);
            input->setSingleStep(row == 1 ? 1.0 : 0.1);
            // The theme collapses arrow subcontrols; native numeric editing stays intact.
            input->setMinimumWidth(0);
            input->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            input->setToolTip(rows[row] + " " + axes[axis] + (row == 1 ? " (degrees)" : ""));
            auto* cell = new QHBoxLayout;
            cell->setSpacing(3);
            auto* label = new TransformAxisLabel(axes[axis], *input, transformGroup_, operations_);
            label->setObjectName(rows[row] + axes[axis] + "Label");
            cell->addWidget(label);
            cell->addWidget(input, 1);
            fields->addLayout(cell, row, axis + 1);
            transformEditors_[row][axis] = input;
            connect(input, &QDoubleSpinBox::valueChanged, this, [this, row, axis](double value) {
                if (selectedObject_ == InvalidObjectId)
                    return;
                try {
                    auto transform = scene_.GetTransform(selectedObject_);
                    if (!transform) { UpdateInspector(); return; }
                    Vec3* vectors[] = {&transform->position, &transform->rotation, &transform->scale};
                    (*vectors[row])[axis] = static_cast<float>(value);
                    operations_.SetTransform(selectedObject_, *transform);
                    viewport_->update();
                } catch (const std::out_of_range&) {
                    RefreshScene();
                }
            });
        }
    }
    inspectorLayout->addWidget(transformGroup_);
    propertySections_ = new InspectorProperties(properties_, inspector, &operations_);
    inspectorLayout->addWidget(propertySections_);
    context_.RegisterService<InspectorProperties>(*propertySections_);
    inspectorLayout->addStretch();
    auto* inspectorScroll = new QScrollArea;
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setFrameShape(QFrame::NoFrame);
    inspectorScroll->setWidget(inspector);
    inspectorDock_ = new QDockWidget("Inspector", this);
    inspectorDock_->setWidget(inspectorScroll);
    context_.RegisterDock("InspectorDock", inspectorDock_, Qt::RightDockWidgetArea);

    auto* assets = new QWidget;
    auto* assetsLayout = new QVBoxLayout(assets);
    assetsLayout->setContentsMargins(10, 8, 10, 10);
    assetsLayout->setSpacing(8);
    auto* assetsHeader = new QHBoxLayout;
    assetsStatus_ = new QLabel(assets);
    assetsStatus_->setObjectName("AssetsStatus");
    assetsHeader->addWidget(assetsStatus_);
    assetSearch_ = new QLineEdit(assets); assetSearch_->setObjectName("AssetsSearch"); assetSearch_->setPlaceholderText("Search assets");
    assetSearch_->setClearButtonEnabled(true); assetsHeader->addWidget(assetSearch_, 1);
    assetFilter_ = new QComboBox(assets); assetFilter_->setObjectName("AssetsTypeFilter"); assetsHeader->addWidget(assetFilter_);
    const auto filterAssets = [this] {
        if (assetsTree_) FilterAssetItems(*assetsTree_, assetSearch_->text(), assetFilter_->currentData().toString());
    };
    connect(assetSearch_, &QLineEdit::textChanged, this, filterAssets);
    connect(assetFilter_, &QComboBox::currentIndexChanged, this, filterAssets);
    assetsHeader->addStretch();
    auto* refreshAssets = new QPushButton("Refresh", assets);
    refreshAssets->setObjectName("RefreshAssetsButton");
    connect(refreshAssets, &QPushButton::clicked, this, [this] { RefreshAssets(); });
    assetsHeader->addWidget(refreshAssets);
    assetsLayout->addLayout(assetsHeader);
    assetsTree_ = new QTreeWidget(assets);
    assetsTree_->setObjectName("AssetsTree");
    assetsTree_->setHeaderLabels({"Name", "Type", "Source Path"});
    assetsTree_->setRootIsDecorated(false);
    assetsTree_->setAlternatingRowColors(true);
    assetsTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    assetsTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    assetsTree_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    assetsLayout->addWidget(assetsTree_);
    connect(assetsTree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        const auto asset = assets_.GetAsset(item->data(0, Qt::UserRole).toULongLong());
        if (asset && assetHandlers_.Open(*asset)) return;
        if (auto* console = context_.GetService<IEditorLog>()) {
            if (asset)
                console->Log(ConsoleMessageType::Info, QString("Opened asset: %1 [%2] - %3")
                    .arg(QString::fromStdString(asset->name), QString::fromStdString(asset->type),
                        QString::fromStdString(asset->sourcePath)));
            else
                console->Log(ConsoleMessageType::Warning, "Asset is no longer available. Refresh Assets to update the list.");
            console->Show();
        }
    });
    assetsDock_ = new QDockWidget("Assets", this);
    assetsDock_->setWidget(assets);
    context_.RegisterDock("AssetsDock", assetsDock_, Qt::BottomDockWidgetArea);
    RefreshAssets();

    connect(tree, &QTreeWidget::itemSelectionChanged, this, [this, tree] {
        const auto items = tree->selectedItems();
        operations_.Select(items.isEmpty() ? InvalidObjectId : items.front()->data(0, Qt::UserRole).toULongLong());
    });
    RefreshScene();
    auto* root = tree->topLevelItem(0);
    if (root->childCount() > 0)
        tree->setCurrentItem(root->child(0));
}

void EditorWindow::UpdateInspector()
{
    std::string name;
    std::optional<Transform> transform;
    if (selectedObject_ != InvalidObjectId) {
        try {
            name = scene_.GetName(selectedObject_);
            transform = scene_.GetTransform(selectedObject_);
        } catch (const std::out_of_range&) {
            selectedObject_ = InvalidObjectId;
            RefreshScene(); // Keep Hierarchy, Inspector and viewport selection in sync.
            return;
        }
    }
    const bool selected = selectedObject_ != InvalidObjectId;
    if (auto* viewport = dynamic_cast<IEditorViewport*>(viewport_))
        viewport->SetSelectedObject(selectedObject_);
    noSelection_->setVisible(!selected);
    objectName_->setVisible(selected);
    transformGroup_->setVisible(selected && transform.has_value());
    propertySections_->SetObject(selectedObject_);
    if (deleteObject_)
        deleteObject_->setEnabled(selected);
    if (createChild_)
        createChild_->setEnabled(selected);
    if (!selected)
        return;
    const QSignalBlocker nameBlocker(objectName_);
    objectName_->setText(QString::fromStdString(name));
    if (!transform)
        return;
    const Vec3 values[] = {transform->position, transform->rotation, transform->scale};
    for (int row = 0; row < 3; ++row)
        for (int axis = 0; axis < 3; ++axis) {
            const QSignalBlocker blocker(transformEditors_[row][axis]);
            transformEditors_[row][axis]->setValue(values[row][axis]);
        }
}

void EditorWindow::CreateActions()
{
    documents_.Configure(*fileMenu_, *editMenu_);
    fileMenu_->addSeparator();
    auto* exit = fileMenu_->addAction("Exit");
    exit->setShortcut(QKeySequence::Quit);
    connect(exit, &QAction::triggered, this, &QWidget::close);

    editMenu_->addSeparator();
    auto* create = editMenu_->addAction("Create Object");
    create->setObjectName("CreateObjectAction");
    create->setShortcut(QKeySequence("Ctrl+Shift+N"));
    connect(create, &QAction::triggered, this, [this] {
        operations_.Create("Object");
    });
    createChild_ = editMenu_->addAction("Create Child");
    createChild_->setObjectName("CreateChildAction");
    connect(createChild_, &QAction::triggered, this, [this] {
        if (selectedObject_ == InvalidObjectId)
            return;
        try {
            operations_.Create("Object", selectedObject_);
        } catch (const std::out_of_range&) {
            RefreshScene();
        }
    });
    deleteObject_ = editMenu_->addAction("Delete Object");
    deleteObject_->setObjectName("DeleteObjectAction");
    deleteObject_->setShortcut(QKeySequence::Delete);
    deleteObject_->setShortcutContext(Qt::WidgetShortcut);
    connect(deleteObject_, &QAction::triggered, this, [this] {
        if (selectedObject_ == InvalidObjectId)
            return;
        operations_.Delete(selectedObject_);
        RefreshScene();
    });
    sceneTree_->setContextMenuPolicy(Qt::ActionsContextMenu);
    sceneTree_->addActions({create, createChild_, deleteObject_});
    viewport_->addAction(deleteObject_);
    auto* rename = new QAction("Rename", this); rename->setObjectName("RenameObjectAction");
    rename->setShortcut(Qt::Key_F2); rename->setShortcutContext(Qt::WidgetShortcut);
    sceneTree_->addAction(rename);
    connect(rename, &QAction::triggered, this, [this] {
        if (selectedObject_) { objectName_->setFocus(); objectName_->selectAll(); }
    });
    auto* frame = new QAction("Frame Selected", this); frame->setObjectName("FrameSelectedAction");
    frame->setShortcut(Qt::Key_F); frame->setShortcutContext(Qt::WidgetShortcut);
    sceneTree_->addAction(frame); viewport_->addAction(frame);
    connect(frame, &QAction::triggered, this, [this] {
        if (auto* view = dynamic_cast<IEditorViewport*>(viewport_)) view->FrameSelected();
    });
    UpdateInspector();

    toolbar_ = addToolBar("Playback");
    toolbar_->setObjectName("PlaybackToolbar");
    toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar_->setIconSize(QSize(16, 16));
    toolbar_->setMovable(false);
    auto* leftSpace = new QWidget(toolbar_);
    leftSpace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(leftSpace);
    const auto playbackIcon = [this](QStyle::StandardPixmap shape) {
        auto pixmap = style()->standardIcon(shape).pixmap(16, 16);
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), DarkTheme().icon);
        painter.end();
        return QIcon(pixmap);
    };
    play_ = toolbar_->addAction(playbackIcon(QStyle::SP_MediaPlay), "Play");
    play_->setObjectName("PlayAction");
    play_->setEnabled(false);
    play_->setToolTip("Runtime unavailable: game execution is not implemented.");
    stop_ = toolbar_->addAction(playbackIcon(QStyle::SP_MediaStop), "Stop");
    stop_->setObjectName("StopAction");
    stop_->setEnabled(false);
    auto* rightSpace = new QWidget(toolbar_);
    rightSpace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(rightSpace);

    auto* transformTools = new QActionGroup(this);
    transformTools->setExclusive(true);
    const QString toolNames[] = {"Move", "Rotate", "Scale"};
    const TransformTool tools[] = {TransformTool::Move, TransformTool::Rotate, TransformTool::Scale};
    auto* editorViewport = dynamic_cast<IEditorViewport*>(viewport_);
    std::array<QAction*, 3> toolActions;
    for (int index = 0; index < 3; ++index) {
        auto* action = sceneToolbar_->addAction(toolNames[index]);
        toolActions[index] = action;
        action->setObjectName(toolNames[index] + "ToolAction");
        action->setCheckable(true);
        action->setChecked(tools[index] == (editorViewport ? editorViewport->ActiveTransformTool() : TransformTool::Move));
        action->setEnabled(editorViewport && editorViewport->SupportsTransformTools());
        transformTools->addAction(action);
        if (editorViewport && editorViewport->SupportsTransformTools())
            connect(action, &QAction::triggered, viewport_, [editorViewport, tool = tools[index]] {
                editorViewport->SetTransformTool(tool);
            });
    }
    if (editorViewport)
        connect(&editorViewport->Events(), &EditorViewportEvents::TransformToolChanged, this,
            [toolActions](TransformTool tool) {
                const int index = static_cast<int>(tool);
                if (index >= 0 && index < 3) toolActions[index]->setChecked(true);
            });
    sceneToolbar_->addSeparator();
    auto* cameraModes = new QActionGroup(this);
    cameraModes->setExclusive(true);
    auto* mode2D = sceneToolbar_->addAction("2D");
    mode2D->setObjectName("Mode2DAction");
    auto* mode3D = sceneToolbar_->addAction("3D");
    mode3D->setObjectName("Mode3DAction");
    for (auto* mode : {mode2D, mode3D}) {
        mode->setCheckable(true);
        cameraModes->addAction(mode);
    }
    if (auto* viewport = dynamic_cast<IEditorViewport*>(viewport_)) {
        mode2D->setChecked(viewport->Is2DMode());
        mode3D->setChecked(!viewport->Is2DMode());
        connect(mode2D, &QAction::triggered, viewport_, [viewport] { viewport->Set2DMode(true); });
        connect(mode3D, &QAction::triggered, viewport_, [viewport] { viewport->Set2DMode(false); });
        connect(&viewport->Events(), &EditorViewportEvents::CameraModeChanged, this, [mode2D, mode3D](bool twoD) {
            mode2D->setChecked(twoD);
            mode3D->setChecked(!twoD);
        });
    } else {
        mode2D->setEnabled(false);
        mode3D->setEnabled(false);
    }

    sceneToolbar_->addSeparator();
    auto* cameraButton = new QToolButton(sceneToolbar_);
    cameraButton->setObjectName("ViewportCameraButton");
    cameraButton->setText("Camera");
    cameraButton->setPopupMode(QToolButton::InstantPopup);
    auto* cameraMenu = new QMenu(cameraButton);
    auto* cameraSettings = new QWidget(cameraMenu);
    auto* cameraLayout = new QHBoxLayout(cameraSettings);
    cameraLayout->setContentsMargins(12, 10, 12, 10);
    cameraLayout->addWidget(new QLabel("Movement speed", cameraSettings));
    auto* speed = new QDoubleSpinBox(cameraSettings);
    speed->setObjectName("NavigationSpeed");
    speed->setRange(.1, 100);
    speed->setDecimals(1);
    speed->setFixedWidth(70);
    speed->setSingleStep(.5);
    cameraLayout->addWidget(speed);
    auto* settingsAction = new QWidgetAction(cameraMenu);
    settingsAction->setDefaultWidget(cameraSettings);
    cameraMenu->addAction(settingsAction);
    cameraButton->setMenu(cameraMenu);
    if (auto* viewport = dynamic_cast<IEditorViewport*>(viewport_)) {
        speed->setValue(viewport->NavigationSpeed());
        connect(speed, &QDoubleSpinBox::valueChanged, viewport_, [viewport](double value) {
            viewport->SetNavigationSpeed(float(value));
        });
        connect(&viewport->Events(), &EditorViewportEvents::NavigationSpeedChanged, speed, [speed](float value) {
            const QSignalBlocker blocker(speed);
            speed->setValue(value);
        });
    } else cameraButton->setEnabled(false);
    sceneToolbar_->addWidget(cameraButton);
    auto* sceneSpace = new QWidget(sceneToolbar_);
    sceneSpace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sceneToolbar_->addWidget(sceneSpace);
    auto* help = new QToolButton(sceneToolbar_);
    help->setObjectName("ViewportHelpButton");
    help->setText("?");
    const QString controls = "<b>Scene tools</b><br>Move: drag an axis arrow.<br>"
        "Rotate: drag a colored ring (local Euler axis).<br>"
        "Scale: drag an axis square; center square scales uniformly.<br>"
        "2D: Move/Scale X/Y, Rotate Z.<br><br>"
        "<b>3D</b><br>RMB: look &nbsp; WASD: move<br>Wheel: movement speed<br><br>"
        "<b>2D / XY</b><br>RMB or WASD: pan<br>Wheel: zoom";
    help->setToolTip(controls);
    connect(help, &QToolButton::clicked, help, [help, controls] {
        QToolTip::showText(help->mapToGlobal(QPoint(0, help->height())), controls, help);
    });
    sceneToolbar_->addWidget(help);

    viewMenu_->addSeparator();
    viewMenu_->addAction(toolbar_->toggleViewAction());
    auto* refresh = viewMenu_->addAction("Refresh Scene");
    refresh->setObjectName("RefreshSceneAction");
    refresh->setShortcut(QKeySequence("F5"));
    connect(refresh, &QAction::triggered, this, [this] { RefreshScene(); });
    auto* refreshAssets = viewMenu_->addAction("Refresh Assets");
    refreshAssets->setObjectName("RefreshAssetsAction");
    connect(refreshAssets, &QAction::triggered, this, [this] { RefreshAssets(); });
    auto* save = viewMenu_->addAction("Save Layout");
    save->setObjectName("SaveLayoutAction");
    connect(save, &QAction::triggered, this, [this] {
        SaveLayout();
        const bool saved = settings_.status() == QSettings::NoError;
        const QString message = saved ? "Layout saved." : "Could not save layout.";
        statusBar()->showMessage(message, 3000);
        if (auto* console = context_.GetService<IEditorLog>())
            console->Log(saved ? ConsoleMessageType::Success : ConsoleMessageType::Error, message);
    });
    auto* reset = viewMenu_->addAction("Reset Layout");
    reset->setObjectName("ResetLayoutAction");
    connect(reset, &QAction::triggered, this, [this] {
        restoreState(defaultLayout_, LayoutVersion);
        ApplyDefaultDockSizes();
        UpdateBottomDockTitles();
        assetsDock_->raise();
        statusBar()->showMessage("Layout reset", 3000);
    });

    auto* about = helpMenu_->addAction("About Axl Editor");
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(this, "Axl Editor", QString("Axl Editor %1\nQt %2")
            .arg(QCoreApplication::applicationVersion(), qVersion()));
    });
}

void EditorWindow::FilterHierarchy()
{
    if (!hierarchySearch_ || !sceneTree_) return;
    const auto text = hierarchySearch_->text();
    if (!text.isEmpty() && beforeSearchExpansion_.empty()) {
        for (QTreeWidgetItemIterator it(sceneTree_); *it; ++it)
            beforeSearchExpansion_[(*it)->data(0, Qt::UserRole).toULongLong()] = (*it)->isExpanded();
    } else if (text.isEmpty() && !beforeSearchExpansion_.empty()) {
        for (QTreeWidgetItemIterator it(sceneTree_); *it; ++it) {
            const auto id = (*it)->data(0, Qt::UserRole).toULongLong();
            if (beforeSearchExpansion_.count(id)) (*it)->setExpanded(beforeSearchExpansion_[id]);
        }
        beforeSearchExpansion_.clear();
    }
    const auto filter = [&](auto&& self, QTreeWidgetItem* item) -> bool {
        bool visible = item->text(0).contains(text, Qt::CaseInsensitive);
        bool childMatch = false;
        for (int i = 0; i < item->childCount(); ++i) childMatch = self(self, item->child(i)) || childMatch;
        visible = visible || childMatch;
        if (!text.isEmpty() && childMatch) item->setExpanded(true); // Reveal a match under a collapsed ancestor.
        item->setHidden(!visible && item->data(0, Qt::UserRole).toULongLong() != 0);
        return visible;
    };
    for (int i = 0; i < sceneTree_->topLevelItemCount(); ++i) filter(filter, sceneTree_->topLevelItem(i));
}

void EditorWindow::SaveLayout()
{
    settings_.setValue("window/geometry", saveGeometry());
    settings_.setValue("window/layout", saveState(LayoutVersion));
    settings_.sync();
}

bool EditorWindow::RestoreLayout()
{
    const auto geometry = settings_.value("window/geometry").toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    const auto state = settings_.value("window/layout").toByteArray();
    if (!state.isEmpty() && restoreState(state, LayoutVersion))
        return true;
    restoreState(defaultLayout_, LayoutVersion);
    return false;
}
