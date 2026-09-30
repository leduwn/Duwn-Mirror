#include "D3D11Device.h"
#include "common/logging/Logger.h"
#include <d3d10.h>
#include <mfapi.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <string>
#include <format>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

namespace duwn::video {

static std::string WideToUtf8(std::wstring_view w) noexcept {
    if (w.empty()) return {};
    int size = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string s(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), size, nullptr, nullptr);
    return s;
}

bool D3D11Device::Create(bool force_warp, bool allow_warp_fallback) noexcept {
    m_force_warp = force_warp;
    m_allow_warp_fallback = allow_warp_fallback;
    if (force_warp) return CreateInternal(false, false);
#ifdef _DEBUG
    // In Debug builds, attempt hardware device with D3D11_CREATE_DEVICE_DEBUG.
    // If the Windows Graphics Tools / SDK debug layer is not installed,
    // D3D11CreateDevice fails with DXGI_ERROR_SDK_COMPONENT_MISSING (0x887A002D) or E_FAIL.
    // In that case, log warning and retry hardware without the debug flag.
    // Do NOT fall back to WARP just because the debug layer is missing.
    if (CreateInternal(/*hardware=*/true, /*enable_debug=*/true)) return true;
    DUWN_LOG_WARN("D3D11Device",
        "Hardware D3D11CreateDevice with D3D11_CREATE_DEVICE_DEBUG failed; "
        "retrying hardware device without debug layer");
    if (CreateInternal(/*hardware=*/true, /*enable_debug=*/false)) return true;
#else
    if (CreateInternal(/*hardware=*/true, /*enable_debug=*/false)) return true;
#endif

    // Hardware retry without debug flag occurred above before this WARP fallback.
    if (!allow_warp_fallback) return false;
    DUWN_LOG_WARN("D3D11Device", "Hardware device failed; falling back to WARP");
    return CreateInternal(/*hardware=*/false, /*enable_debug=*/false);
}

bool D3D11Device::Reset() noexcept {
    m_device.Reset();
    m_context.Reset();
    m_adapter.Reset();
    m_mf_dev_mgr.Reset();
    return Create(m_force_warp, m_allow_warp_fallback);
}

bool D3D11Device::CreateInternal(bool hardware, bool enable_debug) noexcept {
    m_device.Reset();
    m_context.Reset();
    m_adapter.Reset();
    m_mf_dev_mgr.Reset();
    m_adapter_info = {};

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (hardware) flags |= D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    if (enable_debug) {
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    }

    constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
    };
    D3D_FEATURE_LEVEL achieved{};
    HRESULT hr = E_FAIL;

    if (hardware) {
        // Multi-adapter enumeration:
        // Query DXGIFactory (prefer IDXGIFactory6 for GPU preference, fallback to IDXGIFactory1).
        ComPtr<IDXGIFactory1> factory1;
        if (SUCCEEDED(::CreateDXGIFactory1(IID_PPV_ARGS(&factory1)))) {
            ComPtr<IDXGIFactory6> factory6;
            factory1.As(&factory6);

            struct CandidateAdapter {
                ComPtr<IDXGIAdapter1> adapter;
                DXGI_ADAPTER_DESC1    desc{};
                bool                  has_outputs{false};
            };
            std::vector<CandidateAdapter> candidates;

            auto enumerate_adapter = [&](IDXGIAdapter1* ad) {
                if (!ad) return;
                DXGI_ADAPTER_DESC1 desc{};
                if (FAILED(ad->GetDesc1(&desc))) return;
                // Skip software/WARP in hardware pass
                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) return;

                ComPtr<IDXGIOutput> out;
                bool has_outputs = (ad->EnumOutputs(0, &out) == S_OK);
                candidates.push_back({ad, desc, has_outputs});
            };

            if (factory6) {
                // High-performance preference first (dGPU on dual-GPU laptops)
                ComPtr<IDXGIAdapter1> ad1;
                for (UINT i = 0; SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                         i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&ad1))); ++i) {
                    enumerate_adapter(ad1.Get());
                    ad1.Reset();
                }
            }
            if (candidates.empty()) {
                // Standard enumeration fallback
                ComPtr<IDXGIAdapter1> ad1;
                for (UINT i = 0; SUCCEEDED(factory1->EnumAdapters1(i, &ad1)); ++i) {
                    enumerate_adapter(ad1.Get());
                    ad1.Reset();
                }
            }

            // Test candidate adapters in preference order
            for (auto& cand : candidates) {
                hr = ::D3D11CreateDevice(
                    cand.adapter.Get(),
                    D3D_DRIVER_TYPE_UNKNOWN, // Required when pAdapter is non-null
                    nullptr,
                    flags,
                    kFeatureLevels, static_cast<UINT>(std::size(kFeatureLevels)),
                    D3D11_SDK_VERSION,
                    m_device.GetAddressOf(),
                    &achieved,
                    m_context.GetAddressOf()
                );

                if (SUCCEEDED(hr)) {
                    // Check video device support
                    ComPtr<ID3D11VideoDevice> video_dev;
                    if (SUCCEEDED(m_device.As(&video_dev))) {
                        m_adapter = cand.adapter;
                        m_adapter_info.description          = cand.desc.Description;
                        m_adapter_info.vendor_id            = cand.desc.VendorId;
                        m_adapter_info.device_id            = cand.desc.DeviceId;
                        m_adapter_info.dedicated_video_mem  = cand.desc.DedicatedVideoMemory;
                        m_adapter_info.dedicated_system_mem = cand.desc.DedicatedSystemMemory;
                        m_adapter_info.shared_system_mem    = cand.desc.SharedSystemMemory;
                        m_adapter_info.feature_level        = achieved;
                        m_adapter_info.is_hardware          = true;
                        m_adapter_info.is_warp              = false;
                        m_adapter_info.has_video_support    = true;
                        m_adapter_info.has_outputs          = cand.has_outputs;
                        break;
                    } else {
                        // Candidate lacks video support — release and try next
                        m_device.Reset();
                        m_context.Reset();
                    }
                }
            }
        }

        // If candidate enumeration didn't succeed, try default hardware adapter
        if (!m_device) {
            hr = ::D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                flags,
                kFeatureLevels, static_cast<UINT>(std::size(kFeatureLevels)),
                D3D11_SDK_VERSION,
                m_device.GetAddressOf(),
                &achieved,
                m_context.GetAddressOf()
            );
        }
    }

    if (!m_device) {
        // Fallback to WARP software renderer
        hr = ::D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            flags,
            kFeatureLevels, static_cast<UINT>(std::size(kFeatureLevels)),
            D3D11_SDK_VERSION,
            m_device.GetAddressOf(),
            &achieved,
            m_context.GetAddressOf()
        );
        if (FAILED(hr)) {
            if (!enable_debug) {
                DUWN_LOG_WARNF("D3D11Device",
                    "D3D11CreateDevice (WARP) failed HRESULT={:#010x}", static_cast<unsigned>(hr));
            }
            return false;
        }
        m_adapter_info.is_warp = true;
        m_adapter_info.is_hardware = false;
    }

    m_is_hardware = !m_adapter_info.is_warp;

    // Retrieve adapter if not set during candidate loop
    if (!m_adapter) {
        ComPtr<IDXGIDevice> dxgi_dev;
        m_device.As(&dxgi_dev);
        if (dxgi_dev) {
            dxgi_dev->GetAdapter(m_adapter.GetAddressOf());
        }
    }

    // Populate adapter info if not already populated
    if (m_adapter && m_adapter_info.description.empty()) {
        ComPtr<IDXGIAdapter1> ad1;
        m_adapter.As(&ad1);
        if (ad1) {
            DXGI_ADAPTER_DESC1 desc1{};
            if (SUCCEEDED(ad1->GetDesc1(&desc1))) {
                m_adapter_info.description          = desc1.Description;
                m_adapter_info.vendor_id            = desc1.VendorId;
                m_adapter_info.device_id            = desc1.DeviceId;
                m_adapter_info.dedicated_video_mem  = desc1.DedicatedVideoMemory;
                m_adapter_info.dedicated_system_mem = desc1.DedicatedSystemMemory;
                m_adapter_info.shared_system_mem    = desc1.SharedSystemMemory;
                m_adapter_info.is_warp              = (desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || !hardware;
                m_adapter_info.is_hardware          = !m_adapter_info.is_warp;
            }
        } else {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(m_adapter->GetDesc(&desc))) {
                m_adapter_info.description          = desc.Description;
                m_adapter_info.vendor_id            = desc.VendorId;
                m_adapter_info.device_id            = desc.DeviceId;
                m_adapter_info.dedicated_video_mem  = desc.DedicatedVideoMemory;
                m_adapter_info.dedicated_system_mem = desc.DedicatedSystemMemory;
                m_adapter_info.shared_system_mem    = desc.SharedSystemMemory;
                m_adapter_info.is_warp              = !hardware;
                m_adapter_info.is_hardware          = hardware;
            }
        }
        m_adapter_info.feature_level = achieved;
        ComPtr<ID3D11VideoDevice> vdev;
        m_adapter_info.has_video_support = SUCCEEDED(m_device.As(&vdev));
        ComPtr<IDXGIOutput> out;
        m_adapter_info.has_outputs = (m_adapter->EnumOutputs(0, &out) == S_OK);
    }

    // Enable multithreaded protection on D3D11 device (required for MF decoder)
    ComPtr<ID3D10Multithread> mt;
    m_device.As(&mt);
    if (mt) mt->SetMultithreadProtected(TRUE);

    // Create MF DXGI Device Manager so MF transforms can use our D3D11 device
    hr = ::MFCreateDXGIDeviceManager(&m_mf_reset_token, m_mf_dev_mgr.GetAddressOf());
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("D3D11Device",
            "MFCreateDXGIDeviceManager failed HRESULT={:#010x}", static_cast<unsigned>(hr));
        return false;
    }

    hr = m_mf_dev_mgr->ResetDevice(m_device.Get(), m_mf_reset_token);
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("D3D11Device",
            "IMFDXGIDeviceManager::ResetDevice failed HRESULT={:#010x}",
            static_cast<unsigned>(hr));
        return false;
    }

    DUWN_LOG_INFOF("D3D11Device",
        "Selected GPU: {} (Vendor={:#06x} {}, VRAM={} MB, Shared={} MB, FL={:#06x}, Video={})",
        WideToUtf8(m_adapter_info.description),
        m_adapter_info.vendor_id,
        VendorName(),
        m_adapter_info.dedicated_video_mem / (1024 * 1024),
        m_adapter_info.shared_system_mem / (1024 * 1024),
        static_cast<unsigned>(achieved),
        m_adapter_info.has_video_support ? "Yes" : "No");

    return true;
}

std::wstring D3D11Device::AdapterName() const noexcept {
    if (!m_adapter_info.description.empty()) return m_adapter_info.description;
    if (!m_adapter) return L"Unknown";
    DXGI_ADAPTER_DESC desc{};
    m_adapter->GetDesc(&desc);
    return desc.Description;
}

} // namespace duwn::video
