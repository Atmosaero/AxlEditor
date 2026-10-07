#include "Reference/Qt/SceneDocument.h"
#include "Reference/Qt/CommentSection.h"
#include "Reference/Qt/AssetLinkSection.h"
#include <QTemporaryDir>
#include <QtTest>

class DocumentTests final : public QObject {
    Q_OBJECT
private slots:
    void includedExampleProjectLoads() {
        const auto path = QFINDTESTDATA("../Example/Assets/example.axl"); QVERIFY(!path.isEmpty());
        auto root = QFileInfo(path).dir(); QVERIFY(root.cdUp());
        ReferenceSceneProvider scene; ReferenceAssetProvider assets("");
        EditorWindow window(scene, assets, nullptr, &scene); SceneDocument document(window, scene, assets);
        QVERIFY(document.OpenProject(root.absolutePath())); QVERIFY(document.Open(path));
        QCOMPARE(scene.GetObjects().size(), std::size_t{2}); QCOMPARE(scene.GetParent(2), ObjectId{1});
        QCOMPARE(scene.GetAssetLink(1, 2), std::string("Assets/demo.lua"));
        QVERIFY(!document.IsDirty()); QCOMPARE(assets.GetAssets().size(), std::size_t{3});
    }
    void referenceAssetHandlersOpenSceneAndTexture() {
        QTemporaryDir project; QVERIFY(QDir(project.path()).mkpath("Assets"));
        QImage image(2, 3, QImage::Format_ARGB32); image.fill(Qt::blue);
        QVERIFY(image.save(project.filePath("Assets/texture.png")));
        ReferenceSceneProvider source; source.CreateObject("From asset");
        SceneCodec::Write(project.filePath("Assets/scene.axl"), SceneCodec::Encode(source.GetObjects()));
        ReferenceSceneProvider scene; ReferenceAssetProvider assets("");
        EditorWindow window(scene, assets, nullptr, &scene); SceneDocument document(window, scene, assets);
        QVERIFY(document.OpenProject(project.path()));
        auto* handlers = window.Context().GetService<AssetOpenHandlers>(); QVERIFY(handlers);
        for (const auto& asset : assets.GetAssets()) {
            QVERIFY(handlers->Open(asset));
            if (asset.type == "Scene") QCOMPARE(SceneCodec::Encode(scene.GetObjects()), SceneCodec::Encode(source.GetObjects()));
        }
        auto* preview = window.findChild<QDialog*>("AssetImagePreview"); QVERIFY(preview);
        QCOMPARE(preview->findChild<QLabel*>()->pixmap().size(), QSize(2, 3)); preview->close();
        QVERIFY(!document.IsDirty());
    }
    void relativeAssetLinkSurvivesMovingProjectAndMissingResource() {
        QTemporaryDir first, second; QVERIFY(QDir(first.path()).mkpath("Assets")); QVERIFY(QDir(second.path()).mkpath("Assets"));
        SceneCodec::Write(first.filePath("Assets/test.lua"), "-- test");
        ReferenceSceneProvider scene; ReferenceAssetProvider assets(""); EditorWindow window(scene, assets, nullptr, &scene);
        SceneDocument document(window, scene, assets); document.decide = [] { return SceneDocument::Decision::Discard; };
        RegisterAssetLinkSection(window.PropertySections(), scene, assets, document.Session());
        QVERIFY(document.OpenProject(first.path())); const auto id = window.Operations().Create("Linked").object;
        PropertyGroupId group = 0;
        QVERIFY(window.Operations().Edit("Asset Link", [&] {
            group = scene.AddPropertyGroup(id, "AssetLink"); scene.SetAssetLink(id, group, "Assets/test.lua");
        }).success);
        QVERIFY(document.SaveAs(first.filePath("scene.axl")));
        QVERIFY(QFile::copy(first.filePath("scene.axl"), second.filePath("scene.axl")));
        QVERIFY(QFile::copy(first.filePath("Assets/test.lua"), second.filePath("Assets/test.lua")));
        QVERIFY(document.OpenProject(second.path())); QVERIFY(document.Open(second.filePath("scene.axl")));
        QVERIFY(window.Operations().Select(id).success);
        QCOMPARE(scene.GetAssetLink(id, group), std::string("Assets/test.lua"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto* label = window.findChild<QLabel*>("AssetLinkPath"); QVERIFY(label); QVERIFY(!label->text().contains("missing"));
        QVERIFY(QFile::remove(second.filePath("Assets/test.lua"))); window.RefreshAssets(); QVERIFY(label->text().contains("missing"));
        QCOMPARE(scene.GetAssetLink(id, group), std::string("Assets/test.lua"));
    }
    void sceneRoundtripAndSafeReplacement() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        ReferenceSceneProvider scene; ReferenceAssetProvider assets("");
        EditorWindow window(scene, assets, nullptr, &scene);
        RegisterCommentSection(window.PropertySections(), scene);
        SceneDocument document(window, scene, assets);
        document.decide = [] { return SceneDocument::Decision::Discard; };
        QVERIFY(!document.Session().HasProject());
        QVERIFY(document.OpenProject(dir.path()));
        auto& operations = window.Operations();
        const auto parent = operations.Create(QString::fromUtf8("Родитель").toStdString()).object;
        const auto child = operations.Create("Child", parent).object;
        Transform transform; transform.position = {2, 3, 4}; transform.rotation = {20, 30, 40}; transform.scale = {-1, 0, 2};
        QVERIFY(operations.SetTransform(child, transform).success);
        const auto group = scene.AddPropertyGroup(child, "Comment");
        QVERIFY(operations.Edit("Comment", [&] { scene.SetCommentText(child, group, QString::fromUtf8("Привет\nSecond line").toStdString()); }).success);
        QVERIFY(document.IsDirty());
        const auto original = SceneCodec::Encode(scene.GetObjects());
        const auto path = dir.filePath("scene.axl");
        QVERIFY(document.SaveAs(path)); QVERIFY(!document.IsDirty());
        window.PropertySections().Refresh();
        auto* retired = window.findChild<QPlainTextEdit*>("CommentText");
        QVERIFY(retired);
        const auto generation = operations.Generation();
        QVERIFY(document.NewScene()); QVERIFY(scene.GetObjects().empty());
        QVERIFY(!operations.IsCurrent(generation));
        QVERIFY(document.Open(path));
        QCOMPARE(SceneCodec::Encode(scene.GetObjects()), original);
        QCOMPARE(scene.GetParent(child), parent);
        retired->setPlainText("A retired callback must not change a newly loaded object with the same ID");
        QCOMPARE(SceneCodec::Encode(scene.GetObjects()), original);
        QVERIFY(operations.Rename(child, "Unsaved").success); QVERIFY(document.IsDirty());
        document.decide = [] { return SceneDocument::Decision::Cancel; };
        QVERIFY(!document.NewScene()); QCOMPARE(scene.GetName(child), std::string("Unsaved"));
        QVERIFY(!document.OpenProject(dir.path())); QVERIFY(document.IsDirty());
        QVERIFY(!document.SaveAs(dir.filePath("missing/failure.axl"))); QVERIFY(document.IsDirty());
        QVERIFY(!document.lastError.isEmpty());
    }
    void rejectsCorruptScenesWithoutChangingCurrentDocument() {
        QTemporaryDir dir;
        ReferenceSceneProvider scene; ReferenceAssetProvider assets("");
        EditorWindow window(scene, assets, nullptr, &scene);
        SceneDocument document(window, scene, assets);
        const auto id = window.Operations().Create("Keep me").object;
        const auto initial = SceneCodec::Encode(scene.GetObjects());
        auto root = QJsonDocument::fromJson(initial).object();
        const auto fail = [&](const QJsonObject& corrupt) {
            const auto path = dir.filePath("bad.axl");
            SceneCodec::Write(path, QJsonDocument(corrupt).toJson());
            QVERIFY(!document.Open(path));
            QCOMPARE(SceneCodec::Encode(scene.GetObjects()), initial);
            QCOMPARE(window.Operations().Selection(), id);
            QVERIFY(document.IsDirty());
        };
        auto version = root; version["version"] = 2; fail(version);
        auto duplicate = root; auto array = root["objects"].toArray(); array.append(array.first()); duplicate["objects"] = array; fail(duplicate);
        auto object = root["objects"].toArray().first().toObject(); object["parent"] = "999";
        auto missing = root; missing["objects"] = QJsonArray{object}; fail(missing);
        object["parent"] = QString::number(id); auto cycle = root; cycle["objects"] = QJsonArray{object}; fail(cycle);
        object["parent"] = "0"; auto transform = object["transform"].toObject(); transform["position"] = QJsonArray{1e100, 0, 0}; object["transform"] = transform;
        auto nonFinite = root; nonFinite["objects"] = QJsonArray{object}; fail(nonFinite);
    }
    void historyRestoresSubtreesPropertiesAndCleanState() {
        QTemporaryDir dir;
        ReferenceSceneProvider scene; ReferenceAssetProvider assets("");
        EditorWindow window(scene, assets, nullptr, &scene);
        SceneDocument document(window, scene, assets);
        document.decide = [] { return SceneDocument::Decision::Discard; };
        auto& operations = window.Operations();
        const auto parent = operations.Create("Parent").object;
        const auto child = operations.Create("Child", parent).object;
        PropertyGroupId group = 0;
        QVERIFY(operations.Edit("Add Comment", [&] { group = scene.AddPropertyGroup(child, "Comment"); }).success);
        QVERIFY(operations.Edit("Comment text", [&] { scene.SetCommentText(child, group, "Remember me"); }).success);
        QVERIFY(document.SaveAs(dir.filePath("saved.axl")));
        const auto saved = SceneCodec::Encode(scene.GetObjects());
        QVERIFY(!document.IsDirty());
        QVERIFY(operations.Delete(parent).success); QVERIFY(scene.GetObjects().empty()); QVERIFY(document.IsDirty());
        document.History().undo(); QCOMPARE(SceneCodec::Encode(scene.GetObjects()), saved); QVERIFY(!document.IsDirty());
        document.History().redo(); QVERIFY(scene.GetObjects().empty());
        document.History().undo();
        const int count = document.History().count();
        QVERIFY(operations.BeginTransformGesture(child));
        auto transform = *scene.GetTransform(child); transform.position.x = 3;
        QVERIFY(operations.SetTransform(child, transform).success);
        transform.position.x = 8; QVERIFY(operations.SetTransform(child, transform).success);
        QCOMPARE(document.History().count(), count);
        operations.CommitTransformGesture();
        QCOMPARE(scene.GetTransform(child)->position.x, 8.f);
        QCOMPARE(document.History().index(), count); // Redo branch replaced by one transform command.
        document.History().undo(); QCOMPARE(scene.GetTransform(child)->position.x, 0.f); QVERIFY(!document.IsDirty());
        document.History().redo(); QCOMPARE(scene.GetTransform(child)->position.x, 8.f);
        const auto index = document.History().index();
        QVERIFY(operations.BeginTransformGesture(child)); transform.position.x = 50;
        QVERIFY(operations.SetTransform(child, transform).success); operations.CancelTransformGesture();
        QCOMPARE(scene.GetTransform(child)->position.x, 8.f); QCOMPARE(document.History().index(), index);
        QVERIFY(document.NewScene()); QCOMPARE(document.History().count(), 0); QVERIFY(!document.IsDirty());
    }
};
QTEST_MAIN(DocumentTests)
#include "DocumentTests.moc"
