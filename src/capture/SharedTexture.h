#pragma once
// SharedTexture — D3D11 triple-buffered shared texture pool for GPU export.
// Legacy DXGI shared handles (D3D11_RESOURCE_MISC_SHARED) are managed strictly via
// OpenSharedResource on the consumer side. Handles must not be closed via CloseHandle.

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstdint>

namespace duwn::capture {
using Microsoft::WRL::ComPtr;

constexpr uint32_t kSharedTextureRingSize = 3;

class SharedTexture {
public:
    SharedTexture() noexcept = default;
    ~SharedTexture() { Release(); }

    SharedTexture(const SharedTexture&) = delete;
    SharedTexture& operator=(const SharedTexture&) = delete;

    // Create a triple-buffered ring of shareable BGRA textures. Returns true on success.
    bool Create(ID3D11Device* device, uint32_t width, uint32_t height) noexcept;
    void Release() noexcept;

    // Flush GPU command queue and verify hardware completion for the given ring slot
    bool SyncGpu(ID3D11DeviceContext* context, uint32_t ring_index) noexcept;

    HANDLE SharedHandle(uint32_t ring_index = 0) const noexcept {
        return (ring_index < kSharedTextureRingSize) ? m_shared_handles[ring_index] : nullptr;
    }
    const HANDLE* SharedHandles() const noexcept { return m_shared_handles; }

    ID3D11Texture2D* Texture(uint32_t ring_index = 0) const noexcept {
        return (ring_index < kSharedTextureRingSize) ? m_textures[ring_index].Get() : nullptr;
    }

    ID3D11Query* Query(uint32_t ring_index = 0) const noexcept {
        return (ring_index < kSharedTextureRingSize) ? m_queries[ring_index].Get() : nullptr;
    }

    uint32_t Width() const noexcept { return m_width; }
    uint32_t Height() const noexcept { return m_height; }
    uint32_t RingSize() const noexcept { return kSharedTextureRingSize; }
    uint32_t ResourceGeneration() const noexcept { return m_resource_generation; }
    LUID AdapterLuid() const noexcept { return m_adapter_luid; }

private:
    ComPtr<ID3D11Texture2D> m_textures[kSharedTextureRingSize];
    HANDLE                  m_shared_handles[kSharedTextureRingSize]{nullptr, nullptr, nullptr};
    ComPtr<ID3D11Query>     m_queries[kSharedTextureRingSize];
    uint32_t                m_width{0};
    uint32_t                m_height{0};
    uint32_t                m_resource_generation{0};
    LUID                    m_adapter_luid{0, 0};
};

} // namespace duwn::capture
