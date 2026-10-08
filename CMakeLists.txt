cmake_minimum_required(VERSION 3.22)

project(AxlEditor VERSION 0.1.0 LANGUAGES CXX)

option(AXL_BUILD_EDITOR "Build the Qt editor" ON)
option(AXL_BUILD_TESTS "Build focused editor tests" OFF)
add_library(AxlData INTERFACE)
target_include_directories(AxlData INTERFACE src)
target_compile_features(AxlData INTERFACE cxx_std_17)
if(AXL_BUILD_TESTS)
    enable_testing()
    add_executable(AxlSceneContractTests tests/SceneContractTests.cpp)
    set_target_properties(AxlSceneContractTests PROPERTIES AUTOMOC OFF AUTOUIC OFF)
    target_include_directories(AxlSceneContractTests PRIVATE src)
    target_compile_features(AxlSceneContractTests PRIVATE cxx_std_17)
    add_test(NAME scene_contracts_without_qt COMMAND AxlSceneContractTests)
    add_executable(AxlNeutralModelTests tests/NeutralModelTests.cpp)
    target_link_libraries(AxlNeutralModelTests PRIVATE AxlData)
    add_test(NAME neutral_models_without_qt COMMAND AxlNeutralModelTests)
endif()
if(NOT AXL_BUILD_EDITOR)
    return() # This configuration never searches for Qt.
endif()

find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets OpenGLWidgets Svg)

qt_standard_project_setup()
qt_add_resources(EDITOR_ICON_SOURCES resources/editor-icons.qrc)
qt_add_resources(PYTHON_CONSOLE_SOURCES resources/python-console.qrc)
qt_add_executable(AxlEditor WIN32
    ${EDITOR_ICON_SOURCES}
    ${PYTHON_CONSOLE_SOURCES}
    # Bootstrap and editor shell.
    src/main.cpp
    src/Editor/Qt/EditorWindow.h src/Editor/Qt/EditorWindow.cpp
    src/Editor/Qt/EditorTheme.h src/Editor/Qt/EditorTheme.cpp
    src/Editor/Qt/EditorContext.h src/Editor/Application/IEditorTool.h
    src/Editor/Qt/IEditorViewport.h src/Editor/Qt/IEditorLog.h
    src/Editor/Qt/ConsoleCommands.h src/Editor/Qt/ConsoleCommands.cpp
    src/Editor/Application/EditorOperations.h src/Editor/Application/EditorSession.h
    src/Editor/Application/TransformGesture.h
    src/Editor/Qt/DocumentActions.h src/Editor/Qt/DocumentFiles.h
    src/Editor/Qt/SnapshotHistory.h src/Editor/Qt/AssetWidgets.h

    # Small editor-facing data contracts.
    src/Contracts/Transform.h src/Contracts/ISceneProvider.h
    src/Contracts/IAssetProvider.h src/Contracts/IObjectProperties.h

    # Replaceable reference backend and its property editor.
    src/Reference/Scene/Scene.h src/Reference/Scene/ReferenceSceneProvider.h
    src/Reference/Qt/ReferenceAssetProvider.h src/Reference/Qt/CommentSection.h
    src/Reference/Assets/AssetCatalog.h src/Reference/Qt/AssetLinkSection.h
    src/Reference/Qt/SceneDocument.h src/Reference/Qt/SceneCodec.h
    src/Reference/Qt/StartupCommands.h src/Reference/Qt/StartupCommands.cpp
    src/Reference/Qt/PythonEditorCommands.h src/Reference/Qt/PythonEditorCommands.cpp

    # Editor tools.
    src/Editor/Qt/InspectorProperties.h src/Tools/Console/EditorConsole.h
    src/Tools/Console/ConsoleTool.h src/Tools/Console/ConsoleTool.cpp
    src/Tools/PythonConsole/PythonConsoleTool.h src/Tools/PythonConsole/PythonConsoleTool.cpp
    src/Tools/PythonConsole/PythonCommands.h src/Tools/PythonConsole/PythonCommands.cpp
    src/Tools/ScriptCanvas/ScriptCanvasTool.h src/Tools/ScriptCanvas/ScriptCanvasTool.cpp
    src/Tools/ScriptCanvas/ScriptCanvasView.h src/Tools/ScriptCanvas/ScriptCanvasView.cpp
    src/Tools/ScriptCanvas/ScriptGraph.h
    src/Tools/ScriptCanvas/GraphDocument.h src/Tools/ScriptCanvas/GraphCodec.h

    # Reference viewport; external backends may supply their own QWidget.
    src/Reference/Qt/Viewport/ReferenceViewport.h src/Reference/Qt/Viewport/ReferenceViewport.cpp
    src/Reference/Qt/Viewport/TranslateGizmo.h src/Reference/Qt/Viewport/TransformGizmo.h
    src/Reference/Qt/Viewport/GizmoPainting.cpp
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

# Optional focused Qt adapter checks.
if(AXL_BUILD_TESTS)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Test)
    enable_testing()
    qt_add_executable(AxlAssetProviderTests tests/AssetProviderTests.cpp)
    target_include_directories(AxlAssetProviderTests PRIVATE src)
    target_compile_features(AxlAssetProviderTests PRIVATE cxx_std_17)
    target_link_libraries(AxlAssetProviderTests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME reference_asset_provider COMMAND AxlAssetProviderTests)
    # Python is an optional runtime interpreter, not a Qt/C++ build dependency.
    find_program(AXL_PYTHON_EXECUTABLE NAMES python python3)
    qt_add_executable(AxlPythonConsoleTests
        ${EDITOR_ICON_SOURCES} ${PYTHON_CONSOLE_SOURCES}
        tests/PythonConsoleTests.cpp
        src/Tools/PythonConsole/PythonConsoleTool.h src/Tools/PythonConsole/PythonConsoleTool.cpp
        src/Tools/PythonConsole/PythonCommands.cpp src/Reference/Qt/PythonEditorCommands.cpp
        src/Editor/Qt/EditorWindow.cpp src/Editor/Qt/EditorTheme.cpp
        src/Editor/Qt/IEditorViewport.h src/Editor/Qt/ConsoleCommands.cpp
        src/Tools/Console/ConsoleTool.cpp)
    target_include_directories(AxlPythonConsoleTests PRIVATE src)
    target_compile_features(AxlPythonConsoleTests PRIVATE cxx_std_17)
    target_link_libraries(AxlPythonConsoleTests PRIVATE Qt6::Widgets Qt6::Test)
    if(AXL_PYTHON_EXECUTABLE)
        target_compile_definitions(AxlPythonConsoleTests PRIVATE AXL_TEST_PYTHON="${AXL_PYTHON_EXECUTABLE}")
    endif()
    set_target_properties(AxlPythonConsoleTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/python-tests")
    add_test(NAME editor_python_console COMMAND AxlPythonConsoleTests -o "${CMAKE_CURRENT_BINARY_DIR}/python-tests/python-console.txt,txt")
    set_tests_properties(editor_python_console PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
    if(WIN32)
        set_tests_properties(editor_python_console PROPERTIES ENVIRONMENT "QT_PLUGIN_PATH=${QT_BIN_DIR}/../plugins")
    endif()
    qt_add_executable(AxlToolLifecycleTests
        tests/ToolLifecycleTests.cpp
        src/Tools/Console/ConsoleTool.h src/Tools/Console/ConsoleTool.cpp
        src/Editor/Qt/ConsoleCommands.h src/Editor/Qt/ConsoleCommands.cpp
        src/Tools/ScriptCanvas/ScriptCanvasTool.h src/Tools/ScriptCanvas/ScriptCanvasTool.cpp src/Tools/ScriptCanvas/ScriptCanvasView.cpp
    )
    target_include_directories(AxlToolLifecycleTests PRIVATE src)
    target_compile_features(AxlToolLifecycleTests PRIVATE cxx_std_17)
    target_link_libraries(AxlToolLifecycleTests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME editor_tool_lifecycle COMMAND AxlToolLifecycleTests)

    qt_add_executable(AxlConsoleCommandTests
        tests/ConsoleCommandTests.cpp
        src/Editor/Qt/ConsoleCommands.h src/Editor/Qt/ConsoleCommands.cpp
        src/Tools/Console/ConsoleTool.h src/Tools/Console/ConsoleTool.cpp
    )
    target_include_directories(AxlConsoleCommandTests PRIVATE src)
    target_compile_features(AxlConsoleCommandTests PRIVATE cxx_std_17)
    target_link_libraries(AxlConsoleCommandTests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME editor_console_commands COMMAND AxlConsoleCommandTests
        -o "${CMAKE_CURRENT_BINARY_DIR}/console-commands.txt,txt")

    # Intentionally links no reference viewport / OpenGL code.
    qt_add_executable(AxlEditorBoundaryTests
        ${EDITOR_ICON_SOURCES}
        tests/EditorBoundaryTests.cpp src/Editor/Qt/IEditorViewport.h
        src/Editor/Qt/EditorTheme.h src/Editor/Qt/EditorTheme.cpp
        src/Editor/Qt/EditorWindow.h src/Editor/Qt/EditorWindow.cpp
        src/Tools/Console/ConsoleTool.h src/Tools/Console/ConsoleTool.cpp
        src/Editor/Qt/ConsoleCommands.h src/Editor/Qt/ConsoleCommands.cpp
        src/Tools/ScriptCanvas/ScriptCanvasTool.h src/Tools/ScriptCanvas/ScriptCanvasTool.cpp src/Tools/ScriptCanvas/ScriptCanvasView.cpp
    )
    target_include_directories(AxlEditorBoundaryTests PRIVATE src)
    target_compile_features(AxlEditorBoundaryTests PRIVATE cxx_std_17)
    target_link_libraries(AxlEditorBoundaryTests PRIVATE Qt6::Widgets Qt6::Test)
    # Isolate test layout files from the editor's saved workspace.
    set_target_properties(AxlEditorBoundaryTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
    add_test(NAME editor_backend_boundaries COMMAND AxlEditorBoundaryTests
        -o "${CMAKE_CURRENT_BINARY_DIR}/tests/editor-boundaries.txt,txt")

    qt_add_executable(AxlViewportInputTests
        tests/ViewportInputTests.cpp src/Editor/Qt/IEditorViewport.h
        src/Reference/Qt/Viewport/GizmoPainting.cpp
        src/Reference/Qt/Viewport/ReferenceViewport.h src/Reference/Qt/Viewport/ReferenceViewport.cpp
    )
    target_include_directories(AxlViewportInputTests PRIVATE src)
    target_compile_features(AxlViewportInputTests PRIVATE cxx_std_17)
    target_link_libraries(AxlViewportInputTests PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Test)
    add_test(NAME editor_viewport_input COMMAND AxlViewportInputTests
        -o "${CMAKE_CURRENT_BINARY_DIR}/viewport-input.txt,txt")
    qt_add_executable(AxlScriptCanvasTests
        tests/ScriptCanvasTests.cpp
        src/Tools/ScriptCanvas/ScriptCanvasView.h src/Tools/ScriptCanvas/ScriptCanvasView.cpp
        src/Tools/ScriptCanvas/ScriptGraph.h
    )
    target_include_directories(AxlScriptCanvasTests PRIVATE src)
    target_compile_features(AxlScriptCanvasTests PRIVATE cxx_std_17)
    target_link_libraries(AxlScriptCanvasTests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME editor_script_canvas COMMAND AxlScriptCanvasTests
        -o "${CMAKE_CURRENT_BINARY_DIR}/script-canvas.txt,txt")
    qt_add_executable(AxlWorkflowTests ${EDITOR_ICON_SOURCES} tests/WorkflowTests.cpp
        src/Editor/Qt/EditorWindow.cpp src/Editor/Qt/EditorTheme.cpp src/Editor/Qt/ConsoleCommands.cpp
        src/Editor/Qt/IEditorViewport.h src/Tools/Console/ConsoleTool.cpp
        src/Tools/ScriptCanvas/ScriptCanvasTool.cpp src/Tools/ScriptCanvas/ScriptCanvasView.cpp
        src/Reference/Qt/Viewport/ReferenceViewport.cpp src/Reference/Qt/Viewport/GizmoPainting.cpp)
    target_include_directories(AxlWorkflowTests PRIVATE src)
    target_compile_features(AxlWorkflowTests PRIVATE cxx_std_17)
    target_link_libraries(AxlWorkflowTests PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Test)
    set_target_properties(AxlWorkflowTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/workflow-tests")
    add_test(NAME editor_document_workflow COMMAND AxlWorkflowTests -o "${CMAKE_CURRENT_BINARY_DIR}/workflow-tests/workflow.txt,txt")
    set_tests_properties(editor_document_workflow PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    if(WIN32)
        set_tests_properties(editor_document_workflow PROPERTIES ENVIRONMENT "QT_PLUGIN_PATH=${QT_BIN_DIR}/../plugins")
    endif()

    qt_add_executable(AxlGraphDocumentTests tests/GraphDocumentTests.cpp
        src/Tools/ScriptCanvas/ScriptCanvasView.cpp src/Editor/Qt/EditorTheme.cpp)
    target_include_directories(AxlGraphDocumentTests PRIVATE src)
    target_compile_features(AxlGraphDocumentTests PRIVATE cxx_std_17)
    target_link_libraries(AxlGraphDocumentTests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME script_graph_documents COMMAND AxlGraphDocumentTests -o "${CMAKE_CURRENT_BINARY_DIR}/graph-documents.txt,txt")
    set_tests_properties(script_graph_documents PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    if(WIN32)
        set_tests_properties(script_graph_documents PROPERTIES ENVIRONMENT "QT_PLUGIN_PATH=${QT_BIN_DIR}/../plugins")
    endif()

    qt_add_executable(AxlDocumentTests
        ${EDITOR_ICON_SOURCES} tests/DocumentTests.cpp
        src/Editor/Qt/EditorWindow.cpp src/Editor/Qt/EditorTheme.cpp
        src/Editor/Qt/ConsoleCommands.cpp src/Editor/Qt/IEditorViewport.h)
    target_include_directories(AxlDocumentTests PRIVATE src)
    target_compile_features(AxlDocumentTests PRIVATE cxx_std_17)
    target_link_libraries(AxlDocumentTests PRIVATE Qt6::Widgets Qt6::Test)
    set_target_properties(AxlDocumentTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
    add_test(NAME editor_documents COMMAND AxlDocumentTests -o "${CMAKE_CURRENT_BINARY_DIR}/tests/documents.txt,txt")
    set_tests_properties(editor_documents PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    if(WIN32)
        set_tests_properties(editor_documents PROPERTIES ENVIRONMENT "QT_PLUGIN_PATH=${QT_BIN_DIR}/../plugins")
    endif()

    # Native GUI input checks need exclusive focus, even with ctest --parallel.
    qt_add_executable(AxlStartupCommandTests
        ${EDITOR_ICON_SOURCES} tests/StartupCommandTests.cpp
        src/Reference/Qt/StartupCommands.cpp
        src/Editor/Qt/EditorWindow.cpp src/Editor/Qt/EditorTheme.cpp
        src/Editor/Qt/ConsoleCommands.cpp src/Editor/Qt/IEditorViewport.h)
    target_include_directories(AxlStartupCommandTests PRIVATE src)
    target_compile_features(AxlStartupCommandTests PRIVATE cxx_std_17)
    target_link_libraries(AxlStartupCommandTests PRIVATE Qt6::Widgets Qt6::Test)
    target_compile_definitions(AxlStartupCommandTests PRIVATE AXL_EDITOR_EXECUTABLE="$<TARGET_FILE:AxlEditor>")
    add_dependencies(AxlStartupCommandTests AxlEditor)
    set_target_properties(AxlStartupCommandTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/startup-tests")
    add_test(NAME editor_startup_commands COMMAND AxlStartupCommandTests
        -o "${CMAKE_CURRENT_BINARY_DIR}/startup-tests/startup-commands.txt,txt")
    set_tests_properties(editor_startup_commands PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
    if(WIN32)
        set_tests_properties(editor_startup_commands PROPERTIES ENVIRONMENT "QT_PLUGIN_PATH=${QT_BIN_DIR}/../plugins")
    endif()

    set_tests_properties(editor_backend_boundaries editor_viewport_input editor_script_canvas editor_console_commands PROPERTIES RUN_SERIAL TRUE)
    if(WIN32)
        # The boundary executable lives below build/ for layout isolation; use
        # SDK plugins even when Windows loads Qt DLLs from the working directory.
        set_tests_properties(editor_backend_boundaries editor_viewport_input editor_script_canvas editor_console_commands PROPERTIES
            ENVIRONMENT "QT_PLUGIN_PATH=${QT_BIN_DIR}/../plugins"
            TIMEOUT 60)
    endif()
endif()
