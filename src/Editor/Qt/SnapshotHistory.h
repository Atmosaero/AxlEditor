#pragma once
#include <QUndoStack>
#include <QUndoCommand>
#include <functional>
#include <utility>

// A small Qt history adapter. Snapshots contain IDs/data, never pointers to scene objects.
template<class State> class SnapshotHistory
{
public:
    QUndoStack stack;
    std::function<void(const State&)> apply;
    void Record(State before, State after, const QString& label) {
        class Command final : public QUndoCommand {
        public:
            Command(State before, State after, std::function<void(const State&)> apply, QString label)
                : QUndoCommand(label), before_(std::move(before)), after_(std::move(after)), apply_(std::move(apply)) {}
            void undo() override { apply_(before_); }
            void redo() override { if (std::exchange(first_, false)) return; apply_(after_); }
        private:
            State before_, after_;
            std::function<void(const State&)> apply_;
            bool first_ = true; // The preview/result is already visible when committed.
        };
        stack.push(new Command(std::move(before), std::move(after), apply, label));
    }
};
