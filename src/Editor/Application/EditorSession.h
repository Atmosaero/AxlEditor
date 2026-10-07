#pragma once
#include <string>
#include <utility>

// Document identity is separate from scene data and the user's window layout.
struct EditorSession
{
    std::string projectRoot;
    std::string projectName;
    std::string scenePath;
    std::string sceneName = "Untitled Scene";
    bool HasProject() const { return !projectRoot.empty(); }
    void OpenProject(std::string root, std::string name) {
        projectRoot = std::move(root); projectName = std::move(name); NewScene();
    }
    void NewScene() { scenePath.clear(); sceneName = "Untitled Scene"; }
};
