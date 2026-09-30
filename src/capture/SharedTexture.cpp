#include "SharedTexture.h"

namespace duwn::capture {

bool SharedTexture::Create(ID3D11Device* device, uint32_t width, uint32_t height) noexcept {
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
    m_texture.As(&dxgi_res);
    if (!dxgi_res) return false;

    hr = dxgi_res->GetSharedHandle(&m_shared_handle);
    return SUCCEEDED(hr);
}

} // namespace duwn::capture
