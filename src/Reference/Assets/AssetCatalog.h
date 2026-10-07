#pragma once
#include "Contracts/IAssetProvider.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>

// One metadata catalog; the filesystem adapter only supplies scan results.
class AssetCatalog
{
public:
    struct Source { std::string path, name, extension; };
    static std::string Classify(std::string extension) {
        for (auto& c : extension) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (extension == "png" || extension == "jpg") return "Texture";
        if (extension == "obj" || extension == "glb") return "Mesh";
        if (extension == "axl" || extension == "json") return "Scene";
        if (extension == "lua") return "Script";
        if (extension == "axlgraph") return "Graph";
        return "Unknown";
    }
    void Refresh(std::vector<Source> sources) {
        std::sort(sources.begin(), sources.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        std::vector<AssetInfo> assets;
        for (const auto& source : sources) {
            auto found = ids_.find(source.path);
            if (found == ids_.end()) {
                if (!nextId_) throw std::overflow_error("Asset IDs exhausted");
                found = ids_.emplace(source.path, nextId_++).first;
            }
            assets.push_back({found->second, source.name, Classify(source.extension), source.path});
        }
        assets_ = std::move(assets);
    }
    std::vector<AssetInfo> GetAssets() const { return assets_; }
    std::optional<AssetInfo> GetAsset(AssetId id) const {
        for (const auto& asset : assets_) if (asset.id == id) return asset;
        return std::nullopt;
    }
private:
    std::vector<AssetInfo> assets_;
    std::map<std::string, AssetId> ids_;
    AssetId nextId_ = 1;
};
