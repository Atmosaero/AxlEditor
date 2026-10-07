#pragma once
#include "Editor/Qt/InspectorProperties.h"
#include "Editor/Qt/AssetWidgets.h"
#include "Editor/Application/EditorSession.h"
#include "Reference/Scene/ReferenceSceneProvider.h"
#include <QDir>
#include <QHBoxLayout>

inline void RegisterAssetLinkSection(InspectorProperties& inspector, ReferenceSceneProvider& scene,
    IAssetProvider& assets, const EditorSession& session)
{
    const auto refresh = [&scene, &assets, &session](QWidget* widget, ObjectId object, PropertyGroupId group) {
        auto* label = widget->findChild<QLabel*>("AssetLinkPath");
        const auto path = QString::fromStdString(scene.GetAssetLink(object, group));
        bool found = false;
        const auto absolute = QDir(QString::fromStdString(session.projectRoot)).absoluteFilePath(path);
        for (const auto& asset : assets.GetAssets())
            if (QDir::cleanPath(QString::fromStdString(asset.sourcePath)) == QDir::cleanPath(absolute)) found = true;
        label->setText(path.isEmpty() ? "No asset" : path + (found ? "" : " (missing)"));
        label->setToolTip(path);
    };
    inspector.RegisterEditor({"AssetLink", "Asset Link",
        [&scene, &assets, &session, operations = inspector.Operations(), refresh](ObjectId object, PropertyGroupId group, QWidget* parent) -> QWidget* {
            auto* widget = new QWidget(parent); auto* layout = new QVBoxLayout(widget); layout->setContentsMargins(0, 0, 0, 0);
            auto* label = new QLabel(widget); label->setObjectName("AssetLinkPath"); label->setWordWrap(true); layout->addWidget(label);
            auto* row = new QHBoxLayout; auto* choose = new QPushButton("Choose Asset...", widget);
            choose->setObjectName("ChooseAssetButton"); choose->setEnabled(session.HasProject());
            auto* clear = new QPushButton("Clear", widget); row->addWidget(choose); row->addWidget(clear); layout->addLayout(row);
            const auto generation = operations ? operations->Generation() : 0;
            const auto write = [&scene, operations, generation, object, group, widget, refresh](const std::string& path) {
                if (operations && !operations->IsCurrent(generation)) return;
                try {
                    if (scene.GetAssetLink(object, group) == path) return;
                    if (operations) operations->Edit("Asset Link", [&] { scene.SetAssetLink(object, group, path); });
                    else scene.SetAssetLink(object, group, path);
                    refresh(widget, object, group);
                } catch (const std::out_of_range&) { widget->setEnabled(false); }
            };
            QObject::connect(choose, &QPushButton::clicked, widget, [&assets, &session, widget, write] {
                AssetPicker picker(assets, widget);
                if (picker.exec() != QDialog::Accepted) return;
                if (const auto asset = picker.Selected())
                    write(QDir(QString::fromStdString(session.projectRoot)).relativeFilePath(QString::fromStdString(asset->sourcePath)).toStdString());
            });
            QObject::connect(clear, &QPushButton::clicked, widget, [write] { write({}); });
            refresh(widget, object, group); return widget;
        }, refresh, {}});
}
