#include "Editor/Qt/EditorWindow.h"
#include "Editor/Qt/EditorTheme.h"
#include "Reference/Qt/CommentSection.h"
#include "Reference/Qt/ReferenceAssetProvider.h"
#include "Reference/Qt/SceneDocument.h"
#include "Reference/Qt/AssetLinkSection.h"
#include "Reference/Qt/StartupCommands.h"
#include "Reference/Scene/ReferenceSceneProvider.h"
#include "Reference/Qt/Viewport/ReferenceViewport.h"
#include "Tools/Console/ConsoleTool.h"
#include "Tools/ScriptCanvas/ScriptCanvasTool.h"
#include "Tools/PythonConsole/PythonConsoleTool.h"
#include "Reference/Qt/PythonEditorCommands.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QTextStream>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
void PrintStartupMessage(const QString& text, bool error = false)
{
#ifdef Q_OS_WIN
    // WIN32 executables have no console by default. Preserve redirected handles
    // (for launchers/tests); attach to the parent's console when launched there.
    const auto channel = error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
    auto handle = GetStdHandle(channel);
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) handle = GetStdHandle(channel);
    }
    if (handle && handle != INVALID_HANDLE_VALUE) {
        const auto bytes = text.toUtf8(); DWORD written = 0;
        WriteFile(handle, bytes.constData(), DWORD(bytes.size()), &written, nullptr);
    }
#else
    QTextStream(error ? stderr : stdout) << text;
#endif
}
}

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
    StartupCommands startup;
    if (!startup.Parse(app.arguments())) {
        PrintStartupMessage("Axl Editor: " + startup.Error() + "\nUse --help for usage.\n", true);
        return 2;
    }
    if (startup.HelpRequested()) { PrintStartupMessage(startup.HelpText()); return 0; }
    if (startup.VersionRequested()) { PrintStartupMessage("Axl Editor " + app.applicationVersion() + "\n"); return 0; }
    ApplyDarkTheme(app);

    // Runtime integration point: replace the provider and supply its viewport widget.
    ReferenceSceneProvider scene;
    // Replace this instance with an external engine's IAssetProvider adapter.
    ReferenceAssetProvider assets("");
    auto* viewport = new ReferenceViewport(scene);
    EditorWindow::ToolList tools;
    tools.push_back(std::make_unique<ConsoleTool>());
    tools.push_back(std::make_unique<ScriptCanvasTool>());
    tools.push_back(std::make_unique<PythonConsoleTool>());
    EditorWindow window(scene, assets, viewport, &scene, std::move(tools));
    RegisterCommentSection(window.PropertySections(), scene);
    SceneDocument document(window, scene, assets);
    RegisterAssetLinkSection(window.PropertySections(), scene, assets, document.Session());
    RegisterPythonEditorCommands(window, document, scene);
    if (!startup.Apply(document)) {
        PrintStartupMessage("Axl Editor: " + startup.Error() + "\n", true);
        return 2;
    }
    window.show();
    return app.exec();
}
