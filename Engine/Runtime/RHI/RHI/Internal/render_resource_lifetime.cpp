// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "i_render_device.h"
#include <algorithm>
#include <mutex>
#include <utility>

namespace UVE::Render {
struct IRenderDevice::TextureRetirementState {
    struct Batch {
        std::vector<TextureHandle> textures;
        Batch *next = nullptr;
    };
    struct Lease {
        explicit Lease(const std::shared_ptr<TextureRetirementState> &owner) : state(owner) {}
        std::weak_ptr<TextureRetirementState> state;
        std::unique_ptr<Batch> batch;
        ~Lease() {
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
    Batch *head = nullptr;
    ~TextureRetirementState() {
        while (head) {
            auto *next = head->next;
            delete head;
            head = next;
        }
    }
};
IRenderDevice::IRenderDevice() : m_textureRetirement(std::make_shared<TextureRetirementState>()) {}
IRenderDevice::~IRenderDevice() = default;
std::shared_ptr<void> IRenderDevice::AdoptTransientTextures(std::vector<TextureHandle> &&textures) {
    if (!IsUsable() || GetCapabilities().retainedTextures == RenderFeatureSupport::Unsupported ||
        textures.empty())
        return {};
    for (std::size_t index = 0U; index < textures.size(); ++index) {
        if (textures[index] == kInvalidTextureHandle ||
            std::find(textures.begin(), textures.begin() + static_cast<std::ptrdiff_t>(index),
                      textures[index]) != textures.begin() + static_cast<std::ptrdiff_t>(index))
            return {};
        TextureDesc description;
        if (GetTextureDesc(textures[index], description) != RenderOperationResult::Success)
            return {};
    }
    auto lease = std::make_shared<TextureRetirementState::Lease>(m_textureRetirement);
    lease->batch = std::make_unique<TextureRetirementState::Batch>();
    lease->batch->textures =
        std::move(textures); // all potentially throwing allocation precedes ownership transfer
    textures.clear();
    return lease;
}
std::size_t IRenderDevice::CollectRetiredTextures() {
    TextureRetirementState::Batch *batches = nullptr;
    {
        const std::lock_guard lock(m_textureRetirement->mutex);
        batches = std::exchange(m_textureRetirement->head, nullptr);
    }
    std::size_t count = 0U;
    while (batches) {
        std::unique_ptr<TextureRetirementState::Batch> batch(batches);
        batches = batch->next;
        for (auto texture : batch->textures) {
            DestroyTexture(texture);
            ++count;
        }
    }
    return count;
}
} // namespace UVE::Render
