#pragma once
#include <QObject>
#include <QPointer>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <functional>
#include <map>

// Editor-thread command registry. A module owns its callbacks through a QObject;
// a retired owner can never be invoked by a still-running Python session.
class PythonCommands final : public QObject
{
public:
    using Handler = std::function<QJsonValue(const QJsonObject&)>;
    using QObject::QObject;
    bool RegisterCommand(const QString& name, const QString& description, QStringList parameters,
        QJsonObject defaults, QObject& owner, Handler handler);
    void Unregister(QObject& owner);
    QJsonArray GetCommands() const;
    QJsonValue Invoke(const QString& name, const QJsonArray& args = {}, const QJsonObject& kwargs = {}) const;
private:
    struct Entry { QString description; QStringList parameters; QJsonObject defaults; QPointer<QObject> owner; Handler handler; };
    std::map<QString, Entry> entries_;
};
