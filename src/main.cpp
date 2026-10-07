#include "Editor/Qt/EditorWindow.h"
#include "Editor/Qt/EditorTheme.h"
#include "Reference/Qt/CommentSection.h"
#include "Reference/Qt/ReferenceAssetProvider.h"
#include "Reference/Qt/SceneDocument.h"
#include "Reference/Qt/AssetLinkSection.h"
#include "Reference/Scene/ReferenceSceneProvider.h"
#include "Reference/Qt/Viewport/ReferenceViewport.h"
#include "Tools/Console/ConsoleTool.h"
#include "Tools/ScriptCanvas/ScriptCanvasTool.h"
#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char* argv[])
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    QApplication::setApplicationName("AxlEditor");
    QApplication::setOrganizationName("Axl");
    QApplication::setApplicationVersion("0.1.0");
    ApplyDarkTheme(app);

    // Runtime integration point: replace the provider and supply its viewport widget.
    ReferenceSceneProvider scene;
    // Replace this instance with an external engine's IAssetProvider adapter.
    ReferenceAssetProvider assets("");
    auto* viewport = new ReferenceViewport(scene);
    EditorWindow::ToolList tools;
    tools.push_back(std::make_unique<ConsoleTool>());
    tools.push_back(std::make_unique<ScriptCanvasTool>());
    EditorWindow window(scene, assets, viewport, &scene, std::move(tools));
    RegisterCommentSection(window.PropertySections(), scene);
    SceneDocument document(window, scene, assets);
    RegisterAssetLinkSection(window.PropertySections(), scene, assets, document.Session());
    window.show();
    return app.exec();
}
