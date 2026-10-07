#pragma once
#include "Editor/EditorTheme.h"

#include <QMatrix4x4>
#include <QPainter>
#include <QPolygonF>
#include <QVector3D>
#include <array>
#include <algorithm>
#include <cmath>
#include <optional>

// World-axis translation only. Qt paints the projected handles over the GL image.
// This class handles screen geometry and dragging; the viewport edits the provider.
class TranslateGizmo
{
public:
    void Update(const QMatrix4x4& viewProjection, QSize size, QVector3D origin, bool twoD)
    {
        visible_.fill(false);
        size_ = size;
        origin_ = origin;
        bool invertible = false;
        inverse_ = viewProjection.inverted(&invertible);
        const QVector4D clip = viewProjection * QVector4D(origin, 1);
        if (!invertible || clip.w() <= 0 || size.width() <= 0 || size.height() <= 0)
            return;
        const auto ndc = clip.toVector3DAffine();
        if (ndc.z() < -1 || ndc.z() > 1)
            return;
        center_ = ScreenPoint(ndc);
        // Keep the handles about 90 logical pixels long, independent of zoom/DPI.
        const QVector3D adjacent = Unproject(center_ + QPointF(1, 0), ndc.z(), inverse_, size_);
        const float length = (adjacent - origin).length() * 90.0f;
        for (int axis = 0; axis < (twoD ? 2 : 3); ++axis) {
            const auto end = viewProjection * QVector4D(origin + Direction(axis) * length, 1);
            if (end.w() <= 0)
                continue;
            tips_[axis] = ScreenPoint(end.toVector3DAffine());
            const QPointF line = tips_[axis] - center_;
            // An axis viewed end-on has no usable screen direction.
            visible_[axis] = QPointF::dotProduct(line, line) >= 12 * 12;
        }
    }

    void Clear() { visible_.fill(false); EndDrag(); }

    int HitTest(QPointF mouse) const
    {
        int result = -1;
        double best = 8.0; // Logical-pixel tolerance also covers the arrowhead.
        for (int axis = 0; axis < 3; ++axis) {
            if (!visible_[axis]) continue;
            const auto line = tips_[axis] - center_;
            const double t = QPointF::dotProduct(mouse - center_, line) / QPointF::dotProduct(line, line);
            if (t < .12 || t > 1.08) continue; // Leave the shared center unpickable.
            const auto nearest = center_ + line * std::clamp(t, .12, 1.0);
            const double distance = std::hypot(mouse.x() - nearest.x(), mouse.y() - nearest.y());
            if (distance < best) { best = distance; result = axis; }
        }
        return result;
    }

    void Draw(QPainter& painter, int hover) const
    {
        painter.setRenderHint(QPainter::Antialiasing);
        const auto& theme = DarkTheme();
        const QColor colors[] = {theme.axisX, theme.axisY, theme.axisZ};
        const char* labels[] = {"X", "Y", "Z"};
        bool any = false;
        for (int axis = 0; axis < 3; ++axis) {
            if (!visible_[axis]) continue;
            any = true;
            const auto line = tips_[axis] - center_;
            const double length = std::hypot(line.x(), line.y());
            const QPointF direction = line / length;
            const QPointF side(-direction.y(), direction.x());
            const QColor color = axis == (IsDragging() ? dragAxis_ : hover) ? theme.warning : colors[axis];
            painter.setPen(QPen(theme.gizmoOutline, 5));
            painter.drawLine(center_, tips_[axis]);
            painter.setPen(QPen(color, 3));
            painter.drawLine(center_, tips_[axis]);
            painter.setBrush(color);
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(QPolygonF{tips_[axis], tips_[axis] - direction * 11 + side * 5,
                tips_[axis] - direction * 11 - side * 5});
            painter.setPen(color);
            painter.drawText(tips_[axis] + direction * 8 + QPointF(-4, 4), labels[axis]);
        }
        if (any) {
            painter.setPen(QPen(theme.gizmoOutline, 1));
            painter.setBrush(theme.gizmoCenter);
            painter.drawRect(QRectF(center_ - QPointF(3, 3), QSizeF(6, 6)));
        }
    }

    bool BeginDrag(QPointF mouse)
    {
        dragAxis_ = HitTest(mouse);
        if (dragAxis_ < 0) return false;
        dragInverse_ = inverse_;
        dragOrigin_ = origin_;
        dragSize_ = size_;
        const auto coordinate = AxisCoordinate(mouse);
        if (!coordinate) { EndDrag(); return false; }
        dragStart_ = *coordinate;
        return true;
    }

    std::optional<QVector3D> DragOffset(QPointF mouse) const
    {
        if (!IsDragging()) return std::nullopt;
        const auto coordinate = AxisCoordinate(mouse);
        if (!coordinate) return std::nullopt;
        return Direction(dragAxis_) * (*coordinate - dragStart_);
    }

    bool IsDragging() const { return dragAxis_ >= 0; }
    void EndDrag() { dragAxis_ = -1; }

private:
    static QVector3D Direction(int axis) { QVector3D value; value[axis] = 1; return value; }

    QPointF ScreenPoint(QVector3D ndc) const
    {
        return {(ndc.x() + 1) * size_.width() * .5, (1 - ndc.y()) * size_.height() * .5};
    }

    static QVector3D Unproject(QPointF mouse, float depth, const QMatrix4x4& inverse, QSize size)
    {
        return (inverse * QVector4D(float(mouse.x() * 2 / size.width() - 1),
            float(1 - mouse.y() * 2 / size.height()), depth, 1)).toVector3DAffine();
    }

    std::optional<float> AxisCoordinate(QPointF mouse) const
    {
        const auto near = Unproject(mouse, -1, dragInverse_, dragSize_);
        const auto far = Unproject(mouse, 1, dragInverse_, dragSize_);
        const auto ray = (far - near).normalized();
        const auto axis = Direction(dragAxis_);
        const auto offset = near - dragOrigin_;
        const float parallel = QVector3D::dotProduct(axis, ray);
        const float denominator = 1 - parallel * parallel;
        if (denominator < .0001f) return std::nullopt;
        // Closest point between the cursor ray and the selected world-axis line.
        return (QVector3D::dotProduct(axis, offset) - parallel * QVector3D::dotProduct(ray, offset)) / denominator;
    }

    QSize size_, dragSize_;
    QVector3D origin_, dragOrigin_;
    QMatrix4x4 inverse_, dragInverse_;
    QPointF center_;
    std::array<QPointF, 3> tips_{};
    std::array<bool, 3> visible_{};
    int dragAxis_ = -1;
    float dragStart_ = 0;
};
