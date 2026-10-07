#include "Reference/Qt/Viewport/ReferenceViewport.h"
#include <QApplication>
#include <QMouseEvent>
#include <QtTest>
#include <cmath>
#include <limits>

namespace {
bool Show(ReferenceViewport& viewport) {
    viewport.resize(640, 480);
    viewport.show();
    viewport.activateWindow();
    viewport.setFocus();
    return QTest::qWaitForWindowExposed(&viewport) && QTest::qWaitForWindowActive(&viewport)
        && viewport.isValid();
}
void Move(ReferenceViewport& viewport, QPoint point, Qt::MouseButtons buttons) {
    QMouseEvent event(QEvent::MouseMove, point, viewport.mapToGlobal(point), Qt::NoButton, buttons, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &event);
}
QPoint XHandle(const ReferenceViewport& viewport) { return QPoint(viewport.width() / 2 + 55, viewport.height() / 2); }
QPointF Project(const QMatrix4x4& matrix, QVector3D point) {
    const auto ndc = (matrix * QVector4D(point, 1)).toVector3DAffine();
    return {(ndc.x() + 1) * 320., (1 - ndc.y()) * 240.};
}
}

class ViewportInputTests final : public QObject
{
    Q_OBJECT
private slots:
    void pickingFrameAndGizmoPriorityInBothModes() {
        for (bool twoD : {true, false}) {
            ReferenceSceneProvider scene;
            const auto id = scene.CreateObject("Cube");
            EditorOperations operations(scene);
            ReferenceViewport viewport(scene); viewport.BindOperations(&operations);
            viewport.Set2DMode(twoD);
            QVERIFY(Show(viewport));
            operations.Select(id); viewport.SetSelectedObject(id); viewport.FrameSelected();
            viewport.SetSelectedObject(0); operations.Select(0);
            QTest::mouseClick(&viewport, Qt::LeftButton, Qt::NoModifier, viewport.rect().center());
            QCOMPARE(operations.Selection(), id);
            QTest::mouseClick(&viewport, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
            QCOMPARE(operations.Selection(), ObjectId{0});
            if (twoD) {
                operations.Select(id); viewport.SetSelectedObject(id);
                const auto start = *scene.GetTransform(id);
                QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, XHandle(viewport));
                QCOMPARE(QWidget::mouseGrabber(), &viewport);
                Move(viewport, XHandle(viewport) + QPoint(30, 0), Qt::LeftButton);
                QCOMPARE(operations.Selection(), id);
                QTest::keyClick(&viewport, Qt::Key_Escape);
                QVERIFY(scene.GetTransform(id)->position == start.position);
            }
        }
    }
    void rotationRingsEditEachAxisAndAccumulateAcrossTheSeam() {
        for (int axis = 0; axis < 3; ++axis) {
            QVector3D direction, u, v;
            direction[axis] = 1;
            u[(axis + 1) % 3] = 1;
            v[(axis + 2) % 3] = 1;
            QMatrix4x4 projection, view, parent;
            projection.ortho(-20.f / 3, 20.f / 3, -5.f, 5.f, .05f, 1000.f);
            view.lookAt(direction * 100, {}, axis == 1 ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0));
            TransformGizmo gizmo;
            gizmo.SetTool(TransformTool::Rotate);
            gizmo.Update(projection * view, {640, 480}, {}, false, parent, Transform{});
            const float radius = 75.f * 10 / 480;
            const auto start = Project(projection * view, u * radius);
            QCOMPARE(gizmo.HitTest(start), axis);
            QVERIFY(gizmo.BeginDrag(start));
            const QVector3D quarterTurns[] = {v, -u, -v, u};
            for (int turn = 0; turn < 4; ++turn) {
                const auto amount = gizmo.DragAmount(Project(projection * view, quarterTurns[turn] * radius));
                QVERIFY(amount);
                QVERIFY(std::abs(*amount - (turn + 1) * 90) < .1f);
            }
            gizmo.EndDrag();
            QVERIFY(!gizmo.IsDragging());
            gizmo.Update(projection * view, {640, 480}, direction * 2000, false, parent, Transform{});
            QCOMPARE(gizmo.HitTest(start), -1);
        }
    }

    void rotationIn2DUpdatesOnlyZAndRenderedCube() {
        ReferenceSceneProvider scene;
        const auto id = scene.CreateObject("Cube");
        ReferenceViewport viewport(scene);
        viewport.Set2DMode(true);
        viewport.SetSelectedObject(id);
        viewport.SetTransformTool(TransformTool::Rotate);
        QVERIFY(Show(viewport));
        const auto before = viewport.grabFramebuffer();
        QSignalSpy edits(&viewport.Events(), &EditorViewportEvents::TransformEdited);
        const QPoint start(395, 240), end(373, 187);
        QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, start);
        QCOMPARE(QWidget::mouseGrabber(), &viewport);
        Move(viewport, end, Qt::LeftButton);
        QTest::mouseRelease(&viewport, Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(QWidget::mouseGrabber() != &viewport);
        const auto result = scene.GetTransform(id).value();
        QVERIFY(std::abs(result.rotation.z - 45) < .1f);
        QCOMPARE(result.rotation.x, 0.f);
        QCOMPARE(result.rotation.y, 0.f);
        QVERIFY(result.position == Vec3{});
        QVERIFY(result.scale == (Vec3{1, 1, 1}));
        QCOMPARE(edits.count(), 1);
        QVERIFY(viewport.grabFramebuffer() != before);
    }

    void axisScaleRecoversZeroAndUniform2DKeepsZ() {
        ReferenceSceneProvider scene;
        const auto id = scene.CreateObject("Cube");
        auto transform = scene.GetTransform(id).value();
        transform.scale = {0, -2, 3};
        scene.SetTransform(id, transform);
        ReferenceViewport viewport(scene);
        viewport.Set2DMode(true);
        viewport.SetSelectedObject(id);
        viewport.SetTransformTool(TransformTool::Scale);
        QVERIFY(Show(viewport));
        const QPoint handles[] = {{375, 240}, {320, 185}};
        const QPoint offsets[] = {{30, 0}, {0, -30}};
        for (int axis = 0; axis < 2; ++axis) {
            QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, handles[axis]);
            QCOMPARE(QWidget::mouseGrabber(), &viewport);
            Move(viewport, handles[axis] + offsets[axis], Qt::LeftButton);
            QTest::mouseRelease(&viewport, Qt::LeftButton, Qt::NoModifier, handles[axis] + offsets[axis]);
        }
        const auto scaled = scene.GetTransform(id).value();
        QVERIFY(std::abs(scaled.scale.x - 1.f / 3) < .001f);
        QVERIFY(std::abs(scaled.scale.y + 5.f / 3) < .001f);
        QCOMPARE(scaled.scale.z, 3.f);
        QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, QPoint(320, 240));
        QCOMPARE(QWidget::mouseGrabber(), &viewport);
        Move(viewport, {365, 240}, Qt::LeftButton);
        QTest::mouseRelease(&viewport, Qt::LeftButton, Qt::NoModifier, QPoint(365, 240));
        transform = scene.GetTransform(id).value();
        QVERIFY(std::abs(transform.scale.x - .5f) < .001f);
        QVERIFY(std::abs(transform.scale.y + 2.5f) < .001f);
        QCOMPARE(transform.scale.z, 3.f);
        QVERIFY(transform.position == Vec3{} && transform.rotation == Vec3{});
    }

    void localRotationAndScaleRespectNegativeParentAndRejectSingularParent() {
        ReferenceSceneProvider scene;
        const auto parent = scene.CreateObject("Parent");
        auto parentTransform = scene.GetTransform(parent).value();
        parentTransform.scale = {-2, 3, 1};
        scene.SetTransform(parent, parentTransform);
        const auto child = scene.CreateObject("Child", parent);
        ReferenceViewport viewport(scene);
        viewport.Set2DMode(true);
        viewport.SetSelectedObject(child);
        viewport.SetTransformTool(TransformTool::Rotate);
        QVERIFY(Show(viewport));
        QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, QPoint(245, 240));
        QCOMPARE(QWidget::mouseGrabber(), &viewport);
        Move(viewport, {320, 165}, Qt::LeftButton);
        QTest::mouseRelease(&viewport, Qt::LeftButton, Qt::NoModifier, QPoint(320, 165));
        QVERIFY(std::abs(scene.GetTransform(child)->rotation.z - 90) < .1f);
        viewport.SetTransformTool(TransformTool::Scale);
        QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, QPoint(320, 185));
        QCOMPARE(QWidget::mouseGrabber(), &viewport);
        Move(viewport, {320, 155}, Qt::LeftButton);
        QTest::mouseRelease(&viewport, Qt::LeftButton, Qt::NoModifier, QPoint(320, 155));
        const auto before = scene.GetTransform(child).value();
        QVERIFY(std::abs(before.scale.x - 4.f / 3) < .001f);
        QCOMPARE(before.scale.y, 1.f);
        QCOMPARE(before.scale.z, 1.f);
        QVERIFY(scene.GetTransform(parent)->scale == parentTransform.scale);
        parentTransform.scale.x = 0;
        scene.SetTransform(parent, parentTransform);
        QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, QPoint(320, 185));
        QVERIFY(QWidget::mouseGrabber() != &viewport);
        Move(viewport, {320, 155}, Qt::LeftButton);
        QVERIFY(scene.GetTransform(child)->scale == before.scale);
    }

    void newToolsStopOnCaptureLossAndModeChanges() {
        for (const auto tool : {TransformTool::Rotate, TransformTool::Scale}) {
            ReferenceSceneProvider scene;
            const auto id = scene.CreateObject("Cube");
            ReferenceViewport viewport(scene);
            viewport.Set2DMode(true);
            viewport.SetSelectedObject(id);
            viewport.SetTransformTool(tool);
            QVERIFY(Show(viewport));
            const auto point = tool == TransformTool::Rotate ? QPoint(395, 240) : XHandle(viewport);
            QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, point);
            QCOMPARE(QWidget::mouseGrabber(), &viewport);
            QWidget thief;
            thief.setAttribute(Qt::WA_ShowWithoutActivating);
            thief.show();
            thief.grabMouse();
            Move(viewport, point + QPoint(30, -30), Qt::LeftButton);
            QCOMPARE(QWidget::mouseGrabber(), &thief);
            QVERIFY(scene.GetTransform(id)->rotation == Vec3{});
            QVERIFY(scene.GetTransform(id)->scale == (Vec3{1, 1, 1}));
            thief.releaseMouse();
            thief.hide();
            viewport.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&viewport));
            QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, point);
            QCOMPARE(QWidget::mouseGrabber(), &viewport);
            viewport.SetTransformTool(TransformTool::Move);
            QVERIFY(QWidget::mouseGrabber() != &viewport);
            Move(viewport, point + QPoint(30, -30), Qt::LeftButton);
            QVERIFY(scene.GetTransform(id)->rotation == Vec3{});
            QVERIFY(scene.GetTransform(id)->scale == (Vec3{1, 1, 1}));
        }
    }

    void lostMouseCaptureEndsTranslation() {
        ReferenceSceneProvider scene;
        const auto id = scene.CreateObject("Cube");
        ReferenceViewport viewport(scene);
        viewport.Set2DMode(true);
        viewport.SetSelectedObject(id);
        QVERIFY(Show(viewport));
        const auto point = XHandle(viewport);
        QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(QWidget::mouseGrabber(), &viewport);
        QWidget other;
        other.setAttribute(Qt::WA_ShowWithoutActivating);
        other.show();
        other.grabMouse(); // Another widget takes capture without a mouse release.
        QCOMPARE(QWidget::mouseGrabber(), &other);
        Move(viewport, point + QPoint(30, 0), Qt::LeftButton);
        QVERIFY(scene.GetTransform(id)->position == Vec3{});
        QCOMPARE(QWidget::mouseGrabber(), &other);
        other.releaseMouse();
    }

    void missingReleaseDoesNotLeaveDragOrLookActive() {
        ReferenceSceneProvider scene;
        const auto id = scene.CreateObject("Cube");
        ReferenceViewport viewport(scene);
        viewport.Set2DMode(true);
        viewport.SetSelectedObject(id);
        QVERIFY(Show(viewport));
        const auto point = XHandle(viewport);
        QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(QWidget::mouseGrabber(), &viewport);
        Move(viewport, point + QPoint(30, 0), Qt::NoButton);
        QVERIFY(QWidget::mouseGrabber() != &viewport);
        QVERIFY(scene.GetTransform(id)->position == Vec3{});
        viewport.SetSelectedObject(InvalidObjectId);
        const auto before = viewport.grabFramebuffer();
        QTest::mousePress(&viewport, Qt::RightButton, Qt::NoModifier, QPoint(30, 30));
        Move(viewport, QPoint(90, 50), Qt::NoButton);
        QVERIFY(QWidget::mouseGrabber() != &viewport);
        QCOMPARE(viewport.grabFramebuffer(), before);
    }

    void escapeStopsLookAndHeldMovementKeys() {
        ReferenceSceneProvider scene;
        scene.CreateObject("Cube");
        ReferenceViewport viewport(scene);
        QVERIFY(Show(viewport));
        QTest::mousePress(&viewport, Qt::RightButton, Qt::NoModifier, QPoint(30, 30));
        QTest::keyPress(&viewport, Qt::Key_W);
        QTest::qWait(80);
        QTest::keyClick(&viewport, Qt::Key_Escape);
        QVERIFY(QWidget::mouseGrabber() != &viewport);
        const auto stopped = viewport.grabFramebuffer();
        QTest::qWait(100);
        Move(viewport, QPoint(90, 60), Qt::RightButton);
        QCOMPARE(viewport.grabFramebuffer(), stopped);
        QTest::keyRelease(&viewport, Qt::Key_W);
    }

    void deactivateAndHideStopNavigation() {
        ReferenceSceneProvider scene;
        scene.CreateObject("Cube");
        ReferenceViewport viewport(scene);
        QVERIFY(Show(viewport));
        QTest::mousePress(&viewport, Qt::RightButton, Qt::NoModifier, QPoint(30, 30));
        QTest::keyPress(&viewport, Qt::Key_D);
        QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&viewport, &deactivate);
        QVERIFY(QWidget::mouseGrabber() != &viewport);
        const auto stopped = viewport.grabFramebuffer();
        QTest::qWait(100);
        QCOMPARE(viewport.grabFramebuffer(), stopped);
        QTest::mousePress(&viewport, Qt::RightButton, Qt::NoModifier, QPoint(30, 30));
        viewport.hide();
        QVERIFY(QWidget::mouseGrabber() != &viewport);
    }

    void worldTranslationWithNegativeParentScaleAndSingularParent() {
        ReferenceSceneProvider scene;
        const auto parent = scene.CreateObject("Parent");
        auto transform = scene.GetTransform(parent).value();
        transform.rotation.z = 35;
        transform.scale = {-2, 3, 1};
        scene.SetTransform(parent, transform);
        const auto child = scene.CreateObject("Child", parent);
        ReferenceViewport viewport(scene);
        viewport.Set2DMode(true);
        viewport.SetSelectedObject(child);
        QVERIFY(Show(viewport));
        const auto point = XHandle(viewport);
        QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(QWidget::mouseGrabber(), &viewport);
        Move(viewport, point + QPoint(30, 0), Qt::LeftButton);
        QTest::mouseRelease(&viewport, Qt::LeftButton, Qt::NoModifier, point + QPoint(30, 0));
        QMatrix4x4 parentMatrix;
        parentMatrix.rotate(35, 0, 0, 1);
        parentMatrix.scale(-2, 3, 1);
        const auto local = scene.GetTransform(child)->position;
        const auto world = parentMatrix.map(QVector3D(local.x, local.y, local.z));
        QVERIFY(world.x() > .1f);
        QVERIFY(std::abs(world.y()) < .0001f && std::abs(world.z()) < .0001f);
        transform.scale.x = 0;
        scene.SetTransform(parent, transform);
        auto childTransform = scene.GetTransform(child).value();
        childTransform.position = {};
        scene.SetTransform(child, childTransform);
        QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(QWidget::mouseGrabber() != &viewport);
        Move(viewport, point + QPoint(30, 0), Qt::LeftButton);
        QVERIFY(scene.GetTransform(child)->position == Vec3{});
        QCOMPARE(viewport.grabFramebuffer().width(), qRound(viewport.width() * viewport.devicePixelRatioF()));
    }

    void gizmoRejectsEndOnAxisAndInvisibleOrigin() {
        TranslateGizmo gizmo;
        QMatrix4x4 projection, view;
        projection.ortho(-7.5f, 7.5f, -5.f, 5.f, .05f, 1000.f);
        view.lookAt({0, 0, 100}, {0, 0, 0}, {0, 1, 0});
        gizmo.Update(projection * view, {600, 400}, {}, true);
        QCOMPARE(gizmo.HitTest({355, 200}), 0);
        QCOMPARE(gizmo.HitTest({300, 145}), 1);
        QCOMPARE(gizmo.HitTest({300, 200}), -1);
        QVERIFY(gizmo.BeginDrag({355, 200}));
        const auto offset = gizmo.DragOffset({385, 220});
        QVERIFY(offset.has_value());
        QVERIFY(offset->x() > .1f && offset->y() == 0 && offset->z() == 0);
        gizmo.EndDrag();
        view.setToIdentity();
        view.lookAt({100, 0, 0}, {0, 0, 0}, {0, 1, 0});
        gizmo.Update(projection * view, {600, 400}, {}, false);
        QCOMPARE(gizmo.HitTest({355, 200}), -1); // X faces the camera directly.
        QVERIFY(!gizmo.BeginDrag({355, 200}));
        gizmo.Update(projection * view, {600, 400}, {2000, 0, 0}, false);
        QCOMPARE(gizmo.HitTest({300, 145}), -1); // Origin is outside the depth range.
    }

    void cameraSpeedIsFiniteAndBounded() {
        ReferenceSceneProvider scene;
        ReferenceViewport viewport(scene);
        viewport.SetNavigationSpeed(5);
        viewport.SetNavigationSpeed(std::numeric_limits<float>::infinity());
        viewport.SetNavigationSpeed(std::numeric_limits<float>::quiet_NaN());
        QCOMPARE(viewport.NavigationSpeed(), 5.f);
        viewport.SetNavigationSpeed(-1);
        QCOMPARE(viewport.NavigationSpeed(), .1f);
        viewport.SetNavigationSpeed(1000000);
        QCOMPARE(viewport.NavigationSpeed(), 100.f);
    }

    void idleMouseCaptureEventDoesNotCancelKeyboardNavigation() {
        ReferenceSceneProvider scene;
        scene.CreateObject("Cube");
        ReferenceViewport viewport(scene);
        QVERIFY(Show(viewport));
        const auto before = viewport.grabFramebuffer();
        QTest::keyPress(&viewport, Qt::Key_W);
        QEvent ungrab(QEvent::UngrabMouse); // Delayed notification after a completed mouse operation.
        QApplication::sendEvent(&viewport, &ungrab);
        const bool moved = QTest::qWaitFor([&] { return viewport.grabFramebuffer() != before; }, 1000);
        QTest::keyRelease(&viewport, Qt::Key_W);
        QVERIFY(moved);
    }
};

QTEST_MAIN(ViewportInputTests)
#include "ViewportInputTests.moc"
