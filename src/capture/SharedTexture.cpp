#include "SharedTexture.h"
#include <dxgi.h>

namespace duwn::capture {

void SharedTexture::Release() noexcept {
    for (uint32_t i = 0; i < kSharedTextureRingSize; ++i) {
        m_textures[i].Reset();
        m_queries[i].Reset();
        // Legacy DXGI shared handles are freed when all D3D references are released.
        // Never call CloseHandle on legacy shared handles.
        m_shared_handles[i] = nullptr;
    }
    m_width = 0;
    m_height = 0;
}

bool SharedTexture::Create(ID3D11Device* device, uint32_t width, uint32_t height) noexcept {
    if (!device || width == 0 || height == 0) return false;

    if (m_textures[0] && m_width == width && m_height == height) {
        return true;
    }

    Release();

    // Query DXGI Adapter LUID
    ComPtr<IDXGIDevice> dxgi_dev;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi_dev))) && dxgi_dev) {
        ComPtr<IDXGIAdapter> adapter;
        if (SUCCEEDED(dxgi_dev->GetAdapter(&adapter)) && adapter) {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(adapter->GetDesc(&desc))) {
                m_adapter_luid = desc.AdapterLuid;
            }
        }
    }

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

    D3D11_QUERY_DESC qdesc{};
    qdesc.Query = D3D11_QUERY_EVENT;

    for (uint32_t i = 0; i < kSharedTextureRingSize; ++i) {
        HRESULT hr = device->CreateTexture2D(&desc, nullptr, m_textures[i].GetAddressOf());
        if (FAILED(hr)) {
            Release();
            return false;
        }

        ComPtr<IDXGIResource> dxgi_res;
        hr = m_textures[i].As(&dxgi_res);
        if (FAILED(hr) || !dxgi_res) {
            Release();
            return false;
        }

        hr = dxgi_res->GetSharedHandle(&m_shared_handles[i]);
        if (FAILED(hr) || !m_shared_handles[i]) {
            Release();
            return false;
        }

        hr = device->CreateQuery(&qdesc, m_queries[i].GetAddressOf());
        if (FAILED(hr)) {
            Release();
            return false;
        }
    }

    m_width = width;
    m_height = height;
    ++m_resource_generation;
    return true;
}

bool SharedTexture::SyncGpu(ID3D11DeviceContext* context, uint32_t ring_index) noexcept {
    if (!context || ring_index >= kSharedTextureRingSize || !m_queries[ring_index]) return false;

    context->End(m_queries[ring_index].Get());
    context->Flush();

    // Query completion check with timeout to avoid freezing producer pipeline
    LARGE_INTEGER freq{}, start{}, now{};
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&start);
    const int64_t max_ticks = (freq.QuadPart * 5) / 1000; // 5ms maximum spin wait

    while (context->GetData(m_queries[ring_index].Get(), nullptr, 0, 0) == S_FALSE) {
        ::QueryPerformanceCounter(&now);
        if (now.QuadPart - start.QuadPart > max_ticks) {
            break; // Timeout, continue safely
        }
        YieldProcessor();
    }
    return true;
}

} // namespace duwn::capture
