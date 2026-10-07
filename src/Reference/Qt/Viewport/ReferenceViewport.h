#pragma once

#include "Reference/Scene/ReferenceSceneProvider.h"
#include "Reference/Qt/Viewport/TransformGizmo.h"
#include "Editor/Qt/IEditorViewport.h"

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QSet>
#include <QTimer>
#include <memory>

class QLabel;

// Reference renderer, not a universal engine viewport. Objects from the reference
// scene are drawn as cubes; OpenGL resources belong to this widget. The shell uses
// only IEditorViewport and may instead host an external runtime's QWidget.
class ReferenceViewport final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core, public IEditorViewport
{
    Q_OBJECT

public:
    explicit ReferenceViewport(ReferenceSceneProvider& scene, QWidget* parent = nullptr);
    ~ReferenceViewport() override;
    EditorViewportEvents& Events() override { return events_; }
    void SetSelectedObject(ObjectId id) override;
    void Set2DMode(bool enabled) override;
    bool Is2DMode() const override { return twoD_; }
    float NavigationSpeed() const override { return speed_; }
    void SetNavigationSpeed(float speed) override;
    bool SupportsTransformTools() const override { return true; }
    TransformTool ActiveTransformTool() const override { return gizmo_.Tool(); }
    void SetTransformTool(TransformTool tool) override;
    void BindOperations(EditorOperations* operations) override { StopNavigation(); operations_ = operations ? operations : &fallbackOperations_; }
    void CancelInteraction() override { operations_->CancelTransformGesture(); StopNavigation(); }
    void FrameSelected() override;

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool event(QEvent* event) override;

private:
    EditorViewportEvents events_;
    void Cleanup();
    void StopNavigation();
    void StopDrag();
    void MoveCamera();
    void ReportError(const QString& message);
    QVector3D Forward() const;
    QMatrix4x4 CameraView() const;
    QMatrix4x4 ObjectMatrix(ObjectId id) const;
    void UpdateProjection();
    void UpdateGizmo();
    ObjectId Pick(QPointF point) const;

    const ReferenceSceneProvider& scene_;
    ISceneProvider& sceneProvider_;
    EditorOperations fallbackOperations_{sceneProvider_};
    EditorOperations* operations_ = &fallbackOperations_;
    std::unique_ptr<QOpenGLShaderProgram> program_;
    QOpenGLBuffer vertices_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vertexArray_;
    int gridVertexCount_ = 0;
    bool ready_ = false;
    QMatrix4x4 projection_;
    QVector3D cameraPosition_{4.0f, 3.0f, 6.0f};
    float yaw_ = -125.0f;
    float pitch_ = -20.0f;
    float speed_ = 3.0f;
    bool looking_ = false;
    bool twoD_ = false;
    QVector3D cameraCenter2D_;
    float orthoHalfHeight_ = 5.0f;
    ObjectId selectedObject_ = InvalidObjectId;
    TransformGizmo gizmo_;
    int hoverAxis_ = -1;
    Transform dragStartTransform_;
    QMatrix4x4 dragParentInverse_;
    QPointF lastMousePosition_;
    QSet<int> keys_;
    QTimer movementTimer_;
    QElapsedTimer movementClock_;
    QLabel* overlay_ = nullptr;
};
