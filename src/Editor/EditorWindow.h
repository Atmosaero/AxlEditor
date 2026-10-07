#pragma once

#include "Editor/EditorContext.h"
#include "Editor/IEditorModule.h"
#include "Contracts/ISceneProvider.h"
#include "Contracts/IAssetProvider.h"
#include "Contracts/IObjectProperties.h"
#include <QMainWindow>
#include <QSettings>
#include <array>
#include <memory>
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
// Owns the supplied modules; they initialize before layout restore and shut down
// in reverse order while EditorContext and the window's Qt objects remain alive.
class EditorWindow final : public QMainWindow
{
public:
    using ModuleList = std::vector<std::unique_ptr<IEditorModule>>;
    explicit EditorWindow(ISceneProvider& scene, IAssetProvider& assets, QWidget* viewport = nullptr,
        IObjectProperties* properties = nullptr, ModuleList modules = {});
    ~EditorWindow() override;
    InspectorProperties& PropertySections() const { return *propertySections_; }

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
    void CreateActions();
    void SetPlaying(bool playing);
    void SaveLayout();
    bool RestoreLayout();

    ISceneProvider& scene_;
    IAssetProvider& assets_;
    IObjectProperties* properties_;
    QMenu* fileMenu_;
    QMenu* editMenu_;
    QMenu* viewMenu_;
    QMenu* modulesMenu_;
    QMenu* helpMenu_;
    EditorContext context_;
    ModuleList modules_;
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
    QTreeWidget* assetsTree_ = nullptr;
    QLabel* assetsStatus_ = nullptr;
    QAction* createChild_ = nullptr;
    QAction* deleteObject_ = nullptr;
    QLabel* noSelection_ = nullptr;
    QLineEdit* objectName_ = nullptr;
    QGroupBox* transformGroup_ = nullptr;
    InspectorProperties* propertySections_ = nullptr;
    std::array<std::array<QDoubleSpinBox*, 3>, 3> transformEditors_{};
};
