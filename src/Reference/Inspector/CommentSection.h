#pragma once

#include "Editor/InspectorProperties.h"
#include "Reference/Scene/ReferenceSceneProvider.h"
#include <QPlainTextEdit>
#include <QSignalBlocker>

// This registration is the only editor UI code that knows Axl's Comment storage.
// Other backends can register their own factories with completely different data.
inline void RegisterCommentSection(InspectorProperties& inspector, ReferenceSceneProvider& scene)
{
    inspector.RegisterEditor({"Comment", "Comment",
        [&scene](ObjectId object, PropertyGroupId group, QWidget* parent) -> QWidget* {
            auto* text = new QPlainTextEdit(parent);
            text->setObjectName("CommentText");
            text->setPlaceholderText("Text");
            text->setMinimumHeight(90);
            text->setMaximumHeight(150);
            text->setPlainText(QString::fromStdString(scene.GetCommentText(object, group)));
            QObject::connect(text, &QPlainTextEdit::textChanged, text, [&scene, object, group, text] {
                try { scene.SetCommentText(object, group, text->toPlainText().toStdString()); }
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
