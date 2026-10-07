#pragma once
#include <QApplication>
#include <QAction>
#include <QMenu>
#include <QPointer>
#include <QUndoStack>
#include <QWidget>
#include <functional>
#include <algorithm>
#include <vector>

// Routes the standard document actions by focus. Formats stay in their adapters/tools.
class DocumentActions final : public QObject
{
public:
    struct Entry {
        QObject* owner;
        std::vector<QWidget*> focusRoots;
        QUndoStack* history;
        std::function<bool()> save, saveAs, mayClose;
    };
    explicit DocumentActions(QWidget& window) : QObject(&window) {
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* focus) {
            if (!focus) return;
            for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
                if (!it->owner) continue;
                for (auto root : it->roots) if (root && (root == focus || root->isAncestorOf(focus))) {
                    active_ = it->key; Update(); return;
                }
            }
        });
    }
    std::size_t Register(Entry entry) {
        const auto key = ++next_;
        Stored stored; stored.key = key; stored.owner = entry.owner; stored.history = entry.history;
        for (auto* root : entry.focusRoots) stored.roots.push_back(root);
        stored.save = std::move(entry.save); stored.saveAs = std::move(entry.saveAs); stored.mayClose = std::move(entry.mayClose);
        entries_.push_back(std::move(stored));
        if (entry.history) connect(entry.history, &QUndoStack::indexChanged, this, [this] { Update(); });
        active_ = key; Update(); return key;
    }
    void Unregister(std::size_t key) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [key](const auto& e) { return e.key == key; }), entries_.end());
        if (active_ == key) active_ = entries_.empty() ? 0 : entries_.back().key;
        Update();
    }
    void Activate(std::size_t key) { active_ = key; Update(); }
    void Configure(QMenu& file, QMenu& edit) {
        save_ = file.addAction("Save"); save_->setObjectName("SaveAction"); save_->setShortcut(QKeySequence::Save);
        saveAs_ = file.addAction("Save As..."); saveAs_->setObjectName("SaveAsAction"); saveAs_->setShortcut(QKeySequence::SaveAs);
        undo_ = edit.addAction("Undo"); undo_->setObjectName("UndoAction"); undo_->setShortcut(QKeySequence::Undo);
        redo_ = edit.addAction("Redo"); redo_->setObjectName("RedoAction"); redo_->setShortcut(QKeySequence::Redo);
        connect(save_, &QAction::triggered, this, [this] { if (auto* e = Active()) e->save(); });
        connect(saveAs_, &QAction::triggered, this, [this] { if (auto* e = Active()) e->saveAs(); });
        connect(undo_, &QAction::triggered, this, [this] { if (auto* e = Active(); e && e->history) e->history->undo(); });
        connect(redo_, &QAction::triggered, this, [this] { if (auto* e = Active(); e && e->history) e->history->redo(); });
        Update();
    }
    bool MayClose() {
        // Copy callbacks: a save/focus change can select another document during the prompt.
        std::vector<std::function<bool()>> checks;
        for (const auto& entry : entries_) if (entry.owner && entry.mayClose) checks.push_back(entry.mayClose);
        for (auto& check : checks) if (!check()) return false;
        return true;
    }
private:
    struct Stored {
        std::size_t key;
        QPointer<QObject> owner;
        std::vector<QPointer<QWidget>> roots;
        QPointer<QUndoStack> history;
        std::function<bool()> save, saveAs, mayClose;
    };
    Stored* Active() {
        for (auto& entry : entries_) if (entry.key == active_ && entry.owner) return &entry;
        return nullptr;
    }
    void Update() {
        if (!save_) return;
        auto* entry = Active();
        save_->setEnabled(entry); saveAs_->setEnabled(entry);
        undo_->setEnabled(entry && entry->history && entry->history->canUndo());
        redo_->setEnabled(entry && entry->history && entry->history->canRedo());
    }
    std::vector<Stored> entries_;
    std::size_t next_ = 0, active_ = 0;
    QAction *save_ = nullptr, *saveAs_ = nullptr, *undo_ = nullptr, *redo_ = nullptr;
};
