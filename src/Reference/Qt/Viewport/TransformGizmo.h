#pragma once

#include "Reference/Qt/Viewport/TranslateGizmo.h"
#include "Editor/Application/TransformGesture.h"

// One projected overlay for the three tools. Scene writes remain in ReferenceViewport.
// Move delegates to the existing world-axis gizmo; Rotate edits local Euler
// components (Rz * Ry * Rx), Scale edits local scale independently of its value.
class TransformGizmo
{
public:
    TransformTool Tool() const { return tool_; }
    void SetTool(TransformTool tool) { EndDrag(); tool_ = tool; }
    bool IsDragging() const { return tool_ == TransformTool::Move ? move_.IsDragging() : dragAxis_ >= 0; }
    int DragAxis() const { return dragAxis_; } // 3 is uniform scale.
    void EndDrag() { move_.EndDrag(); dragAxis_ = -1; }
    void Clear() { move_.Clear(); visible_.fill(false); EndDrag(); }

    void Update(const QMatrix4x4& viewProjection, QSize size, QVector3D origin, bool twoD,
        const QMatrix4x4& parent, const Transform& transform)
    {
        if (tool_ == TransformTool::Move) { move_.Update(viewProjection, size, origin, twoD); return; }
        visible_.fill(false);
        size_ = size;
        origin_ = origin;
        bool invertible = false;
        inverse_ = viewProjection.inverted(&invertible);
        const auto projected = Project(viewProjection, origin);
        if (!invertible || !projected) return;
        center_ = *projected;
        const auto clip = viewProjection * QVector4D(origin, 1);
        const float depth = clip.z() / clip.w();
        const float radius = (Unproject(center_ + QPointF(1, 0), depth, inverse_, size_) - origin).length()
            * (tool_ == TransformTool::Rotate ? 75.f : 90.f);
        if (!std::isfinite(radius) || radius <= 0) return;

        // Prefix matrices describe each editable Euler component's actual axis.
        QMatrix4x4 frame = parent;
        std::array<QMatrix4x4, 3> rotationFrames;
        rotationFrames[2] = frame;
        frame.rotate(transform.rotation.z, 0, 0, 1);
        rotationFrames[1] = frame;
        frame.rotate(transform.rotation.y, 0, 1, 0);
        rotationFrames[0] = frame;
        frame.rotate(transform.rotation.x, 1, 0, 0);
        for (int axis = 0; axis < 3; ++axis) {
            if (twoD && (tool_ == TransformTool::Rotate ? axis != 2 : axis == 2)) continue;
            if (tool_ == TransformTool::Scale) {
                const auto direction = frame.mapVector(Direction(axis));
                if (direction.lengthSquared() < 1e-10f) continue;
                const auto tip = Project(viewProjection, origin + direction.normalized() * radius);
                if (!tip) continue;
                tips_[axis] = *tip;
                visible_[axis] = LengthSquared(*tip - center_) >= 144;
            } else {
                u_[axis] = rotationFrames[axis].mapVector(Direction((axis + 1) % 3)).normalized();
                v_[axis] = rotationFrames[axis].mapVector(Direction((axis + 2) % 3)).normalized();
                const auto uTip = Project(viewProjection, origin + u_[axis] * radius);
                const auto vTip = Project(viewProjection, origin + v_[axis] * radius);
                if (!uTip || !vTip) continue;
                const auto a = *uTip - center_, b = *vTip - center_;
                // Edge-on rings do not provide a stable plane intersection.
                if (std::abs(a.x() * b.y() - a.y() * b.x()) < 400) continue;
                rings_[axis].clear();
                for (int step = 0; step <= 72; ++step) {
                    const double angle = step * Pi / 36;
                    const auto point = Project(viewProjection, origin + radius *
                        (u_[axis] * float(std::cos(angle)) + v_[axis] * float(std::sin(angle))));
                    if (!point) { rings_[axis].clear(); break; }
                    rings_[axis].push_back(*point);
                }
                visible_[axis] = !rings_[axis].isEmpty();
            }
        }
    }

    int HitTest(QPointF mouse) const
    {
        if (tool_ == TransformTool::Move) return move_.HitTest(mouse);
        if (tool_ == TransformTool::Scale && AnyVisible()
            && std::abs(mouse.x() - center_.x()) <= 7 && std::abs(mouse.y() - center_.y()) <= 7) return 3;
        int result = -1;
        double best = 8;
        for (int axis = 0; axis < 3; ++axis) {
            if (!visible_[axis]) continue;
            if (tool_ == TransformTool::Scale) {
                const auto line = tips_[axis] - center_;
                const double t = QPointF::dotProduct(mouse - center_, line) / LengthSquared(line);
                if (t < .12 || t > 1.08) continue;
                const double distance = DistanceToSegment(mouse, center_ + line * .12, tips_[axis]);
                if (distance < best) { best = distance; result = axis; }
            } else {
                for (int point = 1; point < rings_[axis].size(); ++point) {
                    const double distance = DistanceToSegment(mouse, rings_[axis][point - 1], rings_[axis][point]);
                    if (distance < best) { best = distance; result = axis; }
                }
            }
        }
        return result;
    }

    void Draw(QPainter& painter, int hover) const;

    bool BeginDrag(QPointF mouse)
    {
        if (tool_ == TransformTool::Move) return move_.BeginDrag(mouse);
        dragAxis_ = HitTest(mouse);
        if (dragAxis_ < 0) return false;
        dragStart_ = mouse;
        dragInverse_ = inverse_;
        dragSize_ = size_;
        dragOrigin_ = origin_;
        if (tool_ == TransformTool::Rotate) {
            dragU_ = u_[dragAxis_]; dragV_ = v_[dragAxis_];
            const auto angle = PlaneAngle(mouse);
            if (!angle) { EndDrag(); return false; }
            previousAngle_ = *angle;
            accumulatedAngle_ = 0;
        } else if (dragAxis_ < 3) dragLine_ = tips_[dragAxis_] - center_;
        return true;
    }

    std::optional<QVector3D> TranslationOffset(QPointF mouse) const { return move_.DragOffset(mouse); }
    std::optional<float> DragAmount(QPointF mouse)
    {
        if (dragAxis_ < 0) return std::nullopt;
        if (tool_ == TransformTool::Rotate) {
            const auto angle = PlaneAngle(mouse);
            if (!angle) return std::nullopt;
            accumulatedAngle_ += std::remainder(*angle - previousAngle_, 2 * Pi);
            previousAngle_ = *angle;
            return float(accumulatedAngle_ * 180 / Pi);
        }
        const auto delta = mouse - dragStart_;
        return float(dragAxis_ == 3 ? (delta.x() - delta.y()) / 90
            : QPointF::dotProduct(delta, dragLine_) / LengthSquared(dragLine_));
    }

private:
    static constexpr double Pi = 3.14159265358979323846;
    static QVector3D Direction(int axis) { QVector3D result; result[axis] = 1; return result; }
    static double LengthSquared(QPointF point) { return QPointF::dotProduct(point, point); }
    bool AnyVisible() const { return std::any_of(visible_.begin(), visible_.end(), [](bool v) { return v; }); }
    static double DistanceToSegment(QPointF point, QPointF a, QPointF b)
    {
        const auto line = b - a;
        const double t = QPointF::dotProduct(point - a, line) / std::max(1e-10, LengthSquared(line));
        return std::sqrt(LengthSquared(point - a - line * std::clamp(t, 0., 1.)));
    }
    std::optional<QPointF> Project(const QMatrix4x4& matrix, QVector3D point) const
    {
        if (size_.width() <= 0 || size_.height() <= 0) return std::nullopt;
        const auto clip = matrix * QVector4D(point, 1);
        if (clip.w() <= 0) return std::nullopt;
        const auto ndc = clip.toVector3DAffine();
        if (!std::isfinite(ndc.x()) || !std::isfinite(ndc.y()) || ndc.z() < -1 || ndc.z() > 1) return std::nullopt;
        return QPointF((ndc.x() + 1) * size_.width() * .5, (1 - ndc.y()) * size_.height() * .5);
    }
    static QVector3D Unproject(QPointF mouse, float depth, const QMatrix4x4& inverse, QSize size)
    {
        return (inverse * QVector4D(float(mouse.x() * 2 / size.width() - 1),
            float(1 - mouse.y() * 2 / size.height()), depth, 1)).toVector3DAffine();
    }
    std::optional<double> PlaneAngle(QPointF mouse) const
    {
        const auto near = Unproject(mouse, -1, dragInverse_, dragSize_);
        const auto ray = (Unproject(mouse, 1, dragInverse_, dragSize_) - near).normalized();
        const auto normal = QVector3D::crossProduct(dragU_, dragV_).normalized();
        const float denominator = QVector3D::dotProduct(normal, ray);
        if (std::abs(denominator) < .02f) return std::nullopt;
        const float distance = QVector3D::dotProduct(normal, dragOrigin_ - near) / denominator;
        if (!std::isfinite(distance) || distance < 0) return std::nullopt;
        const auto point = near + ray * distance - dragOrigin_;
        const double uv = QVector3D::dotProduct(dragU_, dragV_);
        const double determinant = 1 - uv * uv;
        if (determinant < 1e-6) return std::nullopt;
        const double pu = QVector3D::dotProduct(point, dragU_), pv = QVector3D::dotProduct(point, dragV_);
        const double x = (pu - pv * uv) / determinant, y = (pv - pu * uv) / determinant;
        if (x * x + y * y < 1e-10 || !std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
        return std::atan2(y, x);
    }

    TransformTool tool_ = TransformTool::Move;
    TranslateGizmo move_;
    QSize size_, dragSize_;
    QMatrix4x4 inverse_, dragInverse_;
    QVector3D origin_, dragOrigin_, dragU_, dragV_;
    std::array<QVector3D, 3> u_{}, v_{};
    QPointF center_, dragStart_, dragLine_;
    std::array<QPointF, 3> tips_{};
    std::array<QPolygonF, 3> rings_;
    std::array<bool, 3> visible_{};
    int dragAxis_ = -1;
    double previousAngle_ = 0, accumulatedAngle_ = 0;
};
