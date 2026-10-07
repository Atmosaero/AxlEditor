#pragma once

#include "Editor/Qt/InspectorProperties.h"
#include "Reference/Scene/ReferenceSceneProvider.h"
#include <QPlainTextEdit>
#include <QSignalBlocker>

// This registration is the only editor UI code that knows Axl's Comment storage.
// Other backends can register their own factories with completely different data.
inline void RegisterCommentSection(InspectorProperties& inspector, ReferenceSceneProvider& scene)
{
    inspector.RegisterEditor({"Comment", "Comment",
        [&scene, operations = inspector.Operations()](ObjectId object, PropertyGroupId group, QWidget* parent) -> QWidget* {
            auto* text = new QPlainTextEdit(parent);
            text->setObjectName("CommentText");
            text->setPlaceholderText("Text");
            text->setMinimumHeight(90);
            text->setMaximumHeight(150);
            if (operations) text->setUndoRedoEnabled(false); // Document actions own committed edits.
            text->setPlainText(QString::fromStdString(scene.GetCommentText(object, group)));
            const auto generation = operations ? operations->Generation() : 0;
            QObject::connect(text, &QPlainTextEdit::textChanged, text, [&scene, operations, generation, object, group, text] {
                if (operations && !operations->IsCurrent(generation)) return;
                try {
                    const auto value = text->toPlainText().toStdString();
                    if (scene.GetCommentText(object, group) == value) return;
                    if (operations) operations->Edit("Comment text", [&] { scene.SetCommentText(object, group, value); });
                    else scene.SetCommentText(object, group, value);
                }
                catch (const std::out_of_range&) { text->setEnabled(false); }
            });
            return text;
        },
        [&scene](QWidget* widget, ObjectId object, PropertyGroupId group) {
            auto* text = static_cast<QPlainTextEdit*>(widget);
            const auto value = QString::fromStdString(scene.GetCommentText(object, group));
            if (text->toPlainText() == value) return;
            const QSignalBlocker blocker(text);
            text->setPlainText(value);
        }, QIcon(":/icons/components/comment.svg")});
}
