#include "SharedTexture.h"
#include "common/logging/Logger.h"
#include <dxgi.h>

namespace duwn::capture {

void SharedTexture::Release() noexcept {
    for (uint32_t i = 0; i < kSharedTextureRingSize; ++i) {
        m_textures[i].Reset();
        m_queries[i].Reset();
        // Legacy DXGI shared handles are freed when all D3D references are released.
        // Never call CloseHandle on legacy shared handles.
        m_shared_handles[i] = nullptr;
        m_query_issued[i] = false;
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
        m_query_issued[i] = false;
    }

    m_width = width;
    m_height = height;
    ++m_resource_generation;
    return true;
}

bool SharedTexture::RecreateSlot(ID3D11Device* device, uint32_t ring_index) noexcept {
    if (!device || ring_index >= kSharedTextureRingSize || m_width == 0 || m_height == 0) return false;

    // Reset COM references to retire the old resource
    m_textures[ring_index].Reset();
    m_queries[ring_index].Reset();
    m_shared_handles[ring_index] = nullptr;
    m_query_issued[ring_index] = false;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width            = m_width;
    desc.Height           = m_height;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc       = {1, 0};
    desc.Usage            = D3D11_USAGE_DEFAULT;
    desc.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags        = D3D11_RESOURCE_MISC_SHARED;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, m_textures[ring_index].GetAddressOf());
    if (FAILED(hr)) return false;

    ComPtr<IDXGIResource> dxgi_res;
    hr = m_textures[ring_index].As(&dxgi_res);
    if (FAILED(hr) || !dxgi_res) return false;

    hr = dxgi_res->GetSharedHandle(&m_shared_handles[ring_index]);
    if (FAILED(hr) || !m_shared_handles[ring_index]) return false;

    D3D11_QUERY_DESC qdesc{};
    qdesc.Query = D3D11_QUERY_EVENT;
    hr = device->CreateQuery(&qdesc, m_queries[ring_index].GetAddressOf());
    if (FAILED(hr)) return false;

    ++m_resource_generation;
    return true;
}

bool SharedTexture::SyncGpu(ID3D11DeviceContext* context, uint32_t ring_index, std::mutex* mutex, uint32_t timeout_ms) noexcept {
    if (!context || ring_index >= kSharedTextureRingSize) return false;
    if (!m_queries[ring_index]) return true;
    if (!m_query_issued[ring_index]) return true; // Query never issued; nothing to wait for

    // Flush command buffer to guarantee all blit/copy commands are submitted to GPU hardware
    if (mutex) {
        std::lock_guard lock{*mutex};
        context->Flush();
    } else {
        context->Flush();
    }

    // Verify GPU completion of the current frame copy before publishing to consumers
    LARGE_INTEGER freq{}, start{}, now{};
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&start);
    const int64_t max_ticks = (freq.QuadPart * timeout_ms) / 1000;

    auto poll_data = [&]() -> HRESULT {
        if (mutex) {
            std::lock_guard lock{*mutex};
            return context->GetData(m_queries[ring_index].Get(), nullptr, 0, 0);
        }
        return context->GetData(m_queries[ring_index].Get(), nullptr, 0, 0);
    };

    int poll_count = 0;
    HRESULT hr = S_FALSE;
    while ((hr = poll_data()) == S_FALSE) {
        ++poll_count;
        ::QueryPerformanceCounter(&now);
        if (now.QuadPart - start.QuadPart > max_ticks) {
            DUWN_LOG_WARNF("SharedTexture", "SyncGpu TIMEOUT slot={} query={} poll_count={} elapsed_ticks={}",
                           ring_index, (void*)m_queries[ring_index].Get(), poll_count, now.QuadPart - start.QuadPart);
            return false;
        }
        if (poll_count > 64) {
            ::Sleep(0);
        } else {
            YieldProcessor();
        }
    }
    return SUCCEEDED(hr);
}

bool SharedTexture::EnsureSlotReady(ID3D11DeviceContext* context, uint32_t ring_index, std::mutex* mutex) noexcept {
    if (!context || ring_index >= kSharedTextureRingSize || !m_queries[ring_index]) return true;
    if (!m_query_issued[ring_index]) return true; // Brand new slot, no prior GPU work

    // Check if GPU has completed operations from prior cycle on this slot.
    // On a quad-buffered ring (4 slots ~66ms), this returns S_OK immediately without CPU stall.
    LARGE_INTEGER freq{}, start{}, now{};
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&start);
    const int64_t max_ticks = (freq.QuadPart * 3) / 1000;

    auto poll_data = [&]() -> HRESULT {
        if (mutex) {
            std::lock_guard lock{*mutex};
            return context->GetData(m_queries[ring_index].Get(), nullptr, 0, 0);
        }
        return context->GetData(m_queries[ring_index].Get(), nullptr, 0, 0);
    };

    int poll_count = 0;
    while (poll_data() == S_FALSE) {
        ++poll_count;
        ::QueryPerformanceCounter(&now);
        if (now.QuadPart - start.QuadPart > max_ticks) {
            return false;
        }
        if (poll_count > 32) {
            ::Sleep(0);
        } else {
            YieldProcessor();
        }
    }
    return true;
}

} // namespace duwn::capture
