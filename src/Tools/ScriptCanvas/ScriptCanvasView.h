#pragma once

#include "Tools/ScriptCanvas/ScriptGraph.h"
#include <QGraphicsView>
#include <functional>
#include <optional>

namespace ScriptCanvasDetail { class GraphScene; }
class QGraphicsPathItem;

// Tool-local widget. The tool starts empty; tests may supply an in-memory graph.
// This is editor data only, with no scene-provider or runtime dependencies.
class ScriptCanvasView final : public QGraphicsView
{
public:
    explicit ScriptCanvasView(QWidget* parent = nullptr);
    explicit ScriptCanvasView(ScriptGraph::Graph graph, QWidget* parent = nullptr);
    const ScriptGraph::Graph& GraphData() const;
    void SetGraph(ScriptGraph::Graph graph);
    void EditGraph(const QString& label, const std::function<void(ScriptGraph::Graph&)>& edit);
    void FinishGesture(bool cancel = false);
    std::function<void(const ScriptGraph::Graph&, const ScriptGraph::Graph&, const QString&)> edited;

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void CancelConnection();
    ScriptCanvasDetail::GraphScene* graph_;
    ScriptGraph::PinId from_ = 0;
    QGraphicsPathItem* preview_ = nullptr;
    std::optional<ScriptGraph::Graph> gesture_;
    void Record(const ScriptGraph::Graph& before, const QString& label);
};
