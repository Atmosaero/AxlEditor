#pragma once

#include <QCommandLineParser>

class SceneDocument;

// Startup-only Qt adapter. Document changes still use the existing document API.
class StartupCommands final
{
public:
    StartupCommands();
    bool Parse(const QStringList& arguments);
    bool Apply(SceneDocument& document);
    bool HelpRequested() const { return parser_.isSet("help"); }
    bool VersionRequested() const { return parser_.isSet("version"); }
    QString HelpText() const { return parser_.helpText(); }
    const QString& Error() const { return error_; }

private:
    QCommandLineParser parser_;
    QString error_;
    bool parsed_ = false;
};
