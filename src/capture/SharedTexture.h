#pragma once
// SharedTexture — D3D11 shared texture for zero-copy GPU export to OBS / TikTok Live Studio.

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

namespace duwn::capture {
using Microsoft::WRL::ComPtr;

class SharedTexture {
public:
    SharedTexture() noexcept = default;
    ~SharedTexture() { Release(); }

    SharedTexture(const SharedTexture&) = delete;
    SharedTexture& operator=(const SharedTexture&) = delete;

    // Create a shareable BGRA texture. Returns shared handle for IPC.
    bool Create(ID3D11Device* device, uint32_t width, uint32_t height) noexcept;
    void Release() noexcept;

    HANDLE SharedHandle() const noexcept { return m_shared_handle; }
    ID3D11Texture2D* Texture() const noexcept { return m_texture.Get(); }
    uint32_t Width() const noexcept { return m_width; }
    uint32_t Height() const noexcept { return m_height; }

private:
    ComPtr<ID3D11Texture2D> m_texture;
    HANDLE                  m_shared_handle{nullptr};
    uint32_t                m_width{0};
    uint32_t                m_height{0};
};

} // namespace duwn::capture
