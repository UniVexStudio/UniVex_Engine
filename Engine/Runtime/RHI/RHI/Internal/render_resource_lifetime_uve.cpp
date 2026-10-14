// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/rhi/i_render_device_uve.h"
#include <algorithm>
#include <mutex>
#include <utility>

namespace UVE::Render {
struct IRenderDeviceUVE::TextureRetirementStateUVE {
    struct BatchUVE {
        std::vector<TextureHandleUVE> textures;
        BatchUVE *next = nullptr;
    };
    struct LeaseUVE {
        explicit LeaseUVE(const std::shared_ptr<TextureRetirementStateUVE> &owner) : state(owner) {}
        std::weak_ptr<TextureRetirementStateUVE> state;
        std::unique_ptr<BatchUVE> batch;
        ~LeaseUVE() {
            if (!batch)
                return;
            if (auto owner = state.lock()) {
                const std::lock_guard lock(owner->mutex);
                batch->next = owner->head;
                owner->head = batch.release(); // allocation-free, safe on submission threads
            }
            // An expired owner already destroyed its native resources. Only metadata is freed.
        }
    };
    std::mutex mutex;
    BatchUVE *head = nullptr;
    ~TextureRetirementStateUVE() {
        while (head) {
            auto *next = head->next;
            delete head;
            head = next;
        }
    }
};
IRenderDeviceUVE::IRenderDeviceUVE() : m_textureRetirement(std::make_shared<TextureRetirementStateUVE>()) {}
IRenderDeviceUVE::~IRenderDeviceUVE() = default;
std::shared_ptr<void> IRenderDeviceUVE::AdoptTransientTexturesUVE(std::vector<TextureHandleUVE> &&textures) {
    if (!IsUsableUVE() || GetCapabilitiesUVE().retainedTextures == RenderFeatureSupportUVE::Unsupported ||
        textures.empty())
        return {};
    for (std::size_t index = 0U; index < textures.size(); ++index) {
        if (textures[index] == kInvalidTextureHandleUVE ||
            std::find(textures.begin(), textures.begin() + static_cast<std::ptrdiff_t>(index),
                      textures[index]) != textures.begin() + static_cast<std::ptrdiff_t>(index))
            return {};
        TextureDescUVE description;
        if (GetTextureDescUVE(textures[index], description) != RenderOperationResultUVE::Success)
            return {};
    }
    auto lease = std::make_shared<TextureRetirementStateUVE::LeaseUVE>(m_textureRetirement);
    lease->batch = std::make_unique<TextureRetirementStateUVE::BatchUVE>();
    lease->batch->textures =
        std::move(textures); // all potentially throwing allocation precedes ownership transfer
    textures.clear();
    return lease;
}
std::size_t IRenderDeviceUVE::CollectRetiredTexturesUVE() {
    TextureRetirementStateUVE::BatchUVE *batches = nullptr;
    {
        const std::lock_guard lock(m_textureRetirement->mutex);
        batches = std::exchange(m_textureRetirement->head, nullptr);
    }
    std::size_t count = 0U;
    while (batches) {
        std::unique_ptr<TextureRetirementStateUVE::BatchUVE> batch(batches);
        batches = batch->next;
        for (auto texture : batch->textures) {
            DestroyTextureUVE(texture);
            ++count;
        }
    }
    return count;
}
} // namespace UVE::Render
