#pragma once
#include "Editor/Application/IEditorTool.h"
#include "Tools/PythonConsole/PythonCommands.h"
#include <QProcess>
#include <QPointer>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QDockWidget>

class EditorContext;
class QAction;

// Python executes in a child process; editor RPC is dispatched on the Qt thread.
class PythonConsoleTool final : public QObject, public IEditorTool
{
    Q_OBJECT
public:
    explicit PythonConsoleTool(QString interpreter = {});
    ~PythonConsoleTool() override { Shutdown(); }
    void Initialize(EditorContext&) override;
    void Shutdown() override;
    bool Execute(const QString& source, const QString& filename = "<Axl Python Console>");
    void Stop();
    bool IsBusy() const { return busy_; }
    PythonCommands* Commands() const { return commands_; }
    QDockWidget* Dock() const { return dock_; }
signals:
    void ExecutionFinished(bool success);
private:
    void ReadOutput();
    void Send(const QJsonObject& message);
    void SendExecution();
    void Finish(bool success);
    void Append(const QString& text);
    void UpdateControls();
    EditorContext* context_ = nullptr;
    QPointer<QDockWidget> dock_;
    QPointer<PythonCommands> commands_;
    QPointer<QPlainTextEdit> input_, output_;
    QPointer<QComboBox> history_;
    QPointer<QAction> run_, stop_, interpreterAction_;
    QProcess process_{this};
    QString interpreter_, source_, filename_;
    QByteArray buffer_;
    int runId_ = 0;
    bool ready_ = false, busy_ = false, stopping_ = false;
};
