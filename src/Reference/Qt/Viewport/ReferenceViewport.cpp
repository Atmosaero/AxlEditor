#include "Reference/Qt/Viewport/ReferenceViewport.h"
#include "Editor/Qt/EditorTheme.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QOpenGLContext>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {
struct Vertex
{
    float position[3];
    float normal[3];
    float color[3];
};

Vertex MakeVertex(QVector3D p, QVector3D n, QVector3D c)
{
    return {{p.x(), p.y(), p.z()}, {n.x(), n.y(), n.z()}, {c.x(), c.y(), c.z()}};
}

QVector3D Rgb(const QColor& color)
{
    return {color.redF(), color.greenF(), color.blueF()};
}

bool IsMovementKey(int key)
{
    return key == Qt::Key_W || key == Qt::Key_A || key == Qt::Key_S || key == Qt::Key_D;
}
}

ReferenceViewport::ReferenceViewport(ReferenceSceneProvider& scene, QWidget* parent)
    : QOpenGLWidget(parent), scene_(scene), sceneProvider_(scene)
{
    setObjectName("Viewport");
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    overlay_ = new QLabel(this);
    overlay_->setObjectName("ViewportOverlay");
    overlay_->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlay_->setStyleSheet(QString("color: %1; background: %2; padding: 10px;")
        .arg(DarkTheme().error.name(), DarkTheme().panelAlt.name()));
    overlay_->hide(); // Only render errors belong on the canvas; controls live in Scene view.

    movementTimer_.setInterval(16);
    movementTimer_.setTimerType(Qt::PreciseTimer);
    connect(&movementTimer_, &QTimer::timeout, this, &ReferenceViewport::MoveCamera);
}

ReferenceViewport::~ReferenceViewport()
{
    StopNavigation();
    if (context())
        disconnect(context(), nullptr, this, nullptr);
    Cleanup();
}

void ReferenceViewport::initializeGL()
{
    if (!initializeOpenGLFunctions()) {
        ReportError("OpenGL 3.3 is unavailable.");
        return;
    }
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &ReferenceViewport::Cleanup, Qt::DirectConnection);
    const auto& theme = DarkTheme();
    const auto& background = theme.viewportBackground;
    glClearColor(background.redF(), background.greenF(), background.blueF(), background.alphaF());

    program_ = std::make_unique<QOpenGLShaderProgram>();
    const char* vertexShader = R"(
        #version 330 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 2) in vec3 color;
        uniform mat4 viewProjection;
        uniform mat4 model;
        uniform mat3 normalMatrix;
        out vec3 vertexColor;
        out vec3 worldNormal;
        void main() {
            gl_Position = viewProjection * model * vec4(position, 1.0);
            vertexColor = color;
            worldNormal = normalMatrix * normal;
        }
    )";
    const char* fragmentShader = R"(
        #version 330 core
        in vec3 vertexColor;
        in vec3 worldNormal;
        uniform bool lit;
        out vec4 fragColor;
        void main() {
            vec3 n = worldNormal / max(length(worldNormal), 0.00001);
            float light = lit ? 0.3 + 0.7 * max(dot(n, normalize(vec3(0.4, 1.0, 0.3))), 0.0) : 1.0;
            fragColor = vec4(vertexColor * light, 1.0);
        }
    )";
    if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader)
        || !program_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader)
        || !program_->link()) {
        ReportError("Viewport shader: " + program_->log());
        return;
    }

    std::vector<Vertex> data;
    for (int mode = 0; mode < 2; ++mode)
    for (int i = -20; i <= 20; ++i) {
        const float coordinate = static_cast<float>(i);
        const auto color = Rgb(i % 5 == 0 ? theme.viewportGridMajor : theme.viewportGridMinor);
        const auto xColor = i == 0 ? Rgb(theme.viewportAxisX) : color;
        const auto axisColor = i == 0 ? Rgb(mode == 0 ? theme.viewportAxisZ : theme.viewportAxisY) : color;
        if (mode == 0) {
            data.push_back(MakeVertex({-20, 0, coordinate}, {0, 1, 0}, xColor));
            data.push_back(MakeVertex({20, 0, coordinate}, {0, 1, 0}, xColor));
            data.push_back(MakeVertex({coordinate, 0, -20}, {0, 1, 0}, axisColor));
            data.push_back(MakeVertex({coordinate, 0, 20}, {0, 1, 0}, axisColor));
        } else {
            data.push_back(MakeVertex({-20, coordinate, 0}, {0, 0, 1}, xColor));
            data.push_back(MakeVertex({20, coordinate, 0}, {0, 0, 1}, xColor));
            data.push_back(MakeVertex({coordinate, -20, 0}, {0, 0, 1}, axisColor));
            data.push_back(MakeVertex({coordinate, 20, 0}, {0, 0, 1}, axisColor));
        }
    }
    gridVertexCount_ = static_cast<int>(data.size()) / 2;
    const QVector3D faces[6][4] = {
        {{-.5f,-.5f,.5f}, {.5f,-.5f,.5f}, {.5f,.5f,.5f}, {-.5f,.5f,.5f}},
        {{.5f,-.5f,-.5f}, {-.5f,-.5f,-.5f}, {-.5f,.5f,-.5f}, {.5f,.5f,-.5f}},
        {{.5f,-.5f,.5f}, {.5f,-.5f,-.5f}, {.5f,.5f,-.5f}, {.5f,.5f,.5f}},
        {{-.5f,-.5f,-.5f}, {-.5f,-.5f,.5f}, {-.5f,.5f,.5f}, {-.5f,.5f,-.5f}},
        {{-.5f,.5f,.5f}, {.5f,.5f,.5f}, {.5f,.5f,-.5f}, {-.5f,.5f,-.5f}},
        {{-.5f,-.5f,-.5f}, {.5f,-.5f,-.5f}, {.5f,-.5f,.5f}, {-.5f,-.5f,.5f}}
    };
    const QVector3D normals[6] = {{0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}};
    for (int face = 0; face < 6; ++face)
        for (int corner : {0, 1, 2, 0, 2, 3})
            data.push_back(MakeVertex(faces[face][corner], normals[face], Rgb(theme.referenceCube)));

    if (!vertexArray_.create() || !vertices_.create()) {
        ReportError("Unable to create viewport geometry buffers.");
        return;
    }
    QOpenGLVertexArrayObject::Binder bind(&vertexArray_);
    vertices_.bind();
    vertices_.allocate(data.data(), static_cast<int>(data.size() * sizeof(Vertex)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, color)));
    vertices_.release();
    ready_ = true;
    overlay_->hide();
}

void ReferenceViewport::resizeGL(int width, int height)
{
    Q_UNUSED(width);
    Q_UNUSED(height);
    StopDrag();
    UpdateProjection();
}

void ReferenceViewport::UpdateProjection()
{
    projection_.setToIdentity();
    const float aspect = static_cast<float>(width()) / std::max(height(), 1);
    if (twoD_)
        projection_.ortho(-orthoHalfHeight_ * aspect, orthoHalfHeight_ * aspect,
            -orthoHalfHeight_, orthoHalfHeight_, .05f, 1000.0f);
    else
        projection_.perspective(60.0f, aspect, 0.05f, 1000.0f);
}

void ReferenceViewport::paintGL()
{
    if (!ready_)
        return;
    glViewport(0, 0, qRound(width() * devicePixelRatioF()), qRound(height() * devicePixelRatioF()));
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE); // Negative scales remain visible.
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const auto view = CameraView();
    {
        program_->bind();
        QOpenGLVertexArrayObject::Binder bind(&vertexArray_);
        program_->setUniformValue("viewProjection", projection_ * view);
        QMatrix4x4 model;
        program_->setUniformValue("model", model);
        program_->setUniformValue("normalMatrix", model.normalMatrix());
        program_->setUniformValue("lit", false);
        glDrawArrays(GL_LINES, twoD_ ? gridVertexCount_ : 0, gridVertexCount_);

        program_->setUniformValue("lit", !twoD_);
        for (const auto& [id, object] : scene_.GetObjects()) {
            model = ObjectMatrix(id);
            program_->setUniformValue("model", model);
            program_->setUniformValue("normalMatrix", model.normalMatrix());
            glDrawArrays(GL_TRIANGLES, gridVertexCount_ * 2, 36);
        }
        program_->release();
    } // Release the GL vertex array before Qt draws the overlay.
    UpdateGizmo();
    QPainter painter(this);
    gizmo_.Draw(painter, hoverAxis_);
}

QMatrix4x4 ReferenceViewport::CameraView() const
{
    QMatrix4x4 view;
    if (twoD_)
        view.lookAt(cameraCenter2D_ + QVector3D(0, 0, 100), cameraCenter2D_, {0, 1, 0});
    else
        view.lookAt(cameraPosition_, cameraPosition_ + Forward(), {0, 1, 0});
    return view;
}

QMatrix4x4 ReferenceViewport::ObjectMatrix(ObjectId id) const
{
    std::vector<Transform> chain;
    while (id != InvalidObjectId) {
        if (auto transform = sceneProvider_.GetTransform(id))
            chain.push_back(*transform);
        id = sceneProvider_.GetParent(id);
    }
    QMatrix4x4 model;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        model.translate(it->position.x, it->position.y, it->position.z);
        model.rotate(it->rotation.z, 0, 0, 1);
        model.rotate(it->rotation.y, 0, 1, 0);
        model.rotate(it->rotation.x, 1, 0, 0);
        model.scale(it->scale.x, it->scale.y, it->scale.z);
    }
    return model;
}

void ReferenceViewport::SetSelectedObject(ObjectId id)
{
    if (selectedObject_ == id) return;
    StopDrag();
    selectedObject_ = id;
    hoverAxis_ = -1;
    gizmo_.Clear();
    update();
}

void ReferenceViewport::Set2DMode(bool enabled)
{
    if (twoD_ == enabled) return;
    StopNavigation();
    twoD_ = enabled;
    hoverAxis_ = -1;
    UpdateProjection();
    update();
    emit events_.CameraModeChanged(twoD_);
}

void ReferenceViewport::SetNavigationSpeed(float speed)
{
    if (!std::isfinite(speed)) return;
    speed = std::clamp(speed, .1f, 100.0f);
    if (qFuzzyCompare(speed_, speed)) return;
    speed_ = speed;
    emit events_.NavigationSpeedChanged(speed_);
}

void ReferenceViewport::SetTransformTool(TransformTool tool)
{
    if (tool != TransformTool::Move && tool != TransformTool::Rotate && tool != TransformTool::Scale) return;
    if (gizmo_.Tool() == tool) return;
    StopNavigation();
    gizmo_.SetTool(tool);
    hoverAxis_ = -1;
    update();
    emit events_.TransformToolChanged(tool);
}

void ReferenceViewport::UpdateGizmo()
{
    if (selectedObject_ == InvalidObjectId) {
        gizmo_.Clear();
        return;
    }
    try {
        const auto transform = sceneProvider_.GetTransform(selectedObject_);
        if (!transform) {
            StopDrag();
            gizmo_.Clear();
            return;
        }
        const auto origin = ObjectMatrix(selectedObject_).map(QVector3D());
        const auto parent = ObjectMatrix(sceneProvider_.GetParent(selectedObject_));
        gizmo_.Update(projection_ * CameraView(), size(), origin, twoD_, parent, *transform);
    } catch (const std::out_of_range&) {
        SetSelectedObject(InvalidObjectId);
        emit events_.TransformEdited(); // Let the editor clear its stale selection too.
    }
}

void ReferenceViewport::Cleanup()
{
    if (!context() || !context()->isValid())
        return;
    makeCurrent();
    vertexArray_.destroy();
    vertices_.destroy();
    program_.reset();
    ready_ = false;
    doneCurrent();
}

QVector3D ReferenceViewport::Forward() const
{
    const float yaw = qDegreesToRadians(yaw_);
    const float pitch = qDegreesToRadians(pitch_);
    return {std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)};
}

void ReferenceViewport::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton && !looking_ && ready_) {
        UpdateGizmo();
        if (gizmo_.HitTest(event->position()) >= 0) {
            StopNavigation();
            try {
                bool invertible = false;
                dragParentInverse_ = ObjectMatrix(sceneProvider_.GetParent(selectedObject_)).inverted(&invertible);
                const auto transform = sceneProvider_.GetTransform(selectedObject_);
                if (!transform) { StopDrag(); gizmo_.Clear(); return; }
                dragStartTransform_ = *transform;
                if (invertible && gizmo_.BeginDrag(event->position())) {
                    if (!operations_->BeginTransformGesture(selectedObject_)) { gizmo_.EndDrag(); return; }
                    grabMouse();
                    setCursor(Qt::SizeAllCursor);
                    update();
                    event->accept();
                    return;
                }
            } catch (const std::out_of_range&) {
                SetSelectedObject(InvalidObjectId);
                emit events_.TransformEdited();
            }
        }
        const ObjectId picked = Pick(event->position());
        operations_->Select(picked);
        SetSelectedObject(picked);
        emit events_.ObjectPicked(picked);
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton && !gizmo_.IsDragging()) {
        looking_ = true;
        lastMousePosition_ = event->globalPosition();
        grabMouse();
        setCursor(Qt::SizeAllCursor);
        event->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(event);
}

void ReferenceViewport::mouseMoveEvent(QMouseEvent* event)
{
    if ((gizmo_.IsDragging() || looking_) && QWidget::mouseGrabber() != this) {
        StopNavigation(); // Another widget may take capture without a release event.
        event->accept();
        return;
    }
    if (gizmo_.IsDragging()) {
        if (!(event->buttons() & Qt::LeftButton)) {
            StopDrag();
            event->accept();
            return;
        }
        try {
            const auto offset = gizmo_.Tool() == TransformTool::Move
                ? gizmo_.TranslationOffset(event->position()) : std::optional<QVector3D>{};
            const auto amount = gizmo_.Tool() != TransformTool::Move
                ? gizmo_.DragAmount(event->position()) : std::optional<float>{};
            if (offset || amount) {
                auto transform = sceneProvider_.GetTransform(selectedObject_);
                if (!transform) {
                    StopDrag();
                    gizmo_.Clear();
                    emit events_.TransformEdited();
                    return;
                }
                const auto local = offset ? dragParentInverse_.mapVector(*offset) : QVector3D{};
                *transform = ApplyTransformGesture(dragStartTransform_, *transform, gizmo_.Tool(),
                    gizmo_.DragAxis(), amount.value_or(0), {local.x(), local.y(), local.z()}, twoD_);
                operations_->SetTransform(selectedObject_, *transform);
                emit events_.TransformEdited();
                update();
            }
        } catch (const std::out_of_range&) {
            SetSelectedObject(InvalidObjectId);
            emit events_.TransformEdited();
        }
        event->accept();
        return;
    }
    if (looking_) {
        if (!(event->buttons() & Qt::RightButton)) {
            StopNavigation();
            event->accept();
            return;
        }
        const QPointF delta = event->globalPosition() - lastMousePosition_;
        lastMousePosition_ = event->globalPosition();
        if (twoD_) {
            const float units = 2 * orthoHalfHeight_ / std::max(height(), 1);
            cameraCenter2D_ += QVector3D(-float(delta.x()) * units, float(delta.y()) * units, 0);
        } else {
            yaw_ += static_cast<float>(delta.x()) * 0.2f;
            pitch_ = std::clamp(pitch_ - static_cast<float>(delta.y()) * 0.2f, -89.0f, 89.0f);
        }
        update();
        event->accept();
        return;
    }
    UpdateGizmo();
    const int hover = gizmo_.HitTest(event->position());
    if (hover != hoverAxis_) {
        hoverAxis_ = hover;
        if (hover >= 0) setCursor(Qt::SizeAllCursor); else unsetCursor();
        update();
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

void ReferenceViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && gizmo_.IsDragging()) {
        StopDrag();
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton && looking_) {
        looking_ = false;
        releaseMouse();
        unsetCursor();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void ReferenceViewport::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_F && !gizmo_.IsDragging()) { FrameSelected(); event->accept(); return; }
    if (event->key() == Qt::Key_Escape && (gizmo_.IsDragging() || looking_ || !keys_.isEmpty())) {
        operations_->CancelTransformGesture();
        StopNavigation();
        update();
        event->accept();
        return;
    }
    if (gizmo_.IsDragging()) { event->accept(); return; }
    if (IsMovementKey(event->key())) {
        keys_.insert(event->key());
        if (!movementTimer_.isActive()) {
            movementClock_.start();
            movementTimer_.start();
        }
        event->accept();
        return;
    }
    QOpenGLWidget::keyPressEvent(event);
}

void ReferenceViewport::keyReleaseEvent(QKeyEvent* event)
{
    if (IsMovementKey(event->key())) {
        if (!event->isAutoRepeat())
            keys_.remove(event->key());
        if (keys_.isEmpty())
            movementTimer_.stop();
        event->accept();
        return;
    }
    QOpenGLWidget::keyReleaseEvent(event);
}

void ReferenceViewport::MoveCamera()
{
    if (!hasFocus() || (looking_ && QWidget::mouseGrabber() != this)) {
        StopNavigation();
        return;
    }
    const float dt = std::min(movementClock_.restart() / 1000.0f, 0.1f);
    const QVector3D forward = twoD_ ? QVector3D(0, 1, 0) : Forward();
    const QVector3D right = twoD_ ? QVector3D(1, 0, 0) : QVector3D::crossProduct(forward, {0, 1, 0}).normalized();
    QVector3D direction;
    if (keys_.contains(Qt::Key_W)) direction += forward;
    if (keys_.contains(Qt::Key_S)) direction -= forward;
    if (keys_.contains(Qt::Key_D)) direction += right;
    if (keys_.contains(Qt::Key_A)) direction -= right;
    if (!direction.isNull()) {
        if (twoD_) cameraCenter2D_ += direction.normalized() * speed_ * dt;
        else cameraPosition_ += direction.normalized() * speed_ * dt;
        update();
    }
}

void ReferenceViewport::wheelEvent(QWheelEvent* event)
{
    if (gizmo_.IsDragging()) { event->accept(); return; }
    const float factor = std::pow(1.2f, event->angleDelta().y() / 120.0f);
    if (twoD_) {
        orthoHalfHeight_ = std::clamp(orthoHalfHeight_ / factor, .1f, 100.0f);
        UpdateProjection();
        update();
    } else
        SetNavigationSpeed(speed_ * factor);
    event->accept();
}

void ReferenceViewport::StopNavigation()
{
    StopDrag();
    keys_.clear();
    movementTimer_.stop();
    if (looking_) {
        looking_ = false;
        if (QWidget::mouseGrabber() == this) releaseMouse();
        unsetCursor();
    }
}

void ReferenceViewport::StopDrag()
{
    if (!gizmo_.IsDragging()) return;
    gizmo_.EndDrag();
    operations_->CommitTransformGesture();
    if (QWidget::mouseGrabber() == this) releaseMouse();
    unsetCursor();
    hoverAxis_ = -1;
    update();
}

void ReferenceViewport::focusOutEvent(QFocusEvent* event)
{
    StopNavigation();
    QOpenGLWidget::focusOutEvent(event);
}

bool ReferenceViewport::event(QEvent* event)
{
    if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide
        || (event->type() == QEvent::UngrabMouse && (looking_ || gizmo_.IsDragging())))
        StopNavigation();
    return QOpenGLWidget::event(event);
}

void ReferenceViewport::ReportError(const QString& message)
{
    ready_ = false;
    overlay_->setText(message);
    overlay_->adjustSize();
    overlay_->show();
    qWarning().noquote() << message;
    emit events_.RenderError(message);
}

ObjectId ReferenceViewport::Pick(QPointF point) const
{
    bool valid = false;
    const auto inverse = (projection_ * CameraView()).inverted(&valid);
    if (!valid || width() <= 0 || height() <= 0) return 0;
    const float x = float(point.x() * 2 / width() - 1), y = float(1 - point.y() * 2 / height());
    const auto near = inverse.map(QVector3D(x, y, -1));
    const auto far = inverse.map(QVector3D(x, y, 1));
    ObjectId picked = 0; float nearest = 1;
    for (const auto& [id, object] : scene_.GetObjects()) {
        const auto objectInverse = ObjectMatrix(id).inverted(&valid);
        if (!valid) continue; // A zero-scale cube has no volume to pick.
        const auto origin = objectInverse.map(near), delta = objectInverse.map(far) - origin;
        float enter = 0, leave = nearest;
        for (int axis = 0; axis < 3 && enter <= leave; ++axis) {
            if (std::abs(delta[axis]) < 1e-8f) {
                if (origin[axis] < -.5f || origin[axis] > .5f) enter = 2;
            } else {
                float a = (-.5f - origin[axis]) / delta[axis], b = (.5f - origin[axis]) / delta[axis];
                if (a > b) std::swap(a, b);
                enter = std::max(enter, a); leave = std::min(leave, b);
            }
        }
        if (enter <= leave && enter < nearest) { picked = id; nearest = enter; }
    }
    return picked;
}

void ReferenceViewport::FrameSelected()
{
    if (!selectedObject_) return;
    StopNavigation();
    try {
        const auto matrix = ObjectMatrix(selectedObject_);
        const auto center = matrix.map(QVector3D{});
        float radius = .5f;
        for (float x : {-.5f, .5f}) for (float y : {-.5f, .5f}) for (float z : {-.5f, .5f})
            radius = std::max(radius, (matrix.map(QVector3D(x, y, z)) - center).length());
        const float aspect = float(width()) / std::max(height(), 1);
        if (twoD_) { cameraCenter2D_ = center; orthoHalfHeight_ = radius * 1.3f / std::min(aspect, 1.f); }
        else cameraPosition_ = center - Forward() * (radius * 2.6f / std::min(aspect, 1.f));
        UpdateProjection(); update();
    } catch (const std::out_of_range&) { SetSelectedObject(0); }
}
