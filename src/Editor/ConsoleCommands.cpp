#include "Editor/ConsoleCommands.h"
#include <exception>

ConsoleCommands::Registration ConsoleCommands::RegisterCommand(QString name, QString description, Handler handler)
{
    name = name.trimmed().toLower();
    const auto letter = [](QChar c) { return c >= 'a' && c <= 'z'; };
    if (name.isEmpty() || !letter(name.front()) || !handler || commands_.count(name)) return {};
    for (auto c : name)
        if (!letter(c) && !(c >= '0' && c <= '9') && c != '.' && c != '_' && c != '-') return {};
    const auto id = nextId_++;
    commands_.emplace(name, Entry{std::move(description), std::make_shared<Handler>(std::move(handler)), id});
    return Registration(*this, std::move(name), id);
}

std::vector<ConsoleCommands::CommandInfo> ConsoleCommands::GetCommands() const
{
    std::vector<CommandInfo> result;
    for (const auto& [name, entry] : commands_) result.push_back({name, entry.description});
    return result;
}

bool ConsoleCommands::Execute(const QString& line, IEditorLog& output)
{
    QStringList tokens;
    QString token;
    QChar quote;
    bool started = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const auto c = line[i];
        if (c == '\\' && i + 1 < line.size()) {
            const auto next = line[i + 1];
            if (next == '\\' || (quote.isNull() ? next == '\'' || next == '"' || next.isSpace() : next == quote)) {
                token += next;
                ++i;
                started = true;
                continue;
            }
        }
        if (!quote.isNull()) {
            if (c == quote) quote = QChar();
            else token += c;
        } else if (c == '\'' || c == '"') {
            quote = c;
            started = true;
        } else if (c.isSpace()) {
            if (started) { tokens.push_back(token); token.clear(); started = false; }
        } else {
            token += c;
            started = true;
        }
    }
    if (!quote.isNull()) {
        output.Log(ConsoleMessageType::Error, "Unclosed quote.");
        return false;
    }
    if (started) tokens.push_back(token);
    if (tokens.isEmpty()) return false;
    const auto name = tokens.takeFirst().toLower();
    const auto it = commands_.find(name);
    if (it == commands_.end()) {
        output.Log(ConsoleMessageType::Error, "Unknown command: " + name + ". Use help.");
        return false;
    }
    try {
        // Keep the same callback (including mutable state) alive if it unregisters itself.
        auto handler = it->second.handler;
        (*handler)(tokens, output);
        return true;
    } catch (const std::exception& error) {
        output.Log(ConsoleMessageType::Error, "Command " + name + " failed: " + QString::fromUtf8(error.what()));
    } catch (...) {
        output.Log(ConsoleMessageType::Error, "Command " + name + " failed with an unknown exception.");
    }
    return false;
}
