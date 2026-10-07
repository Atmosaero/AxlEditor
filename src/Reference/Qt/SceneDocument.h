#pragma once
#include "Editor/Application/EditorSession.h"
#include "Editor/Qt/EditorWindow.h"
#include "Editor/Qt/IEditorViewport.h"
#include "Editor/Qt/InspectorProperties.h"
#include "Reference/Qt/ReferenceAssetProvider.h"
#include "Reference/Scene/ReferenceSceneProvider.h"
#include "Reference/Qt/SceneCodec.h"
#include "Editor/Qt/SnapshotHistory.h"
#include "Editor/Qt/IEditorLog.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QStatusBar>
#include <QTreeWidget>
#include <QScrollArea>
#include <QImageReader>

// Reference document adapter. The general shell knows neither this codec nor storage.
class SceneDocument : public QObject
{
public:
    enum class Decision { Save, Discard, Cancel };
    std::function<Decision()> decide;
    QString lastError;
    SceneDocument(EditorWindow& window, ReferenceSceneProvider& scene, ReferenceAssetProvider& assets)
        : QObject(&window), window_(window), scene_(scene), assets_(assets) {
        auto& menu = window_.FileMenu();
        auto* beforeSave = menu.actions().front();
        auto* project = new QAction("Open Project...", this);
        menu.insertAction(beforeSave, project);
        project->setObjectName("OpenProjectAction");
        connect(project, &QAction::triggered, this, [this] {
            const auto root = QFileDialog::getExistingDirectory(&window_, "Open Project");
            if (!root.isEmpty()) OpenProject(root);
        });
        auto* fresh = new QAction("New Scene", this);
        menu.insertAction(beforeSave, fresh);
        fresh->setObjectName("NewSceneAction"); fresh->setShortcut(QKeySequence::New);
        connect(fresh, &QAction::triggered, this, [this] { NewScene(); });
        auto* open = new QAction("Open Scene...", this);
        menu.insertAction(beforeSave, open);
        menu.insertSeparator(beforeSave);
        open->setObjectName("OpenSceneAction"); open->setShortcut(QKeySequence::Open);
        connect(open, &QAction::triggered, this, [this] {
            const auto path = QFileDialog::getOpenFileName(&window_, "Open Scene", QString::fromStdString(session_.projectRoot), "Axl scenes (*.axl *.json)");
            if (!path.isEmpty()) Open(path);
        });
        history_.apply = [this](const Snapshot& snapshot) {
            window_.Operations().CancelTransformGesture();
            window_.PropertySections().SetObject(0);
            scene_.Replace(snapshot.scene);
            window_.Operations().Select(snapshot.selection);
            window_.Operations().Notify(EditorOperations::Change::Structure);
            UpdateTitle();
        };
        window_.Operations().beforeEdit = [this] { before_ = Capture(); };
        window_.Operations().committed = [this](const std::string& label) {
            const auto after = Capture();
            if (SceneCodec::Encode(before_.scene) != SceneCodec::Encode(after.scene))
                history_.Record(before_, after, QString::fromStdString(label));
            window_.Documents().Activate(documentKey_);
            UpdateTitle();
        };
        connect(&history_.stack, &QUndoStack::cleanChanged, this, [this] { UpdateTitle(); });
        documentKey_ = window_.Documents().Register({this,
            {window_.ViewportWidget().parentWidget(), window_.findChild<QDockWidget*>("InspectorDock"),
                window_.findChild<QDockWidget*>("SceneDock"), window_.findChild<QDockWidget*>("AssetsDock")},
            &history_.stack, [this] { return Save(); }, [this] { return SaveAsDialog(); }, [this] { return MayLeave(); }});
        window_.Context().RegisterService<EditorSession>(session_);
        if (auto* handlers = window_.Context().GetService<AssetOpenHandlers>()) {
            handlers->Register("Scene", *this, [this](const AssetInfo& asset) { Open(QString::fromStdString(asset.sourcePath)); return true; });
            handlers->Register("Texture", *this, [this](const AssetInfo& asset) {
                QImageReader reader(QString::fromStdString(asset.sourcePath)); reader.setAutoTransform(true);
                const auto image = reader.read();
                if (image.isNull()) { Report(reader.errorString()); return true; }
                auto* preview = new QDialog(&window_); preview->setAttribute(Qt::WA_DeleteOnClose);
                preview->setObjectName("AssetImagePreview");
                preview->setWindowTitle(QString::fromStdString(asset.name)); preview->resize(700, 500);
                auto* layout = new QVBoxLayout(preview); auto* scroll = new QScrollArea(preview);
                auto* label = new QLabel; label->setPixmap(QPixmap::fromImage(image)); scroll->setWidget(label); layout->addWidget(scroll);
                preview->show(); return true;
            });
        }
        UpdateTitle();
    }
    ~SceneDocument() override {
        window_.Operations().beforeEdit = {}; window_.Operations().committed = {};
        window_.Documents().Unregister(documentKey_); history_.stack.clear();
        if (auto* handlers = window_.Context().GetService<AssetOpenHandlers>()) handlers->Unregister(*this);
        window_.Context().UnregisterService<EditorSession>(&session_);
    }
    const EditorSession& Session() const { return session_; }
    bool IsDirty() const { return !history_.stack.isClean(); }
    QUndoStack& History() { return history_.stack; }
    bool SaveAs(const QString& path) {
        FinishEdits();
        try {
            SceneCodec::Write(path, SceneCodec::Encode(scene_.GetObjects()));
            session_.scenePath = QFileInfo(path).absoluteFilePath().toStdString();
            session_.sceneName = QFileInfo(path).fileName().toStdString();
            history_.stack.setClean(); lastError.clear(); UpdateTitle(); window_.RefreshAssets(); return true;
        } catch (const std::exception& e) { Report(e.what()); return false; }
    }
    bool Save() {
        return session_.scenePath.empty() ? SaveAsDialog() : SaveAs(QString::fromStdString(session_.scenePath));
    }
    bool Open(const QString& path) {
        try {
            auto state = SceneCodec::Decode(SceneCodec::Read(path)); // Parse/validate before disturbing current data.
            if (!MayLeave()) return false;
            BeginReplacement(); scene_.Replace(std::move(state));
            session_.scenePath = QFileInfo(path).absoluteFilePath().toStdString();
            session_.sceneName = QFileInfo(path).fileName().toStdString();
            lastError.clear(); FinishReplacement(); return true;
        } catch (const std::exception& e) { Report(e.what()); return false; }
    }
    bool MayLeave() {
        FinishEdits();
        if (!IsDirty()) return true;
        Decision answer;
        if (decide) answer = decide();
        else {
            const auto button = QMessageBox::warning(&window_, "Unsaved scene", "Save changes to " + QString::fromStdString(session_.sceneName) + "?",
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
            answer = button == QMessageBox::Save ? Decision::Save : button == QMessageBox::Discard ? Decision::Discard : Decision::Cancel;
        }
        if (answer == Decision::Cancel) return false;
        return answer == Decision::Discard || Save();
    }
    bool OpenProject(const QString& root) {
        const QFileInfo info(root);
        if (!info.isDir()) return false;
        if (!MayLeave()) return false;
        BeginReplacement();
        session_.OpenProject(QDir(root).absolutePath().toStdString(), info.fileName().toStdString());
        ClearScene();
        assets_.SetDirectory(QDir(root).filePath("Assets"));
        window_.RefreshAssets(); FinishReplacement(); return true;
    }
    bool NewScene() {
        if (!MayLeave()) return false;
        BeginReplacement(); ClearScene(); session_.NewScene(); FinishReplacement(); return true;
    }
private:
    struct Snapshot { ReferenceSceneProvider::State scene; ObjectId selection = 0; };
    Snapshot Capture() const { return {scene_.GetObjects(), window_.Operations().Selection()}; }
    void FinishEdits() {
        window_.Operations().CommitTransformGesture();
        if (auto* view = dynamic_cast<IEditorViewport*>(&window_.ViewportWidget())) view->CancelInteraction();
    }
    bool SaveAsDialog() {
        auto path = QFileDialog::getSaveFileName(&window_, "Save Scene", session_.scenePath.empty()
            ? QDir(QString::fromStdString(session_.projectRoot)).filePath("Untitled.axl") : QString::fromStdString(session_.scenePath), "Axl scenes (*.axl)");
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += ".axl";
        return SaveAs(path);
    }
    void Report(const QString& error) {
        lastError = error; window_.statusBar()->showMessage(error);
        if (auto* log = window_.Context().GetService<IEditorLog>()) log->Log(ConsoleMessageType::Error, error);
    }
    void BeginReplacement() {
        if (auto* view = dynamic_cast<IEditorViewport*>(&window_.ViewportWidget())) view->CancelInteraction();
        history_.stack.clear();
        window_.Operations().ResetSession(); // Retire selected property editors before changing data.
        window_.PropertySections().SetObject(0);
    }
    void ClearScene() { for (auto id : scene_.GetRootObjects()) scene_.DeleteObject(id); }
    void FinishReplacement() {
        history_.stack.clear(); before_ = Capture();
        window_.Operations().Notify(EditorOperations::Change::Structure);
        window_.Documents().Activate(documentKey_); UpdateTitle();
    }
    void UpdateTitle() {
        window_.setWindowTitle(QString("Axl Editor - %1 - %2[*]").arg(
            session_.HasProject() ? QString::fromStdString(session_.projectName) : "No project",
            QString::fromStdString(session_.sceneName)));
        window_.setWindowModified(IsDirty());
        if (auto* tree = window_.findChild<QTreeWidget*>("SceneTree"); tree && tree->topLevelItemCount())
            tree->topLevelItem(0)->setText(0, QString::fromStdString(session_.sceneName));
    }
    EditorWindow& window_;
    ReferenceSceneProvider& scene_;
    ReferenceAssetProvider& assets_;
    EditorSession session_;
    Snapshot before_;
    SnapshotHistory<Snapshot> history_;
    std::size_t documentKey_ = 0;
};
