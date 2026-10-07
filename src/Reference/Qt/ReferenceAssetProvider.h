#pragma once

#include "Contracts/IAssetProvider.h"
#include "Reference/Assets/AssetCatalog.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

// File metadata only: recursive scan, absolute source paths, session-local IDs.
class ReferenceAssetProvider final : public IAssetProvider
{
public:
    explicit ReferenceAssetProvider(const QString& assetsDirectory)
        { SetDirectory(assetsDirectory); }
    void SetDirectory(const QString& directory) {
        directory_ = directory.isEmpty() ? QString{} : QDir(directory).absolutePath();
    }

    std::vector<AssetInfo> GetAssets() const override { return catalog_.GetAssets(); }

    std::optional<AssetInfo> GetAsset(AssetId id) const override
    {
        return catalog_.GetAsset(id);
    }

    void Refresh() override
    {
        std::vector<AssetCatalog::Source> sources;
        if (directory_.isEmpty()) { catalog_.Refresh({}); return; }
        QDirIterator files(directory_, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
            QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QFileInfo file(files.next());
            sources.push_back({file.absoluteFilePath().toStdString(), file.fileName().toStdString(), file.suffix().toStdString()});
        }
        catalog_.Refresh(std::move(sources));
    }

private:
    QString directory_;
    AssetCatalog catalog_;
};
