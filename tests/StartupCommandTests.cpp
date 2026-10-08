#include "Reference/Qt/StartupCommands.h"
#include "Reference/Qt/SceneDocument.h"
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
struct Editor {
    ReferenceSceneProvider scene;
    ReferenceAssetProvider assets{""};
    EditorWindow window{scene, assets, nullptr, &scene};
    SceneDocument document{window, scene, assets};
};
void WriteScene(const QString& path) {
    ReferenceSceneProvider scene;
    const auto id = scene.CreateObject("CLI Cube");
    Transform transform; transform.position = {1, 2, 3}; scene.SetTransform(id, transform);
    const auto comment = scene.AddPropertyGroup(id, "Comment"); scene.SetCommentText(id, comment, "Startup\nComment");
    SceneCodec::Write(path, SceneCodec::Encode(scene.GetObjects()));
}
#ifdef Q_OS_WIN
HWND EditorHandle(qint64 process) {
    struct Search { DWORD process; HWND found = nullptr; } search{DWORD(process)};
    EnumWindows([](HWND handle, LPARAM state) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(state); DWORD process;
        GetWindowThreadProcessId(handle, &process);
        wchar_t title[512]{}; GetWindowTextW(handle, title, 512);
        if (process == search.process && QString::fromWCharArray(title).startsWith("Axl Editor -")) {
            search.found = handle; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
QString WindowTitle(HWND handle) {
    wchar_t title[512]{}; GetWindowTextW(handle, title, 512); return QString::fromWCharArray(title);
}
#endif
}

class StartupCommandTests final : public QObject {
    Q_OBJECT
private slots:
    void init() { QFile::remove(QCoreApplication::applicationDirPath() + "/editor-layout.ini"); }
    void cleanup() { QFile::remove(QCoreApplication::applicationDirPath() + "/editor-layout.ini"); }

    void rejectsInvalidArguments_data() {
        QTest::addColumn<QStringList>("arguments");
        QTest::newRow("unknown") << QStringList{"--unknown"};
        QTest::newRow("graph-not-supported") << QStringList{"--open-graph", "file.axlgraph"};
        QTest::newRow("missing-project") << QStringList{"--create-project"};
        QTest::newRow("missing-scene") << QStringList{"--open-scene"};
        QTest::newRow("empty-project") << QStringList{"--open-project", ""};
        QTest::newRow("conflicting-projects") << QStringList{"--create-project", "one", "--open-project", "two"};
        QTest::newRow("duplicate") << QStringList{"--open-scene", "one", "--open-scene", "two"};
        QTest::newRow("positional") << QStringList{"scene.axl"};
    }
    void rejectsInvalidArguments() {
        QFETCH(QStringList, arguments); arguments.prepend("AxlEditor");
        StartupCommands commands; QVERIFY(!commands.Parse(arguments)); QVERIFY(!commands.Error().isEmpty());
    }
    void noArgumentsHelpAndVersion() {
        StartupCommands commands;
        QVERIFY(commands.Parse({"AxlEditor"})); Editor editor; QVERIFY(commands.Apply(editor.document));
        QVERIFY(!editor.document.Session().HasProject()); QVERIFY(editor.scene.GetObjects().empty());
        QVERIFY(commands.Parse({"AxlEditor", "--help"})); QVERIFY(commands.HelpRequested());
        for (auto name : {"--create-project", "--open-project", "--open-scene"}) QVERIFY(commands.HelpText().contains(name));
        QVERIFY(!commands.HelpText().contains("open-graph"));
        QVERIFY(commands.Parse({"AxlEditor", "--version"})); QVERIFY(commands.VersionRequested());
    }
    void createsProjectWithUnicodeAndSpacesWithoutOverwriting() {
        QTemporaryDir dir; const auto root = dir.filePath(QString::fromUtf8("My Game Проект"));
        Editor editor; StartupCommands commands;
        QVERIFY(commands.Parse({"AxlEditor", "--create-project", root})); QVERIFY(commands.Apply(editor.document));
        QVERIFY(QFileInfo(QDir(root).filePath("Assets")).isDir());
        QCOMPARE(QString::fromStdString(editor.document.Session().projectRoot), root);
        QCOMPARE(QString::fromStdString(editor.document.Session().projectName), QFileInfo(root).fileName());
        QVERIFY(editor.scene.GetObjects().empty()); QVERIFY(!editor.document.IsDirty());
        SceneCodec::Write(QDir(root).filePath("keep.lua"), "keep");
        QVERIFY(!commands.Apply(editor.document)); QVERIFY(commands.Error().contains("already exists"));
        QCOMPARE(SceneCodec::Read(QDir(root).filePath("keep.lua")), QByteArray("keep"));
        const auto blocked = dir.filePath("file"); SceneCodec::Write(blocked, "keep");
        QVERIFY(commands.Parse({"AxlEditor", "--create-project", blocked + "/child"}));
        QVERIFY(!commands.Apply(editor.document)); QCOMPARE(SceneCodec::Read(blocked), QByteArray("keep"));
    }
    void opensProjectAndRelativeSceneInEitherOrder() {
        QTemporaryDir dir; QVERIFY(QDir(dir.path()).mkpath("Assets")); WriteScene(dir.filePath("Assets/main.axl"));
        SceneCodec::Write(dir.filePath("Assets/test.lua"), "-- script");
        Editor editor; StartupCommands commands;
        QVERIFY(commands.Parse({"AxlEditor", "--open-project", dir.path()})); QVERIFY(commands.Apply(editor.document));
        QCOMPARE(editor.assets.GetAssets().size(), std::size_t{2}); QVERIFY(editor.scene.GetObjects().empty());
        for (const auto& args : {QStringList{"--open-project", dir.path(), "--open-scene", "Assets/main.axl"},
            QStringList{"--open-scene=Assets/main.axl", "--open-project=" + dir.path()}}) {
            QVERIFY(commands.Parse(QStringList{"AxlEditor"} + args)); QVERIFY(commands.Apply(editor.document));
            QCOMPARE(editor.scene.GetName(1), std::string("CLI Cube"));
            QVERIFY(editor.scene.GetTransform(1)->position == (Vec3{1, 2, 3}));
            QCOMPARE(editor.scene.GetCommentText(1, 1), std::string("Startup\nComment"));
            QCOMPARE(QString::fromStdString(editor.document.Session().scenePath), dir.filePath("Assets/main.axl"));
            QVERIFY(!editor.document.IsDirty());
        }
    }
    void opensStandaloneSceneRelativeToWorkingDirectory() {
        QTemporaryDir dir; const auto path = dir.filePath("standalone.axl"); WriteScene(path);
        Editor editor; StartupCommands commands;
        const auto relative = QDir::current().relativeFilePath(path);
        QVERIFY(commands.Parse({"AxlEditor", "--open-scene", relative})); QVERIFY(commands.Apply(editor.document));
        QVERIFY(!editor.document.Session().HasProject()); QCOMPARE(editor.scene.GetName(1), std::string("CLI Cube"));
        QCOMPARE(QString::fromStdString(editor.document.Session().scenePath), path);
    }
    void validatesBeforeCreatingDirectoriesOrReplacingData() {
        QTemporaryDir dir; Editor editor; const auto id = editor.scene.CreateObject("Keep me");
        const auto before = SceneCodec::Encode(editor.scene.GetObjects()); StartupCommands commands;
        const auto root = dir.filePath("Must not be created"); const auto badScene = dir.filePath("bad.axl");
        SceneCodec::Write(badScene, "{broken");
        for (const auto& args : {QStringList{"--create-project", root, "--open-scene", badScene},
            QStringList{"--open-project", dir.filePath("missing")},
            QStringList{"--open-scene", dir.filePath("missing.axl")}}) {
            QVERIFY(commands.Parse(QStringList{"AxlEditor"} + args)); QVERIFY(!commands.Apply(editor.document));
            QVERIFY(!commands.Error().isEmpty()); QVERIFY(!QFileInfo::exists(root));
            QCOMPARE(SceneCodec::Encode(editor.scene.GetObjects()), before);
        }
        editor.window.Operations().Rename(id, "Unsaved");
        editor.document.decide = [] { return SceneDocument::Decision::Cancel; };
        const auto canceledRoot = dir.filePath("Canceled");
        QVERIFY(commands.Parse({"AxlEditor", "--create-project", canceledRoot})); QVERIFY(!commands.Apply(editor.document));
        QVERIFY(!QFileInfo::exists(canceledRoot)); QCOMPARE(editor.scene.GetName(id), std::string("Unsaved"));
    }

    void executableErrorsAndUtilityOptions_data() {
        QTest::addColumn<QStringList>("arguments"); QTest::addColumn<int>("exitCode"); QTest::addColumn<QByteArray>("expected");
        QTest::newRow("help") << QStringList{"--help"} << 0 << QByteArray("--create-project");
        QTest::newRow("version") << QStringList{"--version"} << 0 << QByteArray("0.1.0");
        QTest::newRow("unknown") << QStringList{"--unknown"} << 2 << QByteArray("Unknown option");
        QTest::newRow("missing-value") << QStringList{"--open-scene"} << 2 << QByteArray("Missing value");
        QTest::newRow("conflict") << QStringList{"--create-project", "one", "--open-project", "two"} << 2 << QByteArray("not both");
    }
    void executableErrorsAndUtilityOptions() {
        QFETCH(QStringList, arguments); QFETCH(int, exitCode); QFETCH(QByteArray, expected);
        QProcess process; process.start(QString::fromUtf8(AXL_EDITOR_EXECUTABLE), arguments);
        QVERIFY(process.waitForStarted()); QVERIFY(process.waitForFinished(15000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit); QCOMPARE(process.exitCode(), exitCode);
        const auto output = exitCode ? process.readAllStandardError() : process.readAllStandardOutput();
        QVERIFY2(output.contains(expected), output.constData());
    }
    void executableStartsWithEachCommand_data() {
        QTest::addColumn<QString>("command");
        QTest::newRow("create") << QString("--create-project");
        QTest::newRow("project") << QString("--open-project");
        QTest::newRow("scene") << QString("--open-scene");
        QTest::newRow("project-and-scene") << QString("combined");
    }
    void executableStartsWithEachCommand() {
#ifdef Q_OS_WIN
        QFETCH(QString, command); QTemporaryDir dir;
        const auto root = dir.filePath(QString::fromUtf8("CLI Проект with spaces"));
        const auto scenePath = dir.filePath(QString::fromUtf8("Scene Сцена.axl")); WriteScene(scenePath);
        if (command != "--create-project") QVERIFY(QDir().mkpath(QDir(root).filePath("Assets")));
        const QStringList arguments = command == "combined"
            ? QStringList{"--open-scene", scenePath, "--open-project", root}
            : QStringList{command, command == "--open-scene" ? scenePath : root};
        QProcess process; process.setWorkingDirectory(dir.path());
        process.start(QString::fromUtf8(AXL_EDITOR_EXECUTABLE), arguments); QVERIFY(process.waitForStarted());
        HWND handle = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((handle = EditorHandle(process.processId())) != nullptr, 15000);
        const auto title = WindowTitle(handle);
        QVERIFY(title.contains(command == "--open-scene" ? "No project" : QFileInfo(root).fileName()));
        QVERIFY(title.contains(command == "--open-scene" || command == "combined" ? QFileInfo(scenePath).fileName() : "Untitled Scene"));
        if (command == "--create-project") QVERIFY(QFileInfo(QDir(root).filePath("Assets")).isDir());
        QVERIFY(PostMessageW(handle, WM_CLOSE, 0, 0)); QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit); QCOMPARE(process.exitCode(), 0);
        QVERIFY2(process.readAllStandardError().isEmpty(), "Unexpected startup diagnostics");
#else
        QSKIP("Native startup window verification is implemented for Windows.");
#endif
    }
};
QTEST_MAIN(StartupCommandTests)
#include "StartupCommandTests.moc"
