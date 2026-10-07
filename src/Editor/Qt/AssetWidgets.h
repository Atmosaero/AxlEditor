#pragma once
#include "Contracts/IAssetProvider.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QTreeWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QComboBox>
#include <QVBoxLayout>
#include <QPointer>
#include <map>
#include <functional>

inline void FilterAssetItems(QTreeWidget& tree, const QString& search, const QString& type) {
    for (int i = 0; i < tree.topLevelItemCount(); ++i) {
        auto* item = tree.topLevelItem(i);
        item->setHidden((!search.isEmpty() && !item->text(0).contains(search, Qt::CaseInsensitive)
            && !item->text(2).contains(search, Qt::CaseInsensitive)) || (!type.isEmpty() && item->text(1) != type));
    }
}
// Metadata-only UI: virtual resources work exactly like files.
class AssetPicker final : public QDialog
{
public:
    AssetPicker(IAssetProvider& provider, QWidget* parent = nullptr) : QDialog(parent), provider_(provider) {
        setWindowTitle("Choose Asset"); resize(640, 420);
        auto* layout = new QVBoxLayout(this);
        search_ = new QLineEdit(this); search_->setObjectName("AssetPickerSearch"); search_->setPlaceholderText("Search assets");
        layout->addWidget(search_);
        tree_ = new QTreeWidget(this); tree_->setObjectName("AssetPickerTree");
        tree_->setHeaderLabels({"Name", "Type", "Source"}); tree_->setRootIsDecorated(false);
        tree_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        layout->addWidget(tree_);
        for (const auto& asset : provider.GetAssets()) {
            auto* item = new QTreeWidgetItem(tree_, {QString::fromStdString(asset.name), QString::fromStdString(asset.type), QString::fromStdString(asset.sourcePath)});
            item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(asset.id));
        }
        connect(search_, &QLineEdit::textChanged, this, [this] { FilterAssetItems(*tree_, search_->text(), {}); });
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this); layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, [this] { if (Selected()) accept(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this] { if (Selected()) accept(); });
    }
    std::optional<AssetInfo> Selected() const {
        const auto* item = tree_->currentItem();
        return item && !item->isHidden() ? provider_.GetAsset(item->data(0, Qt::UserRole).toULongLong()) : std::nullopt;
    }
private:
    IAssetProvider& provider_; QLineEdit* search_; QTreeWidget* tree_;
};
class AssetOpenHandlers
{
public:
    bool Register(const std::string& type, QObject& owner, std::function<bool(const AssetInfo&)> open) {
        if (auto found = handlers_.find(type); found != handlers_.end() && found->second.owner) return false;
        handlers_[type] = {&owner, std::move(open)}; return true;
    }
    void Unregister(QObject& owner) {
        for (auto it = handlers_.begin(); it != handlers_.end();)
            if (it->second.owner == &owner) it = handlers_.erase(it); else ++it;
    }
    bool Open(const AssetInfo& asset) const {
        const auto found = handlers_.find(asset.type);
        return found != handlers_.end() && found->second.owner && found->second.open(asset);
    }
private:
    struct Handler { QPointer<QObject> owner; std::function<bool(const AssetInfo&)> open; };
    std::map<std::string, Handler> handlers_;
};
