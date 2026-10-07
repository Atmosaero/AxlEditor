#pragma once

#include "Contracts/IObjectProperties.h"
#include <QAction>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <functional>
#include <stdexcept>
#include <utility>
#include <algorithm>

// A registry of Qt widget factories, not a property/reflection system.
class InspectorProperties final : public QWidget
{
public:
    struct SectionEditor
    {
        std::string type;
        QString displayName;
        std::function<QWidget*(ObjectId, PropertyGroupId, QWidget*)> create;
        std::function<void(QWidget*, ObjectId, PropertyGroupId)> refresh;
        QIcon icon; // Optional editor metadata; providers do not know about Qt icons.
    };

    explicit InspectorProperties(IObjectProperties* provider, QWidget* parent = nullptr)
        : QWidget(parent), provider_(provider)
    {
        setObjectName("InspectorProperties");
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);
        sections_ = new QVBoxLayout;
        sections_->setSpacing(12);
        layout->addLayout(sections_);
        add_ = new QPushButton("+ Add Component", this);
        add_->setObjectName("AddComponentButton");
        add_->setMinimumHeight(36);
        add_->setCursor(Qt::PointingHandCursor);
        menu_ = new QMenu(add_);
        add_->setMenu(menu_);
        layout->addWidget(add_);
        connect(menu_, &QMenu::aboutToShow, this, [this] { RefreshMenu(); });
        setVisible(false);
    }

    bool RegisterEditor(SectionEditor editor)
    {
        if (editor.type.empty() || !editor.create || Find(editor.type))
            return false;
        editors_.push_back(std::move(editor));
        Refresh(true);
        return true;
    }

    void SetObject(ObjectId object)
    {
        const bool changed = object_ != object;
        object_ = object;
        Refresh(changed);
    }

    // Call after external property changes; unchanged sections retain focus/undo.
    void Refresh(bool rebuild = false)
    {
        const bool selected = provider_ && object_ != InvalidObjectId;
        setVisible(selected);
        if (!selected) { ClearSections(); menu_->clear(); return; }
        try {
            const auto groups = provider_->GetPropertyGroups(object_);
            bool same = active_.size() == groups.size();
            for (std::size_t i = 0; same && i < groups.size(); ++i)
                same = active_[i].group.id == groups[i].id && active_[i].group.type == groups[i].type
                    && active_[i].group.displayName == groups[i].displayName;
            if (rebuild || !same) {
                if (rebuild) ClearSections();
                auto previous = std::move(active_);
                active_.clear();
                for (const auto& group : groups) {
                    const auto existing = std::find_if(previous.begin(), previous.end(), [&group](const auto& section) {
                        return section.group.id == group.id && section.group.type == group.type
                            && section.group.displayName == group.displayName;
                    });
                    if (existing != previous.end()) {
                        active_.push_back(*existing);
                        previous.erase(existing);
                        sections_->addWidget(active_.back().section); // Reorder, retain focus and undo.
                        continue;
                    }
                    auto* section = new QGroupBox(QString::fromStdString(group.displayName), this);
                    section->setObjectName("PropertyGroupSection");
                    section->setProperty("componentCard", true);
                    auto* layout = new QVBoxLayout(section);
                    layout->setContentsMargins(10, 10, 10, 12);
                    layout->setSpacing(8);
                    const auto* registered = Find(group.type);
                    if (registered && !registered->icon.isNull()) {
                        // QGroupBox has no title icon API; reserve space next to its native title.
                        section->setProperty("componentIcon", true);
                        auto* icon = new QLabel(section);
                        icon->setObjectName("PropertyGroupIcon");
                        icon->setPixmap(registered->icon.pixmap(QSize(16, 16)));
                        icon->setGeometry(10, 7, 16, 16);
                    }
                    QWidget* editor = nullptr;
                    try { editor = registered ? registered->create(object_, group.id, section) : nullptr; }
                    catch (const std::out_of_range&) {
                        RetireSection(section);
                        continue; // The group disappeared after enumeration.
                    }
                    const bool hasEditor = editor != nullptr;
                    if (!editor) {
                        auto* missing = new QLabel("No editor registered for this property group.", section);
                        missing->setWordWrap(true);
                        editor = missing;
                    }
                    layout->addWidget(editor);
                    auto* remove = new QPushButton("Remove", section);
                    remove->setObjectName("RemovePropertyGroupButton");
                    remove->setProperty("groupId", QVariant::fromValue<qulonglong>(group.id));
                    layout->addWidget(remove, 0, Qt::AlignRight);
                    const ObjectId object = object_;
                    connect(remove, &QPushButton::clicked, this, [this, object, id = group.id] {
                        if (object_ != object) return;
                        try { provider_->RemovePropertyGroup(object, id); }
                        catch (const std::out_of_range&) { /* Runtime removed the object. */ }
                        QueueRefresh();
                    });
                    sections_->addWidget(section);
                    active_.push_back({group, section, editor, hasEditor});
                }
                for (const auto& section : previous) RetireSection(section.section);
            }
            for (const auto& section : active_) {
                const auto* registered = Find(section.group.type);
                if (section.hasEditor && registered && registered->refresh) {
                    try { registered->refresh(section.editor, object_, section.group.id); }
                    catch (const std::out_of_range&) {
                        // A stale group must not hide unrelated live sections.
                        DeactivateSection(section.section);
                    }
                }
            }
            RefreshMenu();
        } catch (const std::out_of_range&) {
            object_ = InvalidObjectId;
            ClearSections();
            menu_->clear();
            hide();
        }
    }

private:
    struct ActiveSection
    {
        PropertyGroupInfo group;
        QGroupBox* section;
        QWidget* editor;
        bool hasEditor;
    };

    const SectionEditor* Find(const std::string& type) const
    {
        for (const auto& editor : editors_)
            if (editor.type == type) return &editor;
        return nullptr;
    }

    void ClearSections()
    {
        for (const auto& section : active_) RetireSection(section.section);
        active_.clear();
    }

    void RetireSection(QGroupBox* section)
    {
        sections_->removeWidget(section);
        DeactivateSection(section);
        section->hide();
        section->deleteLater(); // A button/text signal may still be executing.
    }

    static void DeactivateSection(QGroupBox* section)
    {
        section->blockSignals(true);
        for (auto* child : section->findChildren<QObject*>())
            child->blockSignals(true); // Retired widgets must not write before deleteLater.
        section->setEnabled(false);
    }

    void QueueRefresh()
    {
        QTimer::singleShot(0, this, [this] { Refresh(); });
    }

    void RefreshMenu()
    {
        menu_->clear();
        if (!provider_ || object_ == InvalidObjectId) { add_->setEnabled(false); return; }
        try {
            for (const auto& editor : editors_) {
                if (!provider_->CanAddPropertyGroup(object_, editor.type)) continue;
                auto* action = menu_->addAction(editor.icon, editor.displayName);
                const ObjectId object = object_;
                connect(action, &QAction::triggered, this, [this, object, type = editor.type] {
                    if (object_ != object) return;
                    try {
                        if (provider_->CanAddPropertyGroup(object, type))
                            provider_->AddPropertyGroup(object, type);
                    } catch (const std::out_of_range&) { /* Runtime removed the object. */ }
                    QueueRefresh();
                });
            }
        } catch (const std::out_of_range&) { menu_->clear(); }
        add_->setEnabled(!menu_->actions().isEmpty());
    }

    IObjectProperties* provider_;
    ObjectId object_ = InvalidObjectId;
    QVBoxLayout* sections_;
    QPushButton* add_;
    QMenu* menu_;
    std::vector<SectionEditor> editors_;
    std::vector<ActiveSection> active_;
};
