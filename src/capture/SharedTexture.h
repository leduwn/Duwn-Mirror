#pragma once
// SharedTexture — Milestone 5: share D3D11 texture with OBS plugin via DXGI handle.
// Milestone 0 stub only.

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

namespace duwn::capture {
using Microsoft::WRL::ComPtr;

class SharedTexture {
public:
    // Create a shareable BGRA texture. Returns shared handle for IPC.
    bool Create(ID3D11Device* device, uint32_t width, uint32_t height) noexcept;

    HANDLE SharedHandle() const noexcept { return m_shared_handle; }
    ID3D11Texture2D* Texture() const noexcept { return m_texture.Get(); }

private:
    ComPtr<ID3D11Texture2D> m_texture;
    HANDLE                  m_shared_handle{nullptr};
};

} // namespace duwn::capture
