#pragma once

#include "Contracts/IAssetProvider.h"
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
        : directory_(QDir(assetsDirectory).absolutePath()) {}

    std::vector<AssetInfo> GetAssets() const override { return assets_; }

    std::optional<AssetInfo> GetAsset(AssetId id) const override
    {
        for (const auto& asset : assets_)
            if (asset.id == id)
                return asset;
        return std::nullopt;
    }

    void Refresh() override
    {
        QStringList paths;
        QDirIterator files(directory_, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
            QDirIterator::Subdirectories);
        while (files.hasNext())
            paths.push_back(QFileInfo(files.next()).absoluteFilePath());
        std::sort(paths.begin(), paths.end());

        std::vector<AssetInfo> assets;
        for (const auto& path : paths) {
            auto found = ids_.find(path);
            if (found == ids_.end()) {
                if (nextId_ == 0)
                    throw std::overflow_error("Asset IDs exhausted");
                found = ids_.emplace(path, nextId_++).first;
            }
            const QFileInfo file(path);
            assets.push_back({found->second, file.fileName().toStdString(),
                TypeFor(file.suffix().toLower()), path.toStdString()});
        }
        assets_ = std::move(assets);
    }

private:
    static std::string TypeFor(const QString& extension)
    {
        if (extension == "png" || extension == "jpg") return "Texture";
        if (extension == "obj" || extension == "glb") return "Mesh";
        if (extension == "axl" || extension == "json") return "Scene";
        if (extension == "lua") return "Script";
        return "Unknown";
    }

    QString directory_;
    std::vector<AssetInfo> assets_;
    std::map<QString, AssetId> ids_;
    AssetId nextId_ = 1;
};
