#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using AssetId = std::uint64_t;

struct AssetInfo
{
    AssetId id;
    std::string name;
    std::string type;
    std::string sourcePath;
};

// Synchronous editor-thread API. Strings are UTF-8; IDs remain stable for live
// assets across Refresh(). Getters read the latest snapshot, missing IDs return nullopt.
class IAssetProvider
{
public:
    virtual ~IAssetProvider() = default;

    virtual std::vector<AssetInfo> GetAssets() const = 0;
    virtual std::optional<AssetInfo> GetAsset(AssetId id) const = 0;
    virtual void Refresh() = 0;
};
