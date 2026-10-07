#pragma once

#include "Editor/IEditorLog.h"
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

// Editor-thread service. Any class can keep a Registration for its command's
// lifetime; no QObject base class, console widget or global singleton is needed.
class ConsoleCommands final : public QObject
{
public:
    using Handler = std::function<void(const QStringList& arguments, IEditorLog& output)>;
    struct CommandInfo { QString name; QString description; };

    class Registration
    {
    public:
        Registration() = default;
        ~Registration() { Reset(); }
        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;
        Registration(Registration&& other) noexcept { *this = std::move(other); }
        Registration& operator=(Registration&& other) noexcept;
        explicit operator bool() const;
        void Reset();
    private:
        friend class ConsoleCommands;
        Registration(ConsoleCommands& registry, QString name, std::uint64_t id)
            : registry_(&registry), name_(std::move(name)), id_(id) {}
        QPointer<ConsoleCommands> registry_;
        QString name_;
        std::uint64_t id_ = 0;
    };

    explicit ConsoleCommands(QObject* parent = nullptr) : QObject(parent) {}
    // Names are case-insensitive ASCII identifiers: letter, then letters,
    // digits, '.', '_' or '-'. Invalid/duplicate names return an empty handle.
    // Store the returned handle and release it before captured state dies.
    [[nodiscard]] Registration RegisterCommand(QString name, QString description, Handler handler);
    std::vector<CommandInfo> GetCommands() const;
    // Splits whitespace, supports single/double quotes and escaped quotes,
    // backslashes and unquoted whitespace. Other backslashes stay literal.
    // Blank input is ignored; syntax/lookup/callback failures log an Error.
    bool Execute(const QString& line, IEditorLog& output);

private:
    struct Entry { QString description; std::shared_ptr<Handler> handler; std::uint64_t id; };
    std::map<QString, Entry> commands_;
    std::uint64_t nextId_ = 1;
};

inline ConsoleCommands::Registration& ConsoleCommands::Registration::operator=(Registration&& other) noexcept
{
    if (this != &other) {
        Reset();
        registry_ = other.registry_;
        name_ = std::move(other.name_);
        id_ = std::exchange(other.id_, 0);
        other.registry_.clear();
    }
    return *this;
}

inline ConsoleCommands::Registration::operator bool() const
{
    return registry_ && id_ != 0;
}

inline void ConsoleCommands::Registration::Reset()
{
    if (registry_ && id_) {
        const auto it = registry_->commands_.find(name_);
        if (it != registry_->commands_.end() && it->second.id == id_) registry_->commands_.erase(it);
    }
    registry_.clear();
    id_ = 0;
}
