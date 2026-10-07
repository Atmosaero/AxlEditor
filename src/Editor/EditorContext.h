#pragma once

#include <QAction>
#include <QDebug>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>

#include <typeindex>
#include <type_traits>
#include <unordered_map>

// Services are borrowed; this registry never owns them. Non-QObject owners must
// unregister before destruction. QObject services registered by their concrete
// Qt type are guarded; GetService returns null after Qt destroys the object.
// The window/menus and this context must outlive initialized modules.
class EditorContext
{
public:
    EditorContext(QMainWindow& window, QMenu& viewMenu, QMenu& modulesMenu)
        : window_(window), viewMenu_(viewMenu), modulesMenu_(modulesMenu)
    {
    }

    EditorContext(const EditorContext&) = delete;
    EditorContext& operator=(const EditorContext&) = delete;

    template<class T>
    bool RegisterService(T& service)
    {
        const auto key = std::type_index(typeid(T));
        const auto existing = services_.find(key);
        if (existing != services_.end()) {
            if (!existing->second.guarded || existing->second.object) {
                qWarning() << "Duplicate editor service:" << typeid(T).name();
                return false;
            }
            services_.erase(existing); // A deleted QObject no longer occupies this service slot.
        }
        ServiceEntry entry{&service, {}, false};
        if constexpr (std::is_base_of_v<QObject, T>) {
            entry.object = &service;
            entry.guarded = true;
        }
        services_.emplace(key, entry);
        return true;
    }

    template<class T>
    T* GetService() const
    {
        const auto it = services_.find(std::type_index(typeid(T)));
        if (it == services_.end() || (it->second.guarded && !it->second.object)) return nullptr;
        return static_cast<T*>(it->second.pointer);
    }

    template<class T>
    void UnregisterService()
    {
        services_.erase(std::type_index(typeid(T)));
    }

    // Modules can remove only their own still-live registration.
    // expected is compared by identity, and is never dereferenced here.
    template<class T>
    void UnregisterService(T* expected)
    {
        const auto it = services_.find(std::type_index(typeid(T)));
        if (it != services_.end() && it->second.pointer == expected) services_.erase(it);
    }

    // On success Qt owns the dock. On failure ownership remains with the caller.
    // IDs must be unique and stable across launches.
    bool RegisterDock(const QString& id, QDockWidget* dock, Qt::DockWidgetArea area)
    {
        if (!dock || id.isEmpty() || window_.findChild<QDockWidget*>(id)) {
            qWarning() << "Invalid or duplicate editor dock:" << id;
            return false;
        }
        dock->setObjectName(id);
        dock->setParent(&window_);
        window_.addDockWidget(area, dock);
        viewMenu_.addAction(dock->toggleViewAction());
        return true;
    }

    // Defaults to Modules; callers can also supply a normal Qt menu.
    // Module actions should be parented to their dock (or another module-owned
    // QObject) so deleting the module UI also removes its menu entries.
    void RegisterAction(QAction* action, QMenu* menu = nullptr)
    {
        if (!action)
            return;
        if (!action->parent())
            action->setParent(&window_);
        (menu ? menu : &modulesMenu_)->addAction(action);
    }

private:
    struct ServiceEntry
    {
        void* pointer;
        QPointer<QObject> object;
        bool guarded;
    };
    QMainWindow& window_;
    QMenu& viewMenu_;
    QMenu& modulesMenu_;
    std::unordered_map<std::type_index, ServiceEntry> services_;
};
