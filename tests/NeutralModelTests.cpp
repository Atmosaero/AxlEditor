#include "Reference/Assets/AssetCatalog.h"
#include "Editor/Application/TransformGesture.h"
#include "Tools/ScriptCanvas/ScriptGraph.h"
#include "Editor/Application/EditorOperations.h"
#include "Editor/Application/EditorSession.h"
#include "Reference/Scene/ReferenceSceneProvider.h"
#include <iostream>
#include <stdexcept>

static void Check(bool value) { if (!value) throw std::runtime_error("Neutral model check failed"); }
int main()
{
    try {
        AssetCatalog catalog;
        catalog.Refresh({{"a.png", "a", "PNG"}, {"b.xyz", "b", "xyz"}});
        const auto id = catalog.GetAssets().front().id;
        Check(catalog.GetAsset(id)->type == "Texture");
        Check(catalog.GetAssets().back().type == "Unknown");
        catalog.Refresh({{"b.xyz", "b", "xyz"}});
        Check(!catalog.GetAsset(id));
        catalog.Refresh({{"a.png", "a", "png"}});
        Check(catalog.GetAssets().front().id == id);
        using namespace ScriptGraph;
        Graph graph;
        const auto start = graph.AddNode(NodeType::Start, {10, 20});
        const auto print = graph.AddNode(NodeType::Print, {100, 20});
        const auto output = graph.Pins()[0].id, input = graph.Pins()[1].id;
        Check(!graph.Connect(input, output));
        Check(graph.Connect(output, input));
        Check(!graph.Connect(output, input));
        graph.SetPosition(print, {140, 80});
        Check(graph.Nodes()[1].position == Position{140, 80});
        graph.RemoveNode(start);
        Check(graph.Connections().empty() && !graph.FindPin(output));
        bool rejected = false;
        try { (void)Graph::FromData(graph.Nodes(), {{500, 999, PinDirection::Input, PinType::Execution}}, {}); }
        catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected);
        Transform transform;
        const auto moved = ApplyTransformGesture(transform, transform, TransformTool::Move, 0, 0, {3, 0, 0}, false);
        Check(moved.position.x == 3 && moved.position.y == 0);
        const auto rotated = ApplyTransformGesture(transform, transform, TransformTool::Rotate, 2, 90, {}, true);
        Check(rotated.rotation.z == 90 && rotated.rotation.x == 0);
        const auto scaled = ApplyTransformGesture(transform, transform, TransformTool::Scale, 3, 1, {}, true);
        Check(scaled.scale.x == 2 && scaled.scale.y == 2 && scaled.scale.z == 1);
        ReferenceSceneProvider scene;
        EditorOperations operations(scene);
        int edits = 0, notifications = 0;
        operations.committed = [&](const std::string&) { ++edits; };
        operations.changed = [&](EditorOperations::Change) { ++notifications; };
        const auto object = operations.Create("Parent");
        Check(object && operations.Selection() == object.object);
        const auto child = operations.Create("Child", object.object);
        Check(child && scene.GetParent(child.object) == object.object);
        Check(operations.Rename(child.object, "Renamed").success);
        Check(scene.GetName(child.object) == "Renamed");
        Check(operations.BeginTransformGesture(child.object));
        const int before = edits;
        Check(operations.SetTransform(child.object, moved).success);
        operations.CancelTransformGesture();
        Check(edits == before && scene.GetTransform(child.object)->position.x == 0);
        Check(operations.BeginTransformGesture(child.object));
        Check(operations.SetTransform(child.object, moved).success);
        Check(operations.Delete(object.object).success && scene.GetObjects().empty());
        Check(edits == before + 2 && !operations.HasTransformGesture()); // Transform commits before delete.
        Check(!operations.SetTransform(child.object, moved));
        const auto generation = operations.Generation();
        operations.ResetSession();
        Check(!operations.IsCurrent(generation) && operations.Selection() == 0 && notifications > edits);
        EditorSession session;
        Check(!session.HasProject());
        session.OpenProject("project", "Example");
        Check(session.HasProject() && session.scenePath.empty());
        session.scenePath = "scene.axl";
        session.NewScene();
        Check(session.projectRoot == "project" && session.scenePath.empty());
        std::cout << "Neutral models passed without Qt\n";
    } catch (const std::exception& error) { std::cerr << error.what(); return 1; }
}
