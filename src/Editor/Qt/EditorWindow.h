#pragma once

#include "Editor/Qt/EditorContext.h"
#include "Editor/Application/IEditorTool.h"
#include "Editor/Application/EditorOperations.h"
#include "Editor/Qt/ConsoleCommands.h"
#include "Editor/Qt/DocumentActions.h"
#include "Editor/Qt/AssetWidgets.h"
#include "Contracts/ISceneProvider.h"
#include "Contracts/IAssetProvider.h"
#include "Contracts/IObjectProperties.h"
#include <QMainWindow>
#include <QSettings>
#include <array>
#include <memory>
#include <map>
#include <vector>

class QAction;
class QCloseEvent;
class QDockWidget;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QMenu;
class QTabWidget;
class QToolBar;
class QTreeWidget;
class InspectorProperties;

// Providers are borrowed and must outlive the window. Qt owns the supplied viewport.
// A viewport may implement IEditorViewport to receive selection/camera controls.
// Owns the supplied tools; they initialize before layout restore and shut down
// in reverse order while EditorContext and the window's Qt objects remain alive.
class EditorWindow final : public QMainWindow
{
public:
    using ToolList = std::vector<std::unique_ptr<IEditorTool>>;
    explicit EditorWindow(ISceneProvider& scene, IAssetProvider& assets, QWidget* viewport = nullptr,
        IObjectProperties* properties = nullptr, ToolList tools = {});
    ~EditorWindow() override;
    InspectorProperties& PropertySections() const { return *propertySections_; }
    EditorOperations& Operations() { return operations_; }
    EditorContext& Context() { return context_; }
    QMenu& FileMenu() { return *fileMenu_; }
    QMenu& EditMenu() { return *editMenu_; }
    QWidget& ViewportWidget() { return *viewport_; }
    DocumentActions& Documents() { return documents_; }
    std::function<bool()> canClose;

    // Invoke on the editor thread after an external backend change.
    void RefreshScene(ObjectId preferredSelection = InvalidObjectId);
    void RefreshAssets();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    static constexpr int LayoutVersion = 2; // Stable across this structural cleanup.
    void ApplyDefaultDockSizes();
    void UpdateBottomDockTitles();
    void CreateViewport();
    void CreatePanels();
    void UpdateInspector();
    void FilterHierarchy();
    void CreateActions();
    void SaveLayout();
    bool RestoreLayout();

    ISceneProvider& scene_;
    IAssetProvider& assets_;
    IObjectProperties* properties_;
    EditorOperations operations_{scene_};
    DocumentActions documents_{*this};
    AssetOpenHandlers assetHandlers_;
    QMenu* fileMenu_;
    QMenu* editMenu_;
    QMenu* viewMenu_;
    QMenu* toolsMenu_;
    QMenu* helpMenu_;
    EditorContext context_;
    ToolList tools_;
    std::vector<ConsoleCommands::Registration> sceneCommands_;
    QSettings settings_;
    QByteArray defaultLayout_;
    QDockWidget* sceneDock_ = nullptr;
    QDockWidget* inspectorDock_ = nullptr;
    QDockWidget* assetsDock_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    QAction* play_ = nullptr;
    QAction* stop_ = nullptr;
    ObjectId selectedObject_ = InvalidObjectId;
    QWidget* viewport_ = nullptr;
    QTabWidget* editorViews_ = nullptr;
    QToolBar* sceneToolbar_ = nullptr;
    QTreeWidget* sceneTree_ = nullptr;
    QLineEdit* hierarchySearch_ = nullptr;
    std::map<ObjectId, bool> beforeSearchExpansion_;
    QTreeWidget* assetsTree_ = nullptr;
    QLineEdit* assetSearch_ = nullptr;
    QComboBox* assetFilter_ = nullptr;
    QLabel* assetsStatus_ = nullptr;
    QAction* createChild_ = nullptr;
    QAction* deleteObject_ = nullptr;
    QLabel* noSelection_ = nullptr;
    QLineEdit* objectName_ = nullptr;
    QGroupBox* transformGroup_ = nullptr;
    InspectorProperties* propertySections_ = nullptr;
    std::array<std::array<QDoubleSpinBox*, 3>, 3> transformEditors_{};
};
