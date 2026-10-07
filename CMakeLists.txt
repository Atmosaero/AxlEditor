cmake_minimum_required(VERSION 3.22)

project(AxlEditor VERSION 0.1.0 LANGUAGES CXX)

find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets OpenGLWidgets Svg)

qt_standard_project_setup()
qt_add_resources(EDITOR_ICON_SOURCES resources/editor-icons.qrc)
qt_add_executable(AxlEditor WIN32
    ${EDITOR_ICON_SOURCES}
    # Bootstrap and editor shell.
    src/main.cpp
    src/Editor/EditorWindow.h src/Editor/EditorWindow.cpp
    src/Editor/EditorTheme.h src/Editor/EditorTheme.cpp
    src/Editor/EditorContext.h src/Editor/IEditorModule.h
    src/Editor/IEditorViewport.h src/Editor/IEditorLog.h
    src/Editor/ConsoleCommands.h src/Editor/ConsoleCommands.cpp

    # Small editor-facing data contracts.
    src/Contracts/Transform.h src/Contracts/ISceneProvider.h
    src/Contracts/IAssetProvider.h src/Contracts/IObjectProperties.h

    # Replaceable reference backend and its property editor.
    src/Reference/Scene/Scene.h src/Reference/Scene/ReferenceSceneProvider.h
    src/Reference/Assets/ReferenceAssetProvider.h src/Reference/Inspector/CommentSection.h

    # Editor tools and modules.
    src/Editor/InspectorProperties.h src/Modules/Console/EditorConsole.h
    src/Modules/Console/ConsoleModule.h src/Modules/Console/ConsoleModule.cpp
    src/Modules/ScriptCanvas/ScriptCanvasModule.h src/Modules/ScriptCanvas/ScriptCanvasModule.cpp
    src/Modules/ScriptCanvas/ScriptCanvasView.h src/Modules/ScriptCanvas/ScriptCanvasView.cpp
    src/Modules/ScriptCanvas/ScriptGraph.h

    # Reference viewport; external backends may supply their own QWidget.
    src/Reference/Viewport/ReferenceViewport.h src/Reference/Viewport/ReferenceViewport.cpp
    src/Reference/Viewport/TranslateGizmo.h src/Reference/Viewport/TransformGizmo.h
)
target_include_directories(AxlEditor PRIVATE src)
target_compile_features(AxlEditor PRIVATE cxx_std_17)
target_link_libraries(AxlEditor PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg)

if(WIN32)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}" REQUIRED NO_DEFAULT_PATH)
    add_custom_command(TARGET AxlEditor POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --release --no-translations
            --add-plugin-types iconengines
            --no-compiler-runtime "$<TARGET_FILE:AxlEditor>"
        COMMENT "Deploying Qt runtime next to AxlEditor"
        VERBATIM
    )
endif()

# Optional focused checks for editor contracts, tools and module lifetime.
option(AXL_BUILD_TESTS "Build focused editor tests" OFF)
if(AXL_BUILD_TESTS)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(AxlSceneContractTests tests/SceneContractTests.cpp)
    set_target_properties(AxlSceneContractTests PROPERTIES AUTOMOC OFF AUTOUIC OFF)
    target_include_directories(AxlSceneContractTests PRIVATE src)
    target_compile_features(AxlSceneContractTests PRIVATE cxx_std_17)
    add_test(NAME scene_contracts_without_qt COMMAND AxlSceneContractTests)
    qt_add_executable(AxlAssetProviderTests tests/AssetProviderTests.cpp)
    target_include_directories(AxlAssetProviderTests PRIVATE src)
    target_compile_features(AxlAssetProviderTests PRIVATE cxx_std_17)
    target_link_libraries(AxlAssetProviderTests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME reference_asset_provider COMMAND AxlAssetProviderTests)
    qt_add_executable(AxlModuleLifecycleTests
        tests/ModuleLifecycleTests.cpp
        src/Modules/Console/ConsoleModule.h src/Modules/Console/ConsoleModule.cpp
        src/Editor/ConsoleCommands.h src/Editor/ConsoleCommands.cpp
        src/Modules/ScriptCanvas/ScriptCanvasModule.h src/Modules/ScriptCanvas/ScriptCanvasModule.cpp src/Modules/ScriptCanvas/ScriptCanvasView.cpp
    )
    target_include_directories(AxlModuleLifecycleTests PRIVATE src)
    target_compile_features(AxlModuleLifecycleTests PRIVATE cxx_std_17)
    target_link_libraries(AxlModuleLifecycleTests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME editor_module_lifecycle COMMAND AxlModuleLifecycleTests)

    qt_add_executable(AxlConsoleCommandTests
        tests/ConsoleCommandTests.cpp
        src/Editor/ConsoleCommands.h src/Editor/ConsoleCommands.cpp
        src/Modules/Console/ConsoleModule.h src/Modules/Console/ConsoleModule.cpp
    )
    target_include_directories(AxlConsoleCommandTests PRIVATE src)
    target_compile_features(AxlConsoleCommandTests PRIVATE cxx_std_17)
    target_link_libraries(AxlConsoleCommandTests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME editor_console_commands COMMAND AxlConsoleCommandTests
        -o "${CMAKE_CURRENT_BINARY_DIR}/console-commands.txt,txt")

    # Intentionally links no reference viewport / OpenGL code.
    qt_add_executable(AxlEditorBoundaryTests
        ${EDITOR_ICON_SOURCES}
        tests/EditorBoundaryTests.cpp src/Editor/IEditorViewport.h
        src/Editor/EditorTheme.h src/Editor/EditorTheme.cpp
        src/Editor/EditorWindow.h src/Editor/EditorWindow.cpp
        src/Modules/Console/ConsoleModule.h src/Modules/Console/ConsoleModule.cpp
        src/Editor/ConsoleCommands.h src/Editor/ConsoleCommands.cpp
        src/Modules/ScriptCanvas/ScriptCanvasModule.h src/Modules/ScriptCanvas/ScriptCanvasModule.cpp src/Modules/ScriptCanvas/ScriptCanvasView.cpp
    )
    target_include_directories(AxlEditorBoundaryTests PRIVATE src)
    target_compile_features(AxlEditorBoundaryTests PRIVATE cxx_std_17)
    target_link_libraries(AxlEditorBoundaryTests PRIVATE Qt6::Widgets Qt6::Test)
    # Isolate test layout files from the editor's saved workspace.
    set_target_properties(AxlEditorBoundaryTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
    add_test(NAME editor_backend_boundaries COMMAND AxlEditorBoundaryTests
        -o "${CMAKE_CURRENT_BINARY_DIR}/tests/editor-boundaries.txt,txt")

    qt_add_executable(AxlViewportInputTests
        tests/ViewportInputTests.cpp src/Editor/IEditorViewport.h
        src/Reference/Viewport/ReferenceViewport.h src/Reference/Viewport/ReferenceViewport.cpp
    )
    target_include_directories(AxlViewportInputTests PRIVATE src)
    target_compile_features(AxlViewportInputTests PRIVATE cxx_std_17)
    target_link_libraries(AxlViewportInputTests PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Test)
    add_test(NAME editor_viewport_input COMMAND AxlViewportInputTests
        -o "${CMAKE_CURRENT_BINARY_DIR}/viewport-input.txt,txt")
    qt_add_executable(AxlScriptCanvasTests
        tests/ScriptCanvasTests.cpp
        src/Modules/ScriptCanvas/ScriptCanvasView.h src/Modules/ScriptCanvas/ScriptCanvasView.cpp
        src/Modules/ScriptCanvas/ScriptGraph.h
    )
    target_include_directories(AxlScriptCanvasTests PRIVATE src)
    target_compile_features(AxlScriptCanvasTests PRIVATE cxx_std_17)
    target_link_libraries(AxlScriptCanvasTests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME editor_script_canvas COMMAND AxlScriptCanvasTests
        -o "${CMAKE_CURRENT_BINARY_DIR}/script-canvas.txt,txt")
    # Native GUI input checks need exclusive focus, even with ctest --parallel.
    set_tests_properties(editor_backend_boundaries editor_viewport_input editor_script_canvas editor_console_commands PROPERTIES RUN_SERIAL TRUE)
    if(WIN32)
        # The boundary executable lives below build/ for layout isolation; use
        # SDK plugins even when Windows loads Qt DLLs from the working directory.
        set_tests_properties(editor_backend_boundaries editor_viewport_input editor_script_canvas editor_console_commands PROPERTIES
            ENVIRONMENT "QT_PLUGIN_PATH=${QT_BIN_DIR}/../plugins"
            TIMEOUT 60)
    endif()
endif()
