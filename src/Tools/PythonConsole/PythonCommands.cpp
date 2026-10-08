#include "Tools/PythonConsole/PythonCommands.h"
#include <QRegularExpression>
#include <stdexcept>

bool PythonCommands::RegisterCommand(const QString& name, const QString& description, QStringList parameters,
    QJsonObject defaults, QObject& owner, Handler handler)
{
    static const QRegularExpression identifier("^[a-z_][a-z0-9_]*$");
    if (!identifier.match(name).hasMatch() || name == "commands" || !handler) return false;
    bool optional = false;
    QStringList seen;
    for (const auto& parameter : parameters) {
        if (!identifier.match(parameter).hasMatch() || seen.contains(parameter)) return false;
        seen.append(parameter);
        if (defaults.contains(parameter)) optional = true;
        else if (optional) return false;
    }
    for (auto it = defaults.begin(); it != defaults.end(); ++it) if (!seen.contains(it.key())) return false;
    if (const auto it = entries_.find(name); it != entries_.end() && it->second.owner) return false;
    entries_[name] = {description, std::move(parameters), std::move(defaults), &owner, std::move(handler)};
    return true;
}
void PythonCommands::Unregister(QObject& owner)
{
    for (auto it = entries_.begin(); it != entries_.end();)
        if (it->second.owner == &owner) it = entries_.erase(it); else ++it;
}
QJsonArray PythonCommands::GetCommands() const
{
    QJsonArray result;
    for (const auto& [name, entry] : entries_) if (entry.owner)
        result.append(QJsonObject{{"name", name}, {"description", entry.description},
            {"parameters", QJsonArray::fromStringList(entry.parameters)}, {"defaults", entry.defaults}});
    return result;
}
QJsonValue PythonCommands::Invoke(const QString& name, const QJsonArray& args, const QJsonObject& kwargs) const
{
    const auto found = entries_.find(name);
    if (found == entries_.end() || !found->second.owner) throw std::runtime_error("Editor command is unavailable: " + name.toStdString());
    const auto entry = found->second; // A callback may unregister itself during execution.
    if (args.size() > entry.parameters.size()) throw std::invalid_argument("Too many arguments for axl." + name.toStdString());
    auto bound = entry.defaults;
    for (qsizetype i = 0; i < args.size(); ++i) bound[entry.parameters[i]] = args[i];
    for (auto it = kwargs.begin(); it != kwargs.end(); ++it) {
        const auto index = entry.parameters.indexOf(it.key());
        if (index < 0) throw std::invalid_argument("Unknown argument: " + it.key().toStdString());
        if (index < args.size()) throw std::invalid_argument("Argument supplied twice: " + it.key().toStdString());
        bound[it.key()] = it.value();
    }
    for (const auto& parameter : entry.parameters)
        if (!bound.contains(parameter)) throw std::invalid_argument("Missing argument: " + parameter.toStdString());
    return entry.handler(bound);
}
