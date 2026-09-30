#pragma once
// D3D11Device — shared D3D11 device + context for the entire pipeline.
// One device, created once, shared by decoder / processor / renderer.
// Handles DXGI_ERROR_DEVICE_REMOVED by triggering full recreation.

#include "PipelineTier.h"
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <mfidl.h>
#include <functional>
#include <mutex>
#include <string>

namespace duwn::video {

using Microsoft::WRL::ComPtr;

struct AdapterInfo {
    std::wstring description;
    uint32_t     vendor_id{0};
    uint32_t     device_id{0};
    size_t       dedicated_video_mem{0};
    size_t       dedicated_system_mem{0};
    size_t       shared_system_mem{0};
    D3D_FEATURE_LEVEL feature_level{D3D_FEATURE_LEVEL_11_0};
    bool         is_hardware{false};
    bool         is_warp{false};
    bool         has_video_support{false};
    bool         has_outputs{false};
};

class D3D11Device {
public:
    D3D11Device() = default;
    ~D3D11Device() = default;

    D3D11Device(const D3D11Device&) = delete;
    D3D11Device& operator=(const D3D11Device&) = delete;

    // Create D3D11 device + MF device manager.
    // Tries hardware (D3D_DRIVER_TYPE_HARDWARE) first.
    // On failure falls back to WARP software renderer.
    bool Create(bool force_warp = false, bool allow_warp_fallback = true) noexcept;

    // Recreate after device-removed / driver reset.
    bool Reset() noexcept;

    bool IsHardware() const noexcept { return m_is_hardware; }
    bool IsValid()    const noexcept { return m_device != nullptr; }

    ID3D11Device*        Device()        const noexcept { return m_device.Get(); }
    ID3D11DeviceContext* Context()       const noexcept { return m_context.Get(); }
    IDXGIAdapter*        Adapter()       const noexcept { return m_adapter.Get(); }
    IMFDXGIDeviceManager* MFDevManager() const noexcept { return m_mf_dev_mgr.Get(); }

    // Lock/unlock device context for multi-threaded use.
    // Prefer immediate context on the render thread; use deferred contexts elsewhere.
    std::mutex& ContextMutex() noexcept { return m_context_mutex; }

    // Retrieve the adapter description string and metadata.
    std::wstring AdapterName() const noexcept;
    const AdapterInfo& GetAdapterInfo() const noexcept { return m_adapter_info; }
    const char* VendorName() const noexcept { return duwn::video::VendorName(m_adapter_info.vendor_id); }

private:
    bool CreateInternal(bool hardware, bool enable_debug) noexcept;

    ComPtr<ID3D11Device>        m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGIAdapter>        m_adapter;
    ComPtr<IMFDXGIDeviceManager> m_mf_dev_mgr;
    UINT                        m_mf_reset_token{0};
    bool                        m_is_hardware{false};
    bool                        m_force_warp{false};
    bool                        m_allow_warp_fallback{true};
    AdapterInfo                 m_adapter_info;
    mutable std::mutex          m_context_mutex;
};

} // namespace duwn::video
