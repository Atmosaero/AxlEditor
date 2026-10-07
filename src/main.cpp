#include "Editor/EditorWindow.h"
#include "Editor/EditorTheme.h"
#include "Reference/Inspector/CommentSection.h"
#include "Reference/Assets/ReferenceAssetProvider.h"
#include "Reference/Scene/ReferenceSceneProvider.h"
#include "Reference/Viewport/ReferenceViewport.h"
#include "Modules/Console/ConsoleModule.h"
#include "Modules/ScriptCanvas/ScriptCanvasModule.h"
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
    const ObjectId cube = scene.CreateObject("Cube");
    auto transform = scene.GetTransform(cube).value();
    transform.position = {0.0f, 0.5f, 0.0f};
    scene.SetTransform(cube, transform);
    // Replace this instance with an external engine's IAssetProvider adapter.
    ReferenceAssetProvider assets(QCoreApplication::applicationDirPath() + "/../Assets");
    auto* viewport = new ReferenceViewport(scene);
    EditorWindow::ModuleList modules;
    modules.push_back(std::make_unique<ConsoleModule>());
    modules.push_back(std::make_unique<ScriptCanvasModule>());
    EditorWindow window(scene, assets, viewport, &scene, std::move(modules));
    RegisterCommentSection(window.PropertySections(), scene);
    window.show();
    return app.exec();
}
