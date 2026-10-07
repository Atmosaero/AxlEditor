#pragma once
#include "Tools/ScriptCanvas/ScriptCanvasView.h"
#include "Tools/ScriptCanvas/GraphCodec.h"
#include "Editor/Qt/SnapshotHistory.h"
#include "Editor/Qt/DocumentActions.h"
#include "Editor/Qt/DocumentFiles.h"
#include "Editor/Qt/EditorContext.h"
#include "Editor/Qt/AssetWidgets.h"
#include "Editor/Qt/IEditorLog.h"
#include "Editor/Application/EditorSession.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QLabel>
#include <QDir>
#include <QStatusBar>

class GraphDocument final : public QObject
{
public:
    enum class Decision { Save, Discard, Cancel };
    std::function<Decision()> decide;
    QString lastError;
    GraphDocument(ScriptCanvasView& view, EditorContext& context, QLabel& title)
        : QObject(&view), view_(view), alive_(&view), context_(context), title_(title) {
        history_.apply = [this](const ScriptGraph::Graph& graph) { view_.SetGraph(graph); UpdateTitle(); };
        view_.edited = [this](const auto& before, const auto& after, const QString& label) {
            history_.Record(before, after, label); Activate(); UpdateTitle();
        };
        connect(&history_.stack, &QUndoStack::cleanChanged, this, [this] { UpdateTitle(); });
        if (auto* actions = context_.GetService<DocumentActions>())
            key_ = actions->Register({this, {view_.parentWidget() ? view_.parentWidget() : &view_}, &history_.stack,
                [this] { return Save(); }, [this] { return SaveAsDialog(); }, [this] { return MayLeave(); }});
        if (auto* assets = context_.GetService<AssetOpenHandlers>())
            assets->Register("Graph", *this, [this](const AssetInfo& asset) {
                if (Open(QString::fromStdString(asset.sourcePath))) {
                    view_.window()->show();
                    if (auto* dock = qobject_cast<QDockWidget*>(view_.parentWidget()->parentWidget())) { dock->show(); dock->raise(); }
                    view_.setFocus();
                }
                return true;
            });
        UpdateTitle();
    }
    ~GraphDocument() override {
        if (alive_) view_.edited = {};
        disconnect(&history_.stack, nullptr, this, nullptr); history_.stack.clear();
        if (auto* actions = context_.GetService<DocumentActions>()) actions->Unregister(key_);
        if (auto* assets = context_.GetService<AssetOpenHandlers>()) assets->Unregister(*this);
    }
    QUndoStack& History() { return history_.stack; }
    bool IsDirty() const { return !history_.stack.isClean(); }
    QString Path() const { return path_; }
    bool SaveAs(const QString& path) {
        view_.FinishGesture();
        try {
            DocumentFiles::Write(path, GraphCodec::Encode(view_.GraphData()));
            path_ = QFileInfo(path).absoluteFilePath(); history_.stack.setClean(); lastError.clear(); Activate(); UpdateTitle(); return true;
        } catch (const std::exception& e) { Report(e.what()); return false; }
    }
    bool Save() { return path_.isEmpty() ? SaveAsDialog() : SaveAs(path_); }
    bool Open(const QString& path) {
        try {
            auto graph = GraphCodec::Decode(DocumentFiles::Read(path));
            if (!MayLeave()) return false;
            view_.SetGraph(std::move(graph)); history_.stack.clear();
            path_ = QFileInfo(path).absoluteFilePath(); lastError.clear(); Activate(); UpdateTitle(); return true;
        } catch (const std::exception& e) { Report(e.what()); return false; }
    }
    bool New() {
        if (!MayLeave()) return false;
        view_.SetGraph({}); path_.clear(); history_.stack.clear(); Activate(); UpdateTitle(); return true;
    }
    bool MayLeave() {
        view_.FinishGesture();
        if (!IsDirty()) return true;
        const auto answer = decide ? decide() : Ask();
        return answer == Decision::Discard || (answer == Decision::Save && Save());
    }
    void OpenDialog() {
        const auto path = QFileDialog::getOpenFileName(&view_, "Open Script Graph", DefaultDirectory(), "Axl graphs (*.axlgraph)");
        if (!path.isEmpty()) Open(path);
    }
private:
    void Activate() { if (auto* actions = context_.GetService<DocumentActions>()) actions->Activate(key_); }
    Decision Ask() {
        const auto answer = QMessageBox::warning(&view_, "Unsaved graph", "Save changes to Script Canvas?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        return answer == QMessageBox::Save ? Decision::Save : answer == QMessageBox::Discard ? Decision::Discard : Decision::Cancel;
    }
    QString DefaultDirectory() const {
        if (auto* session = context_.GetService<EditorSession>()) return QString::fromStdString(session->projectRoot);
        return {};
    }
    bool SaveAsDialog() {
        auto path = QFileDialog::getSaveFileName(&view_, "Save Script Graph",
            path_.isEmpty() ? QDir(DefaultDirectory()).filePath("Untitled.axlgraph") : path_, "Axl graphs (*.axlgraph)");
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += ".axlgraph";
        return SaveAs(path);
    }
    void UpdateTitle() { title_.setText((path_.isEmpty() ? "Untitled Graph" : QFileInfo(path_).fileName()) + (IsDirty() ? " *" : "")); }
    void Report(const QString& error) {
        lastError = error; title_.setToolTip(error);
        if (auto* log = context_.GetService<IEditorLog>()) log->Log(ConsoleMessageType::Error, error);
        if (auto* window = context_.GetService<QMainWindow>()) window->statusBar()->showMessage(error);
    }
    ScriptCanvasView& view_;
    QPointer<ScriptCanvasView> alive_;
    EditorContext& context_;
    QLabel& title_;
    SnapshotHistory<ScriptGraph::Graph> history_;
    QString path_;
    std::size_t key_ = 0;
};
