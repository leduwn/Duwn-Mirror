#include "SharedTexture.h"

namespace duwn::capture {

void SharedTexture::Release() noexcept {
    m_texture.Reset();
    m_shared_handle = nullptr;
    m_width = 0;
    m_height = 0;
}

bool SharedTexture::Create(ID3D11Device* device, uint32_t width, uint32_t height) noexcept {
    if (!device || width == 0 || height == 0) return false;

    if (m_texture && m_width == width && m_height == height) {
        return true;
    }

    Release();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width            = width;
    desc.Height           = height;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc       = {1, 0};
    desc.Usage            = D3D11_USAGE_DEFAULT;
    desc.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags        = D3D11_RESOURCE_MISC_SHARED;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, m_texture.GetAddressOf());
    if (FAILED(hr)) return false;

    ComPtr<IDXGIResource> dxgi_res;
    hr = m_texture.As(&dxgi_res);
    if (FAILED(hr) || !dxgi_res) {
        m_texture.Reset();
        return false;
    }

    hr = dxgi_res->GetSharedHandle(&m_shared_handle);
    if (FAILED(hr) || !m_shared_handle) {
        m_texture.Reset();
        return false;
    }

    m_width = width;
    m_height = height;
    return true;
}

} // namespace duwn::capture
