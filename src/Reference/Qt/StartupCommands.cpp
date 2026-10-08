#include "Reference/Qt/StartupCommands.h"
#include "Reference/Qt/SceneDocument.h"

StartupCommands::StartupCommands()
{
    parser_.setApplicationDescription("Axl Editor: open or create a reference project and open a scene.");
    parser_.addHelpOption();
    parser_.addVersionOption();
    parser_.addOption({"create-project", "Create a new project directory with Assets/ and open it.", "directory"});
    parser_.addOption({"open-project", "Open an existing project directory.", "directory"});
    parser_.addOption({"open-scene", "Open a scene. Relative paths use the specified project, otherwise the working directory.", "file"});
}

bool StartupCommands::Parse(const QStringList& arguments)
{
    error_.clear(); parsed_ = false;
    if (!parser_.parse(arguments)) { error_ = parser_.errorText(); return false; }
    if (HelpRequested() || VersionRequested()) { parsed_ = true; return true; }
    if (!parser_.positionalArguments().isEmpty()) {
        error_ = "Unexpected positional argument. Use --open-project or --open-scene."; return false;
    }
    for (const auto* name : {"create-project", "open-project", "open-scene"}) {
        if (parser_.values(name).size() > 1) {
            error_ = QString("Option --%1 can only be specified once.").arg(name); return false;
        }
        if (parser_.isSet(name) && parser_.value(name).isEmpty()) {
            error_ = QString("Option --%1 requires a non-empty path.").arg(name); return false;
        }
    }
    if (parser_.isSet("create-project") && parser_.isSet("open-project")) {
        error_ = "Use either --create-project or --open-project, not both."; return false;
    }
    parsed_ = true; return true;
}

bool StartupCommands::Apply(SceneDocument& document)
{
    if (!parsed_) { error_ = "Startup arguments have not been parsed successfully."; return false; }
    error_.clear();
    if (HelpRequested() || VersionRequested()) return true;
    const bool create = parser_.isSet("create-project");
    const bool project = create || parser_.isSet("open-project");
    const auto root = project ? QFileInfo(parser_.value(create ? "create-project" : "open-project")).absoluteFilePath() : QString();
    if (create && (QFileInfo::exists(root) || QFileInfo(root).isSymLink())) {
        error_ = "Project destination already exists: " + root; return false;
    }
    if (project && !create && !QFileInfo(root).isDir()) {
        error_ = "Project directory does not exist: " + root; return false;
    }
    QString scene;
    if (parser_.isSet("open-scene")) {
        const auto input = parser_.value("open-scene");
        scene = QFileInfo(project && QFileInfo(input).isRelative() ? QDir(root).filePath(input) : input).absoluteFilePath();
        try {
            // Reject invalid documents before creating folders or changing the session.
            SceneCodec::Decode(SceneCodec::Read(scene));
        } catch (const std::exception& exception) {
            error_ = "Cannot open scene " + scene + ": " + QString::fromUtf8(exception.what()); return false;
        }
    }
    const auto assets = QDir(root).filePath("Assets");
    if (create && !QDir().mkpath(assets)) {
        QDir().rmdir(root); // Only removes an empty directory; never recursively deletes data.
        error_ = "Cannot create project directory: " + root; return false;
    }
    if (project && !document.OpenProject(root)) {
        if (create) { QDir().rmdir(assets); QDir().rmdir(root); }
        error_ = "Project opening was canceled or failed: " + root; return false;
    }
    if (!scene.isEmpty() && !document.Open(scene)) {
        error_ = document.lastError.isEmpty() ? "Scene opening was canceled." : document.lastError; return false;
    }
    return true;
}
