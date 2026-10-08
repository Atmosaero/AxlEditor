#include "Editor/Qt/EditorWindow.h"
#include "Editor/Qt/EditorTheme.h"
#include "Tools/Console/EditorConsole.h"
#include "Tools/Console/ConsoleTool.h"
#include "Tools/ScriptCanvas/ScriptCanvasTool.h"
#include "Tools/ScriptCanvas/ScriptCanvasView.h"
#include "Editor/Qt/IEditorViewport.h"
#include "Editor/Qt/InspectorProperties.h"
#include "Reference/Scene/ReferenceSceneProvider.h"
#include "Reference/Qt/CommentSection.h"
#include <QAction>
#include <QAbstractButton>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QToolButton>
#include <QTreeWidget>
#include <QFile>
#include <QPushButton>
#include <QSignalBlocker>
#include <QtTest>
#include <map>
#include <stdexcept>
#include <type_traits>

static_assert(std::is_standard_layout_v<Transform> && std::is_trivially_copyable_v<Transform>);

namespace {
EditorWindow::ToolList TestTools(bool withCanvas = true)
{
    EditorWindow::ToolList tools;
    tools.push_back(std::make_unique<ConsoleTool>());
    if (withCanvas) tools.push_back(std::make_unique<ScriptCanvasTool>());
    return tools;
}

struct ToolTrace { QStringList calls; bool contextAlive = true; int destroyed = 0; };

class TrackedTool final : public IEditorTool
{
public:
    TrackedTool(ToolTrace& trace, QString name, bool fail = false)
        : trace_(trace), name_(std::move(name)), fail_(fail) {}
    ~TrackedTool() override { ++trace_.destroyed; }
    void Initialize(EditorContext& context) override {
        context_ = &context;
        trace_.calls << "Initialize " + name_;
        trace_.contextAlive &= context.GetService<ISceneProvider>() != nullptr;
        dock_ = new QDockWidget(name_);
        if (!context.RegisterDock(name_ + "Dock", dock_, Qt::BottomDockWidgetArea))
            throw std::runtime_error("Could not register test dock");
        if (fail_) throw std::runtime_error("Test initialization failure");
    }
    void Shutdown() override {
        if (!context_) return;
        trace_.calls << "Shutdown " + name_;
        auto* window = context_->GetService<QMainWindow>();
        trace_.contextAlive &= window && context_->GetService<ISceneProvider>()
            && window->findChild<QDockWidget*>("SceneDock");
        delete dock_.data();
        context_ = nullptr;
    }
private:
    ToolTrace& trace_;
    QString name_;
    bool fail_;
    EditorContext* context_ = nullptr;
    QPointer<QDockWidget> dock_;
};

// Deliberately independent of Entity/Comment and the reference OpenGL widget.
class ForeignScene final : public ISceneProvider, public IObjectProperties
{
public:
    struct Object { std::string name; std::optional<Transform> transform; };
    std::map<ObjectId, Object> objects;
    ObjectId next = ObjectId{1} << 40;
    int writes = 0;
    std::vector<ObjectId> expiredSnapshot;
    std::vector<PropertyGroupInfo> groups{{1, "ForeignMetadata", "Metadata"}};
    std::vector<ObjectId> GetRootObjects() const override {
        std::vector<ObjectId> result;
        for (const auto& [id, object] : objects) result.push_back(id);
        result.insert(result.end(), expiredSnapshot.begin(), expiredSnapshot.end());
        return result;
    }
    ObjectId GetParent(ObjectId id) const override { (void)objects.at(id); return InvalidObjectId; }
    std::vector<ObjectId> GetChildren(ObjectId id) const override { (void)objects.at(id); return {}; }
    std::string GetName(ObjectId id) const override { return objects.at(id).name; }
    void SetName(ObjectId id, const std::string& value) override { objects.at(id).name = value; }
    std::optional<Transform> GetTransform(ObjectId id) const override { return objects.at(id).transform; }
    void SetTransform(ObjectId id, const Transform& value) override {
        auto& transform = objects.at(id).transform;
        if (!transform) throw std::logic_error("Object has no Transform");
        transform = value;
        ++writes;
    }
    ObjectId CreateObject(const std::string& name, ObjectId parent = InvalidObjectId) override {
        if (parent) throw std::invalid_argument("Flat test scene");
        const auto id = next++;
        objects.emplace(id, Object{name, std::nullopt});
        return id;
    }
    void DeleteObject(ObjectId id) override { objects.erase(id); }
    std::vector<PropertyGroupInfo> GetPropertyGroups(ObjectId id) const override {
        (void)objects.at(id);
        return groups;
    }
    bool CanAddPropertyGroup(ObjectId, const std::string&) const override { return false; }
    PropertyGroupId AddPropertyGroup(ObjectId, const std::string&) override { return 0; }
    void RemovePropertyGroup(ObjectId, PropertyGroupId) override {}
};

class EmptyAssets final : public IAssetProvider
{
public:
    std::vector<AssetInfo> GetAssets() const override { return {}; }
    std::optional<AssetInfo> GetAsset(AssetId) const override { return std::nullopt; }
    void Refresh() override {}
};

// Virtual runtime paths deliberately have no corresponding files on disk.
class MemoryAssets final : public IAssetProvider
{
public:
    std::vector<AssetInfo> assets;
    int refreshes = 0;
    std::vector<AssetInfo> GetAssets() const override { return assets; }
    std::optional<AssetInfo> GetAsset(AssetId id) const override {
        for (const auto& asset : assets) if (asset.id == id) return asset;
        return std::nullopt;
    }
    void Refresh() override { ++refreshes; }
};

class ForeignViewport final : public QWidget, public IEditorViewport
{
public:
    EditorViewportEvents events;
    ObjectId selection = InvalidObjectId;
    bool twoD = false;
    float speed = 2;
    bool transformTools = false;
    TransformTool tool = TransformTool::Move;
    EditorViewportEvents& Events() override { return events; }
    void SetSelectedObject(ObjectId id) override { selection = id; }
    void Set2DMode(bool value) override { twoD = value; emit events.CameraModeChanged(value); }
    bool Is2DMode() const override { return twoD; }
    float NavigationSpeed() const override { return speed; }
    void SetNavigationSpeed(float value) override { speed = value; emit events.NavigationSpeedChanged(value); }
    bool SupportsTransformTools() const override { return transformTools; }
    TransformTool ActiveTransformTool() const override { return tool; }
    void SetTransformTool(TransformTool value) override {
        if (!transformTools) return;
        tool = value;
        emit events.TransformToolChanged(tool);
    }
};

QGroupBox* TransformCard(EditorWindow& window) {
    for (auto* card : window.findChildren<QGroupBox*>())
        if (card->title() == "Transform") return card;
    return nullptr;
}
}

class EditorBoundaryTests final : public QObject
{
    Q_OBJECT
private slots:
    void virtualAssetsPickerFilterAndOpenHandler() {
        ForeignScene scene; MemoryAssets assets;
        assets.assets = {{101, "Virtual texture", "Texture", "runtime://images/a"}, {102, "Virtual mesh", "Mesh", "runtime://meshes/b"}};
        EditorWindow window(scene, assets);
        auto* tree = window.findChild<QTreeWidget*>("AssetsTree");
        auto* search = window.findChild<QLineEdit*>("AssetsSearch");
        search->setText("texture"); QVERIFY(!tree->topLevelItem(0)->isHidden()); QVERIFY(tree->topLevelItem(1)->isHidden());
        search->clear();
        auto* type = window.findChild<QComboBox*>("AssetsTypeFilter"); type->setCurrentIndex(type->findData("Mesh"));
        QVERIFY(tree->topLevelItem(0)->isHidden()); QVERIFY(!tree->topLevelItem(1)->isHidden());
        AssetPicker picker(assets); auto* choices = picker.findChild<QTreeWidget*>("AssetPickerTree");
        choices->setCurrentItem(choices->topLevelItem(0)); QCOMPARE(picker.Selected()->sourcePath, std::string("runtime://images/a"));
        AssetId opened = 0; QObject owner;
        QVERIFY(window.Context().GetService<AssetOpenHandlers>()->Register("Mesh", owner, [&](const AssetInfo& asset) { opened = asset.id; return true; }));
        emit tree->itemDoubleClicked(tree->topLevelItem(1), 0); QCOMPARE(opened, AssetId{102});
        window.Context().GetService<AssetOpenHandlers>()->Unregister(owner);
        QVERIFY(!window.Context().GetService<AssetOpenHandlers>()->Open(assets.assets[1]));
    }
    void hierarchySearchRenameAndExpansionSurviveRefresh() {
        ReferenceSceneProvider scene; EmptyAssets assets;
        const auto parent = scene.CreateObject("Parent"), child = scene.CreateObject("Needle", parent);
        EditorWindow window(scene, assets);
        auto* tree = window.findChild<QTreeWidget*>("SceneTree");
        auto* parentItem = tree->topLevelItem(0)->child(0);
        parentItem->setExpanded(false); window.RefreshScene(child);
        parentItem = tree->topLevelItem(0)->child(0);
        QVERIFY(!parentItem->isExpanded());
        auto* search = window.findChild<QLineEdit*>("HierarchySearch");
        search->setText("Needle"); QVERIFY(!parentItem->isHidden()); QVERIFY(!parentItem->child(0)->isHidden());
        QVERIFY(parentItem->isExpanded());
        search->setText("Missing"); QVERIFY(parentItem->isHidden());
        search->clear(); QVERIFY(!parentItem->isHidden());
        QVERIFY(!parentItem->isExpanded());
        window.findChild<QAction*>("RenameObjectAction")->trigger();
        auto* name = window.findChild<QLineEdit*>("ObjectName");
        name->setText("Renamed"); QTest::keyClick(name, Qt::Key_Return);
        QCOMPARE(scene.GetName(child), std::string("Renamed"));
    }
    void initTestCase() { ApplyDarkTheme(*qApp); }
    void init() { QFile::remove(QCoreApplication::applicationDirPath() + "/editor-layout.ini"); }
    void cleanup() { QFile::remove(QCoreApplication::applicationDirPath() + "/editor-layout.ini"); }
    void ownedToolsUseGenericDocksAndReverseShutdown()
    {
        ForeignScene scene;
        EmptyAssets assets;
        ToolTrace trace;
        QPointer<QDockWidget> first, second;
        {
            EditorWindow::ToolList tools;
            tools.push_back(std::make_unique<TrackedTool>(trace, "FirstTool"));
            tools.push_back(std::make_unique<TrackedTool>(trace, "SecondTool"));
            EditorWindow window(scene, assets, nullptr, nullptr, std::move(tools));
            first = window.findChild<QDockWidget*>("FirstToolDock");
            second = window.findChild<QDockWidget*>("SecondToolDock");
            auto* assetsDock = window.findChild<QDockWidget*>("AssetsDock");
            QVERIFY(first && second);
            QVERIFY(window.tabifiedDockWidgets(assetsDock).contains(first));
            QVERIFY(window.tabifiedDockWidgets(assetsDock).contains(second));
            QVERIFY(first->titleBarWidget() && first->titleBarWidget()->height() == 0);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            first->setFloating(true);
            QTRY_VERIFY(!first->titleBarWidget());
        }
        QVERIFY(trace.contextAlive && !first && !second);
        QCOMPARE(trace.destroyed, 2);
        QCOMPARE(trace.calls, QStringList({"Initialize FirstTool", "Initialize SecondTool",
            "Shutdown SecondTool", "Shutdown FirstTool"}));
    }

    void scriptCanvasFloatsByDefaultCanDockAndDetachAndRestoresLayout()
    {
        ForeignScene scene;
        EmptyAssets assets;
        {
            EditorWindow window(scene, assets, nullptr, nullptr, TestTools());
            auto* dock = window.findChild<QDockWidget*>("ScriptCanvasDock");
            auto* assetsDock = window.findChild<QDockWidget*>("AssetsDock");
            QVERIFY(dock->isFloating());
            QVERIFY(!window.tabifiedDockWidgets(assetsDock).contains(dock));
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            window.findChild<QAction*>("OpenScriptCanvasAction")->trigger();
            QTRY_VERIFY(dock->isVisible());
            QCOMPARE(dock->window(), static_cast<QWidget*>(dock));
            auto* view = dynamic_cast<ScriptCanvasView*>(window.findChild<QGraphicsView*>("ScriptCanvasView"));
            QVERIFY(view);
            view->EditGraph("Add node", [](auto& graph) { graph.AddNode(ScriptGraph::NodeType::Start, {20, 30}); });
            dock->activateWindow(); view->setFocus();
            QTRY_VERIFY(view->hasFocus());
            QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
            QTRY_VERIFY(view->GraphData().Nodes().empty());
            window.findChild<QAction*>("RedoAction")->trigger();
            QCOMPARE(view->GraphData().Nodes().size(), std::size_t{1});
            dock->setFloating(false);
            window.tabifyDockWidget(assetsDock, dock);
            dock->show(); dock->raise(); window.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            QTRY_VERIFY(!dock->titleBarWidget());
            auto* floatButton = dock->findChild<QAbstractButton*>("qt_dockwidget_floatbutton");
            QVERIFY(floatButton);
            QTRY_VERIFY(floatButton->isVisible());
            QTest::mouseClick(floatButton, Qt::LeftButton);
            QTRY_VERIFY(dock->isFloating());
            QCOMPARE(view->GraphData().Nodes().size(), std::size_t{1});
            window.findChild<QAction*>("SaveLayoutAction")->trigger();
        }
        {
            EditorWindow window(scene, assets, nullptr, nullptr, TestTools());
            auto* dock = window.findChild<QDockWidget*>("ScriptCanvasDock");
            QVERIFY(dock->isFloating()); // A detached window survives restart.
            dock->setFloating(false);
            window.tabifyDockWidget(window.findChild<QDockWidget*>("AssetsDock"), dock);
            window.findChild<QAction*>("SaveLayoutAction")->trigger();
        }
        {
            EditorWindow window(scene, assets, nullptr, nullptr, TestTools());
            auto* dock = window.findChild<QDockWidget*>("ScriptCanvasDock");
            QVERIFY(!dock->isFloating()); // An explicit docking choice is also restored.
            QVERIFY(!dock->titleBarWidget());
            window.findChild<QAction*>("ResetLayoutAction")->trigger();
            QVERIFY(dock->isFloating());
        }
    }

    void initializationFailureUnwindsToolsWithLiveContext()
    {
        ForeignScene scene;
        EmptyAssets assets;
        ToolTrace trace;
        EditorWindow::ToolList tools;
        tools.push_back(std::make_unique<TrackedTool>(trace, "FirstTool"));
        tools.push_back(std::make_unique<TrackedTool>(trace, "FailingTool", true));
        tools.push_back(std::make_unique<TrackedTool>(trace, "UnstartedTool"));
        QVERIFY_EXCEPTION_THROWN(EditorWindow(scene, assets, nullptr, nullptr, std::move(tools)),
            std::runtime_error);
        QVERIFY(trace.contextAlive);
        QCOMPARE(trace.destroyed, 3);
        QCOMPARE(trace.calls, QStringList({"Initialize FirstTool", "Initialize FailingTool",
            "Shutdown FailingTool", "Shutdown FirstTool"}));
    }

    void optionalToolsRestoreLayoutWithoutScriptCanvas()
    {
        ForeignScene scene;
        EmptyAssets assets;
        {
            EditorWindow window(scene, assets, nullptr, nullptr, TestTools());
            window.findChild<QDockWidget*>("ScriptCanvasDock")->setFloating(true);
            window.findChild<QDockWidget*>("InspectorDock")->setFloating(true);
            window.findChild<QAction*>("SaveLayoutAction")->trigger();
        }
        {
            EditorWindow window(scene, assets, nullptr, nullptr, TestTools(false));
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QVERIFY(!window.findChild<QDockWidget*>("ScriptCanvasDock"));
            QVERIFY(!window.findChild<QAction*>("OpenScriptCanvasAction"));
            QVERIFY(window.findChild<QDockWidget*>("InspectorDock")->isFloating());
            QVERIFY(window.findChild<QPlainTextEdit*>("ConsoleOutput"));
            QVERIFY(!window.findChild<QAction*>("PlayAction")->isEnabled());
            QVERIFY(window.findChild<QAction*>("PlayAction")->toolTip().contains("Runtime unavailable"));
            window.findChild<QAction*>("ResetLayoutAction")->trigger();
            QVERIFY(window.tabifiedDockWidgets(window.findChild<QDockWidget*>("AssetsDock"))
                .contains(window.findChild<QDockWidget*>("ConsoleDock")));
        }
        {
            EditorWindow window(scene, assets); // No tools or logging service.
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QVERIFY(!window.findChild<QDockWidget*>("ConsoleDock"));
            QVERIFY(!window.findChild<QDockWidget*>("ScriptCanvasDock"));
            window.findChild<QAction*>("PlayAction")->trigger();
            window.findChild<QAction*>("SaveLayoutAction")->trigger();
            window.findChild<QAction*>("ResetLayoutAction")->trigger();
            QCOMPARE(window.findChildren<QDockWidget*>().size(), 3);
        }
    }
    void assetPanelUsesProviderIdsAndSnapshot()
    {
        ForeignScene scene;
        MemoryAssets assets;
        const AssetId id = (AssetId{1} << 48) + 7;
        const auto name = QString::fromUtf8(u8"\u0430\u0441\u0441\u0435\u0442.lua");
        const auto path = QString("runtime://library/") + name;
        assets.assets = {{id, name.toUtf8().toStdString(), "Script", path.toUtf8().toStdString()}};
        EditorWindow window(scene, assets, new QWidget, nullptr, TestTools());
        auto* tree = window.findChild<QTreeWidget*>("AssetsTree");
        QVERIFY(tree);
        QCOMPARE(assets.refreshes, 1);
        QCOMPARE(tree->topLevelItemCount(), 1);
        auto* item = tree->topLevelItem(0);
        QCOMPARE(item->text(0), name);
        QCOMPARE(item->text(2), path);
        QCOMPARE(item->toolTip(2), path);
        QCOMPARE(item->data(0, Qt::UserRole).toULongLong(), id);
        tree->setCurrentItem(item);
        assets.assets.insert(assets.assets.begin(), {id + 1, "New", "Unknown", "runtime://new"});
        window.RefreshAssets();
        QCOMPARE(assets.refreshes, 2);
        QCOMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(tree->currentItem()->data(0, Qt::UserRole).toULongLong(), id);
        item = tree->currentItem();
        tree->itemDoubleClicked(item, 0);
        auto* console = window.findChild<QPlainTextEdit*>("ConsoleOutput");
        QVERIFY(console);
        QVERIFY(console->toPlainText().contains("Opened asset: " + name + " [Script] - " + path));

        assets.assets.erase(assets.assets.begin() + 1);
        tree->itemDoubleClicked(item, 0); // Item is stale; provider is the authority at activation.
        QVERIFY(console->toPlainText().contains("[Warning] Asset is no longer available."));
        window.RefreshAssets();
        QCOMPARE(tree->topLevelItemCount(), 1);
        QVERIFY(!tree->currentItem());
        assets.assets.clear();
        window.RefreshAssets();
        QCOMPARE(tree->topLevelItemCount(), 0);
        QCOMPARE(window.findChild<QLabel*>("AssetsStatus")->text(), QString("No assets"));
    }

    void referenceTransformContract() {
        ReferenceSceneProvider scene;
        const auto id = scene.CreateObject("Cube");
        QVERIFY(scene.GetTransform(id).has_value());
        auto transform = *scene.GetTransform(id);
        transform.position = {2, 3, 4};
        QCOMPARE(scene.GetTransform(id)->position.x, 0.f); // Snapshot, not a reference.
        scene.SetTransform(id, transform);
        QCOMPARE(scene.GetTransform(id)->position.y, 3.f);
        scene.DeleteObject(id);
        QVERIFY_EXCEPTION_THROWN(scene.GetTransform(id), std::out_of_range);
    }

    void horizontalTransformScrubRespectsSelectionAndCancellation() {
        ForeignScene scene;
        EmptyAssets assets;
        const auto first = scene.CreateObject("First");
        const auto second = scene.CreateObject("Second");
        scene.objects.at(first).transform = Transform{};
        scene.objects.at(second).transform = Transform{};
        EditorWindow window(scene, assets, nullptr, nullptr, TestTools());
        window.RefreshScene(first);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto* label = window.findChild<QLabel*>("PositionXLabel");
        auto* input = window.findChild<QDoubleSpinBox*>("PositionX");
        QVERIFY(label && input);
        auto* numericText = input->findChild<QLineEdit*>();
        QVERIFY(numericText && numericText->isVisible());
        QVERIFY(numericText->width() >= 35 && numericText->height() >= 12);
        const auto move = [label](int dx) {
            const auto point = label->rect().center() + QPoint(dx, 0);
            QMouseEvent event(QEvent::MouseMove, point, label->mapToGlobal(point),
                Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(label, &event);
        };
        QTest::mousePress(label, Qt::LeftButton, Qt::NoModifier, label->rect().center());
        move(40);
        QCOMPARE(scene.GetTransform(first)->position.x, .5f);
        QCOMPARE(scene.GetTransform(first)->position.y, 0.f);
        QCOMPARE(scene.GetTransform(first)->position.z, 0.f);
        move(-16);
        QCOMPARE(scene.GetTransform(first)->position.x, -.2f);
        QTest::keyClick(input, Qt::Key_Escape);
        const auto stopped = scene.GetTransform(first)->position.x;
        move(80);
        QCOMPARE(scene.GetTransform(first)->position.x, stopped);
        QTest::mouseRelease(label, Qt::LeftButton);

        QTest::mousePress(label, Qt::LeftButton);
        window.RefreshScene(second);
        move(80); // The reused label must not continue a previous object's drag.
        QCOMPARE(scene.GetTransform(first)->position.x, stopped);
        QCOMPARE(scene.GetTransform(second)->position.x, 0.f);
        QTest::mouseRelease(label, Qt::LeftButton);
        input->setFocus();
        input->selectAll();
        QTest::keyClicks(input, "3");
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(scene.GetTransform(second)->position.x, 3.f);
    }

    void objectWithoutTransformKeepsPropertiesAndSelection() {
        ForeignScene scene;
        EmptyAssets assets;
        const auto id = scene.CreateObject("Metadata object");
        auto* viewport = new ForeignViewport;
        EditorWindow window(scene, assets, viewport, &scene, TestTools());
        window.PropertySections().RegisterEditor({"ForeignMetadata", "Metadata",
            [](ObjectId, PropertyGroupId, QWidget* parent) { return new QLabel("External properties", parent); },
            {}});
        window.RefreshScene(id);
        QCOMPARE(viewport->selection, id);
        QVERIFY(TransformCard(window)->isHidden());
        auto* name = window.findChild<QLineEdit*>("ObjectName");
        QVERIFY(!name->isHidden());
        QCOMPARE(name->text(), QString("Metadata object"));
        bool found = false;
        for (auto* label : window.findChildren<QLabel*>())
            found |= label->text() == "External properties";
        QVERIFY(found);
        name->setText("Renamed");
        QTest::keyClick(name, Qt::Key_Return);
        QCOMPARE(QString::fromStdString(scene.GetName(id)), QString("Renamed"));
        window.findChild<QDoubleSpinBox*>("PositionX")->setValue(7);
        QCOMPARE(scene.writes, 0); // Hidden stale field cannot create a Transform.
        QVERIFY_EXCEPTION_THROWN(scene.SetTransform(id, Transform{}), std::logic_error);
        window.findChild<QAction*>("DeleteObjectAction")->trigger();
        QVERIFY(scene.objects.empty());
        QCOMPARE(viewport->selection, InvalidObjectId);
    }

    void externalViewportCameraAndEdits() {
        ForeignScene scene;
        EmptyAssets assets;
        const auto id = scene.CreateObject("Spatial object");
        scene.objects.at(id).transform = Transform{};
        auto* viewport = new ForeignViewport;
        EditorWindow window(scene, assets, viewport, nullptr, TestTools());
        QVERIFY(!window.findChild<QAction*>("RotateToolAction")->isEnabled());
        window.RefreshScene(id);
        QVERIFY(!TransformCard(window)->isHidden());
        auto* x = window.findChild<QDoubleSpinBox*>("PositionX");
        x->setValue(8);
        QCOMPARE(scene.GetTransform(id)->position.x, 8.f);
        QCOMPARE(scene.writes, 1);
        auto transform = *scene.GetTransform(id);
        transform.position.y = 12;
        scene.SetTransform(id, transform);
        emit viewport->events.TransformEdited();
        QCOMPARE(window.findChild<QDoubleSpinBox*>("PositionY")->value(), 12.);
        auto* mode2D = window.findChild<QAction*>("Mode2DAction");
        auto* mode3D = window.findChild<QAction*>("Mode3DAction");
        mode2D->trigger();
        QVERIFY(viewport->twoD && mode2D->isChecked());
        viewport->Set2DMode(false);
        QVERIFY(mode3D->isChecked());
        auto* speed = window.findChild<QDoubleSpinBox*>("NavigationSpeed");
        speed->setValue(5);
        QCOMPARE(viewport->speed, 5.f);
        viewport->SetNavigationSpeed(9);
        QCOMPARE(speed->value(), 9.);
        QCOMPARE(scene.writes, 2); // Camera changes leave the scene untouched.
        emit viewport->events.RenderError("External viewport error");
        QVERIFY(window.findChild<QPlainTextEdit*>("ConsoleOutput")->toPlainText().contains("[Error] External viewport error"));
        scene.DeleteObject(id);
        emit viewport->events.TransformEdited();
        QCOMPARE(viewport->selection, InvalidObjectId);
        QVERIFY(TransformCard(window)->isHidden());
    }

    void transformToolbarSupportsAnOptInExternalViewport() {
        ForeignScene scene;
        EmptyAssets assets;
        auto* viewport = new ForeignViewport;
        viewport->transformTools = true;
        EditorWindow window(scene, assets, viewport, nullptr, TestTools());
        auto* move = window.findChild<QAction*>("MoveToolAction");
        auto* rotate = window.findChild<QAction*>("RotateToolAction");
        auto* scale = window.findChild<QAction*>("ScaleToolAction");
        QVERIFY(move->isChecked() && rotate->isEnabled() && scale->isEnabled());
        rotate->trigger();
        QCOMPARE(viewport->tool, TransformTool::Rotate);
        QVERIFY(rotate->isChecked() && !move->isChecked());
        viewport->SetTransformTool(TransformTool::Scale);
        QVERIFY(scale->isChecked() && !rotate->isChecked());
        QCOMPARE(scene.writes, 0);
    }

    void transformCanDisappearWhileSelected() {
        ForeignScene scene;
        EmptyAssets assets;
        const auto id = scene.CreateObject("Changing object");
        scene.objects.at(id).transform = Transform{};
        auto* viewport = new ForeignViewport;
        EditorWindow window(scene, assets, viewport, nullptr, TestTools());
        window.RefreshScene(id);
        scene.objects.at(id).transform.reset();
        window.findChild<QDoubleSpinBox*>("PositionX")->setValue(3);
        QCOMPARE(viewport->selection, id);
        QCOMPARE(scene.writes, 0);
        QVERIFY(TransformCard(window)->isHidden());
        scene.objects.at(id).transform = Transform{};
        emit viewport->events.TransformEdited();
        QVERIFY(!TransformCard(window)->isHidden());
        QCOMPARE(window.findChild<QDoubleSpinBox*>("PositionX")->value(), 0.);
    }

    void plainWidgetNeedsNoViewportInterface() {
        ForeignScene scene;
        EmptyAssets assets;
        EditorWindow window(scene, assets, new QWidget, nullptr, TestTools());
        QVERIFY(!window.findChild<QAction*>("Mode2DAction")->isEnabled());
        QVERIFY(!window.findChild<QAction*>("Mode3DAction")->isEnabled());
        QVERIFY(!window.findChild<QToolButton*>("ViewportCameraButton")->isEnabled());
    }

    void deletedObjectClearsHierarchyAndStaleSnapshots() {
        ForeignScene scene;
        EmptyAssets assets;
        const auto id = scene.CreateObject("Deleted object");
        const auto survivor = scene.CreateObject("Survivor");
        auto* viewport = new ForeignViewport;
        EditorWindow window(scene, assets, viewport, nullptr, TestTools());
        window.RefreshScene(id);
        scene.DeleteObject(id);
        scene.expiredSnapshot.push_back(id); // Removed since the backend enumerated roots.
        emit viewport->events.TransformEdited();
        auto* tree = window.findChild<QTreeWidget*>("SceneTree");
        QCOMPARE(viewport->selection, InvalidObjectId);
        QCOMPARE(tree->currentItem(), tree->topLevelItem(0));
        QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->child(0)->data(0, Qt::UserRole).toULongLong(), survivor);
        QVERIFY(!window.findChild<QAction*>("DeleteObjectAction")->isEnabled());
        QVERIFY(window.findChild<QLineEdit*>("ObjectName")->isHidden());
    }

    void retiredCommentWidgetsCannotWriteToPreviousObject() {
        ReferenceSceneProvider scene;
        EmptyAssets assets;
        const auto first = scene.CreateObject("First");
        const auto second = scene.CreateObject("Second");
        const auto group = scene.AddPropertyGroup(first, "Comment");
        scene.SetCommentText(first, group, "Keep this note");
        EditorWindow window(scene, assets, nullptr, &scene, TestTools());
        RegisterCommentSection(window.PropertySections(), scene);
        window.RefreshScene(first);
        auto* oldText = window.findChild<QPlainTextEdit*>("CommentText");
        auto* oldRemove = window.findChild<QPushButton*>("RemovePropertyGroupButton");
        QVERIFY(oldText && oldRemove);
        window.RefreshScene(second); // deleteLater has not run yet.
        QVERIFY(!oldText->isEnabled());
        oldText->setPlainText("Stale write");
        oldRemove->click();
        QCOMPARE(QString::fromStdString(scene.GetCommentText(first, group)), QString("Keep this note"));
        QCOMPARE(scene.GetPropertyGroups(first).size(), std::size_t{1});
    }

    void livePropertyEditorRetainsFocusAndUndoWhenGroupsChange() {
        ForeignScene scene;
        EmptyAssets assets;
        const auto id = scene.CreateObject("Object");
        EditorWindow window(scene, assets, nullptr, &scene, TestTools());
        window.PropertySections().RegisterEditor({"ForeignMetadata", "Metadata",
            [](ObjectId, PropertyGroupId, QWidget* parent) { return new QPlainTextEdit(parent); }, {}});
        window.RefreshScene(id);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        // Find the editor inside the group, independently of the Console widget.
        auto* text = window.findChild<QGroupBox*>("PropertyGroupSection")->findChild<QPlainTextEdit*>();
        QVERIFY(text);
        text->setFocus();
        text->insertPlainText("Draft note");
        QVERIFY(text->document()->isUndoAvailable());
        scene.groups.push_back({2, "Opaque", "Other properties"});
        window.RefreshScene(id);
        QCOMPARE(window.findChild<QGroupBox*>("PropertyGroupSection")->findChild<QPlainTextEdit*>(), text);
        QVERIFY(text->hasFocus());
        QVERIFY(text->document()->isUndoAvailable());
        scene.groups.pop_back();
        window.RefreshScene(id);
        QVERIFY(text->hasFocus());
        text->undo();
        QVERIFY(text->toPlainText().isEmpty());
    }

    void disappearingGroupDoesNotHideUnrelatedProperties() {
        ForeignScene scene;
        EmptyAssets assets;
        const auto id = scene.CreateObject("Object");
        scene.groups.push_back({2, "ForeignMetadata", "Surviving properties"});
        bool removeDuringRefresh = false;
        EditorWindow window(scene, assets, nullptr, &scene, TestTools());
        window.PropertySections().RegisterEditor({"ForeignMetadata", "Metadata",
            [](ObjectId, PropertyGroupId group, QWidget* parent) {
                auto* text = new QLineEdit(parent);
                text->setObjectName(QString("ExternalGroup%1").arg(group));
                return text;
            },
            [&scene, &removeDuringRefresh](QWidget* widget, ObjectId, PropertyGroupId group) {
                if (group == 1 && removeDuringRefresh) {
                    scene.groups.erase(scene.groups.begin());
                    throw std::out_of_range("Group removed by runtime");
                }
                static_cast<QLineEdit*>(widget)->setText("Live data");
            }});
        window.RefreshScene(id);
        auto* survivor = window.findChild<QLineEdit*>("ExternalGroup2");
        removeDuringRefresh = true;
        window.PropertySections().Refresh();
        QVERIFY(!window.PropertySections().isHidden());
        QVERIFY(survivor->isEnabled());
        QCOMPARE(survivor->text(), QString("Live data"));
        QVERIFY(!window.findChild<QLineEdit*>("ExternalGroup1")->isEnabled());
        window.PropertySections().Refresh();
        QCOMPARE(window.findChild<QLineEdit*>("ExternalGroup2"), survivor);
        QCOMPARE(window.findChild<QLineEdit*>("ObjectName")->text(), QString("Object"));
    }

    void missingToolAndCorruptLayoutKeepShellUsable() {
        ForeignScene scene;
        EmptyAssets assets;
        {
            EditorWindow window(scene, assets, nullptr, nullptr, TestTools());
            auto* external = new QDockWidget("External tool", &window);
            external->setObjectName("AbsentToolDock");
            window.addDockWidget(Qt::RightDockWidgetArea, external);
            external->setFloating(true);
            window.findChild<QDockWidget*>("InspectorDock")->setFloating(true);
            window.findChild<QAction*>("SaveLayoutAction")->trigger();
        }
        {
            EditorWindow window(scene, assets, nullptr, nullptr, TestTools()); // Saved tool is no longer installed.
            QVERIFY(window.findChild<QDockWidget*>("InspectorDock")->isFloating());
            window.findChild<QAction*>("ResetLayoutAction")->trigger();
            QVERIFY(!window.findChild<QDockWidget*>("InspectorDock")->isFloating());
            QCOMPARE(window.dockWidgetArea(window.findChild<QDockWidget*>("InspectorDock")), Qt::RightDockWidgetArea);
            QVERIFY(window.tabifiedDockWidgets(window.findChild<QDockWidget*>("AssetsDock"))
                .contains(window.findChild<QDockWidget*>("ConsoleDock")));
        }
        QSettings settings(QCoreApplication::applicationDirPath() + "/editor-layout.ini", QSettings::IniFormat);
        settings.setValue("window/layout", QByteArray("Corrupt layout"));
        settings.sync();
        EditorWindow window(scene, assets, nullptr, nullptr, TestTools());
        QVERIFY(!window.findChild<QDockWidget*>("InspectorDock")->isFloating());
        QCOMPARE(window.dockWidgetArea(window.findChild<QDockWidget*>("SceneDock")), Qt::LeftDockWidgetArea);
    }

    void disappearingGroupDuringCreationKeepsOtherEditors() {
        ForeignScene scene;
        EmptyAssets assets;
        const auto id = scene.CreateObject("Object");
        scene.groups.push_back({2, "ForeignMetadata", "Live properties"});
        auto* viewport = new ForeignViewport;
        EditorWindow window(scene, assets, viewport, &scene, TestTools());
        window.PropertySections().RegisterEditor({"ForeignMetadata", "Metadata",
            [&scene](ObjectId, PropertyGroupId group, QWidget* parent) -> QWidget* {
                if (group == 1) {
                    scene.groups.erase(scene.groups.begin());
                    throw std::out_of_range("Group disappeared after enumeration");
                }
                auto* text = new QLineEdit("Live data", parent);
                text->setObjectName("SurvivingEditor");
                return text;
            }, {}});
        window.RefreshScene(id);
        QCOMPARE(viewport->selection, id);
        QVERIFY(!window.PropertySections().isHidden());
        auto* text = window.findChild<QLineEdit*>("SurvivingEditor");
        QVERIFY(text && text->isEnabled());
        window.PropertySections().Refresh();
        QCOMPARE(window.findChild<QLineEdit*>("SurvivingEditor"), text);
    }
};

QTEST_MAIN(EditorBoundaryTests)
#include "EditorBoundaryTests.moc"
