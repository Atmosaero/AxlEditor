#pragma once

#include "Contracts/ISceneProvider.h"
#include <QObject>

enum class TransformTool { Move, Rotate, Scale };
Q_DECLARE_METATYPE(TransformTool)

// UI notifications belong to the widget, not to the scene/runtime contract.
class EditorViewportEvents final : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void RenderError(const QString& message);
    void TransformEdited();
    void CameraModeChanged(bool twoD);
    void NavigationSpeedChanged(float speed);
    void TransformToolChanged(TransformTool tool);
};

// Optional interface for the QWidget supplied to EditorWindow. A plain QWidget
// also works; the shell disables its camera controls. No rendering API is required.
class IEditorViewport
{
public:
    virtual ~IEditorViewport() = default;
    virtual EditorViewportEvents& Events() = 0;
    virtual void SetSelectedObject(ObjectId id) = 0;
    virtual void Set2DMode(bool enabled) = 0;
    virtual bool Is2DMode() const = 0;
    virtual float NavigationSpeed() const = 0;
    virtual void SetNavigationSpeed(float speed) = 0;
    // Optional editing controls: existing external viewports can opt in later.
    virtual bool SupportsTransformTools() const { return false; }
    virtual TransformTool ActiveTransformTool() const { return TransformTool::Move; }
    virtual void SetTransformTool(TransformTool) {}
};
