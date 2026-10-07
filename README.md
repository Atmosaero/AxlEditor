# Axl Editor

A small, extensible editor built with C++17 and Qt Widgets. Axl brings together
scene selection, an Inspector, a 2D/3D viewport and independent editor tools,
with replaceable providers for engine-specific data.

![Axl Editor with the reference 3D viewport, rotation gizmo and dockable panels](docs/screenshots/axl-editor.png)

## Features

- Dockable Hierarchy, Inspector, Assets and Console panels, with saved window layouts.
- Scene and Game tabs; Game currently displays a placeholder.
- One viewport with perspective 3D and orthographic XY modes, a grid, reference cubes
  and Move / Rotate / Scale gizmos.
- Live Transform editing through the Inspector and viewport handles.
- Optional Comment properties with a multiline editor and extensible Inspector sections.
- A file-based reference asset provider and console messages for asset activation.
- Console messages for information, warnings, errors and success.
- Console command input, history and scoped registration from any C++ class.
- A separate Script Canvas tool with Start/Print nodes, execution pins and Bezier connections.
- A minimal dark theme and a centered Play/Stop toolbar.

This is an editor prototype. Play/Stop currently change shell state and log requests;
Script Canvas stores an editable graph in memory and does not execute it.
The reference viewport draws cubes rather than implementing a production renderer.

## Build on Windows

Requirements:

- Qt 6.12 or newer, with the `msvc2022_64` desktop kit and Qt SVG module.
- Visual Studio with the **Desktop development with C++** workload.
- CMake 3.22 or newer and Ninja available in `PATH`.
- An OpenGL 3.3-capable graphics driver for the reference viewport.

From PowerShell in the repository root:

```powershell
.\build.ps1 -QtRoot 'C:\Qt\6.12.0\msvc2022_64' -Run
```

The script initializes the MSVC environment, builds a Release executable with Ninja
and deploys the Qt runtime into `build/`. Omit `-Run` to build without launching.
Without `-QtRoot`, it uses `.tools/Qt/6.12.0/msvc2022_64`.

Create an `Assets/` folder in the repository root to try the reference asset browser.
It scans recursively and recognizes these extensions:

| Type | Extensions |
| --- | --- |
| Texture | `.png`, `.jpg` |
| Mesh | `.obj`, `.glb` |
| Scene | `.axl`, `.json` |
| Script | `.lua` |
| Unknown | All other files |

Use **Refresh** to rescan. Double-clicking an asset logs its metadata to the Console;
it does not import or load runtime assets.

## Editor controls

Select an object in **Hierarchy** to edit its name and Transform in **Inspector**.
Select **Move**, **Rotate** or **Scale** in the Scene toolbar. Move uses world-axis
arrows; Rotate uses colored rings for local Euler components; Scale uses local-axis
square handles. The center square scales uniformly while preserving proportions.
In 2D mode, Move/Scale use X/Y and Rotate uses Z. Uniform 2D scale leaves Z unchanged.
Axis scale changes one component additively, allowing zero scales to be restored.
Negative scales are supported. Handles account for parent transforms; manipulation
is disabled under a singular parent transform. Escape ends a gesture at its current value.
Transform fields have colored X/Y/Z labels on their left. Drag a label horizontally
to change its value, or type a number directly into the field.
Use **+ Add Component** to add a Comment; each reference object supports one Comment.
Its icon appears in the Add Component menu and the Comment section header.
Component SVGs live in `resources/icons/components/` and are embedded via Qt resources.

| Input | 3D mode | 2D mode |
| --- | --- | --- |
| Right mouse button + mouse | Rotate the camera | Pan the camera |
| WASD while the viewport has focus | Move the camera | Pan in XY |
| Mouse wheel | Change movement speed | Zoom |
| Left-drag a tool handle | Move/Rotate/Scale X, Y or Z | Move/Scale X/Y, Rotate Z |
| Escape | End an active drag or navigation gesture | End an active drag or navigation gesture |

The compact Scene toolbar contains **2D / 3D**, camera speed settings and a **?**
control reference. **View -> Refresh Scene** (`F5`) pulls external scene changes.
Window geometry and dock layout are saved on close in `editor-layout.ini` beside
the executable. **View -> Save Layout** and **Reset Layout** are also available.

### Script Canvas

Open **Modules -> Script Canvas**, then right-click the canvas and choose
**Add Node -> Start** or **Print**. Nodes can be selected, dragged and deleted.
Drag an output execution pin to an input pin to create a connection; select a
connection and press **Delete** to remove it. **Escape** cancels a pending connection.

Each execution input accepts one incoming connection. Outputs may connect to
multiple inputs. Invalid directions, occupied inputs, duplicate connections and
unsupported pin types are rejected without replacing existing connections.
Deleting a node removes its pins and incident connections.

The model and widget support multiple pins per node, with separate rows on each
side. The menu still creates only the two simple node types with one pin each.
Graphs are not saved between editor sessions.

## Architecture and integration

`src/main.cpp` is the composition root: it creates the application, reference
providers, viewport and the ordered list of editor modules. `EditorWindow` owns
the shell, panels and supplied modules through `std::unique_ptr<IEditorModule>`.
`EditorTheme` contains named UI colors; the immutable `DarkTheme()` preset is
shared by the application palette, stylesheet, viewport, gizmo, Console and
Script Canvas. Change colors in `src/Editor/EditorTheme.h`; styling stays in
`src/Editor/EditorTheme.cpp`. There is no theme editor or runtime theme switching.

```text
src/
  main.cpp                         Application composition
  Contracts/                       Transform and scene/asset/property contracts
  Editor/                          Window, context, Inspector, theme, interfaces and ConsoleCommands
  Modules/
    Console/                       ConsoleModule and EditorConsole
    ScriptCanvas/                  Module lifecycle, graph view and ScriptGraph
  Reference/
    Scene/                         Scene and ReferenceSceneProvider
    Assets/                        ReferenceAssetProvider
    Viewport/                      ReferenceViewport, TranslateGizmo and TransformGizmo
    Inspector/                     CommentSection
```

All targets use `src` as their include root. Includes show their directory,
for example `Contracts/ISceneProvider.h` or `Editor/IEditorModule.h`.

The scene, asset and property contracts use neutral IDs and standard C++ data.
They do not require an ECS, a Node hierarchy implementation or Qt math types.

- **Scene:** implement `ISceneProvider` for hierarchy, names, optional Transforms
  and object creation/deletion. `Transform` uses `Vec3 { float x, y, z; }`.
  `GetTransform` returns `std::nullopt` for a live object without a Transform;
  invalid object access throws `std::out_of_range`. `SetTransform` edits an
  existing Transform and throws `std::logic_error` when one is absent.
- **Assets:** implement `IAssetProvider` to supply asset metadata and refresh it.
  `ReferenceAssetProvider` is a replaceable filesystem example, with IDs stable
  for each path during its lifetime.
- **Properties:** implement `IObjectProperties` to expose optional property groups.
  Register widget factories and refresh callbacks with `InspectorProperties`.
  `CommentSection.h` demonstrates an editor bound to reference data; a foreign
  backend can register different sections without using Axl's Comment type.
- **Viewport:** pass a runtime's `QWidget` to `EditorWindow`. Implement
  `IEditorViewport` on that widget to receive selection and camera controls,
  then emit `EditorViewportEvents` for Inspector updates and Console errors.
  A plain widget is also accepted, with the shell's camera controls disabled.
  External viewports can opt into the tool selector with `SupportsTransformTools`,
  `ActiveTransformTool` and `SetTransformTool`, emitting `TransformToolChanged`
  when the tool changes. Existing adapters keep working without these overrides.
- **Modules:** implement `IEditorModule::Initialize(EditorContext&)` and `Shutdown()`.
  The context provides borrowed services and registration for docks and actions.
  Choose modules in `main.cpp` and move the list into `EditorWindow`. The shell
  initializes them before layout restoration and shuts them down in reverse order
  with the context still alive. Passing an empty list creates a shell without tools.
  Bottom-area docks are tabified with Assets by their Qt area, regardless of tool name.
  The shell logs through `IEditorLog`; ConsoleModule supplies the optional service.

### Console commands

Type a command in the Console input and press **Enter**. **Up/Down** navigate
the last 100 commands and preserve an unfinished draft. Built-in commands:

```text
help
echo "hello world"
add 2.5 -1
log warning "test warning"
log success "test success"
clear
```

Names are case-insensitive. Arguments accept single/double quotes, including
empty strings. Escape quotes and backslashes with a backslash; unquoted escaped
whitespace stays in one argument. Backslashes before other characters remain
literal, so Windows paths such as `C:\Assets\file.lua` work as arguments.
Unknown commands, invalid syntax/arguments and callback exceptions appear as
Error messages. Execution runs synchronously on the editor thread.

Include `Editor/ConsoleCommands.h` to register from an ordinary class or module.
Keep the returned handle as a member so registration lasts as long as its owner:

```cpp
#include "Editor/ConsoleCommands.h"
#include "Editor/EditorContext.h"

ConsoleCommands::Registration helloCommand; // Member of your class.

// In Initialize (ConsoleModule must already be initialized):
if (auto* commands = context.GetService<ConsoleCommands>()) {
    helloCommand = commands->RegisterCommand("hello", "Print a greeting",
        [](const QStringList& args, IEditorLog& output) {
            output.Log(ConsoleMessageType::Success, "Hello " + args.join(' '));
        });
    // An empty handle indicates a duplicate/invalid name or empty handler.
}

// In Shutdown, before destroying any captured state:
helloCommand.Reset();
```

Destroying or resetting the move-only handle unregisters the command. Handles
also become harmless when the registry/Console dock disappears. The registry
is available as a guarded QObject service; an application without ConsoleModule
has no command service. No OS shell or runtime scripting is invoked.

`ScriptCanvasView` owns a tool-local `ScriptGraph::Graph`. Graphics items remain
inside `Modules/ScriptCanvas/ScriptCanvasView.cpp`, with pin handles registered
explicitly by `PinId`. `ScriptCanvasModule.cpp` contains only registration and lifecycle.
The tool is independent of `ISceneProvider` and runtime scripting.

### Ownership and refresh

Providers and non-QObject services must outlive their consumers. The service
registry does not own them. QObject services registered by their concrete Qt type
are guarded by `QPointer`. The context and window menus must outlive initialized
modules. `EditorWindow` calls `Shutdown()` in reverse order and destroys owned
modules before its context and Qt children. Standalone module hosts must do the
same. Qt owns registered docks, and module actions belong to their dock;
shutdown deletes surviving module UI through guarded pointers. ConsoleModule
unregisters its borrowed `IEditorLog` service before destruction.

Provider calls and UI updates run synchronously on the editor thread. External
backends explicitly call `EditorWindow::RefreshScene`, `RefreshAssets` or
`InspectorProperties::Refresh` after changes. Their own field callbacks must
handle objects or groups disappearing. Unchanged Inspector sections retain their
widgets, focus and local undo; retired widgets stop emitting signals before deletion.

Project documentation, plans and development notes belong in this README;
do not commit separate notes or planning files. Change the application version
only when explicitly requested by the project owner.

No ECS, reflection, importer pipeline, runtime graph execution or serialization
framework is included.

## Tests

Install the Qt Test component with the SDK. In a Visual Studio developer shell,
enable tests and build:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH='C:\Qt\6.12.0\msvc2022_64' -DAXL_BUILD_TESTS=ON
.\build.ps1 -QtRoot 'C:\Qt\6.12.0\msvc2022_64'
$env:PATH = 'C:\Qt\6.12.0\msvc2022_64\bin;' + $env:PATH
ctest --test-dir build --output-on-failure
```

The seven test targets cover:

- Scene contracts compiled without Qt.
- Reference asset scans, UTF-8 paths, extension types, stable IDs and missing files.
- Service registration, module initialization/shutdown, logging and dock ownership.
- Command registration/lifetime, quoted arguments, errors/exceptions, Enter input,
  history, built-ins and registration from a non-QObject class.
- Foreign providers/viewports, objects without Transform, stale properties,
  Inspector focus/undo, layout recovery, generic module docks, reverse shutdown,
  startup without Script Canvas and asset activation/selection through a provider.
- Viewport capture, interrupted input, parent transforms, rotation/scale gestures,
  uniform 2D scale, zero-scale recovery and camera limits.
- Script Canvas pin IDs, multiple pins, connection rules, movement and deletion.

GUI tests require a desktop session and run serially to preserve input focus.
Viewport tests use the real OpenGL widget. Boundary-test layout files are isolated
under `build/tests/`; CTest uses SDK plugins on Windows.
Detailed reports are written to `build/tests/editor-boundaries.txt`,
`build/viewport-input.txt`, `build/script-canvas.txt` and `build/console-commands.txt`.

To repeat viewport tests at 200% scaling:

```powershell
$env:QT_SCALE_FACTOR = '2'
ctest --test-dir build -R editor_viewport_input --output-on-failure
Remove-Item Env:\QT_SCALE_FACTOR
```

Local SDKs, build output, logs and temporary recordings are excluded from Git.

## License

[MIT](LICENSE). Copyright (c) 2026 Atmosaero.
