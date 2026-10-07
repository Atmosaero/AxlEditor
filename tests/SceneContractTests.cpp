// This target has no Qt includes, definitions or libraries.
#include "Contracts/ISceneProvider.h"
#include "Contracts/IAssetProvider.h"
#include "Contracts/IObjectProperties.h"
#include "Reference/Scene/ReferenceSceneProvider.h"
#include <stdexcept>
#include <type_traits>

static_assert(std::is_standard_layout_v<Transform> && std::is_trivially_copyable_v<Transform>);

int main()
{
    ReferenceSceneProvider scene;
    const auto parent = scene.CreateObject("Parent");
    const auto child = scene.CreateObject("Child", parent);
    auto transform = scene.GetTransform(child).value();
    transform.position = {2, 3, 4};
    if (!(scene.GetTransform(child)->position == Vec3{}))
        throw std::runtime_error("GetTransform must return a snapshot");
    scene.SetTransform(child, transform);
    if (!(scene.GetTransform(child)->position == Vec3{2, 3, 4}))
        throw std::runtime_error("Transform roundtrip failed");
    scene.DeleteObject(parent);
    try {
        (void)scene.GetTransform(child);
    } catch (const std::out_of_range&) {
        return 0;
    }
    throw std::runtime_error("Deleted child must not have a live Transform");
}
