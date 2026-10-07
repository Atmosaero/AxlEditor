#include "Reference/Qt/Viewport/TransformGizmo.h"
#include "Editor/Qt/EditorTheme.h"
#include <QPainter>

void TranslateGizmo::Draw(QPainter& painter, int hover) const
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

void TransformGizmo::Draw(QPainter& painter, int hover) const
{
        if (tool_ == TransformTool::Move) { move_.Draw(painter, hover); return; }
        painter.setRenderHint(QPainter::Antialiasing);
        const auto& theme = DarkTheme();
        const QColor colors[] = {theme.axisX, theme.axisY, theme.axisZ};
        const char* labels[] = {"X", "Y", "Z"};
        for (int axis = 0; axis < 3; ++axis) {
            if (!visible_[axis]) continue;
            const auto color = axis == (IsDragging() ? dragAxis_ : hover) ? theme.warning : colors[axis];
            painter.setPen(QPen(theme.gizmoOutline, 5));
            if (tool_ == TransformTool::Rotate) painter.drawPolyline(rings_[axis]);
            else painter.drawLine(center_, tips_[axis]);
            painter.setPen(QPen(color, 2.5));
            QPointF label;
            if (tool_ == TransformTool::Rotate) {
                painter.drawPolyline(rings_[axis]);
                label = rings_[axis][9 + axis * 12];
            } else {
                painter.drawLine(center_, tips_[axis]);
                painter.setBrush(color);
                painter.setPen(QPen(theme.gizmoOutline, 1));
                painter.drawRect(QRectF(tips_[axis] - QPointF(5, 5), QSizeF(10, 10)));
                label = tips_[axis];
            }
            const auto radial = label - center_;
            painter.setPen(color);
            painter.drawText(label + radial * (10 / std::max(1., std::sqrt(LengthSquared(radial)))) + QPointF(-4, 4), labels[axis]);
        }
        if (tool_ == TransformTool::Scale && AnyVisible()) {
            painter.setPen(QPen(theme.gizmoOutline, 1));
            painter.setBrush((IsDragging() ? dragAxis_ : hover) == 3 ? theme.warning : theme.gizmoCenter);
            painter.drawRect(QRectF(center_ - QPointF(5, 5), QSizeF(10, 10)));
        }
    }
