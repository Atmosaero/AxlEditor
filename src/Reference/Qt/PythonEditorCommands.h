#pragma once
class EditorWindow;
class SceneDocument;
class ReferenceSceneProvider;

// Reference application composition, not a dependency of the Python tool.
void RegisterPythonEditorCommands(EditorWindow&, SceneDocument&, ReferenceSceneProvider&);
