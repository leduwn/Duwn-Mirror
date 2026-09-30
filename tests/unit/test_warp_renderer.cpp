#include "video/WarpVideoRenderer.h"
#include <mfapi.h>
#include <vector>
#include <algorithm>

DUWN_TEST(warp_software_nv12_frame_presents) {
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DUWN_ASSERT(SUCCEEDED(::MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET)));
    struct MfGuard { ~MfGuard() { ::MFShutdown(); ::CoUninitialize(); } } mf_guard;

    duwn::video::D3D11Device device;
    DUWN_ASSERT(device.Create(true));
    HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"WARP test", WS_OVERLAPPEDWINDOW,
                                  0, 0, 640, 360, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    DUWN_ASSERT(hwnd != nullptr);
    struct WindowGuard { HWND hwnd; ~WindowGuard() { ::DestroyWindow(hwnd); } } window_guard{hwnd};
    duwn::video::WarpVideoRenderer renderer(device, hwnd);
    DUWN_ASSERT(renderer.Init(640, 360));

    std::vector<uint8_t> nv12(640 * 360 * 3 / 2, 128);
    std::fill(nv12.begin(), nv12.begin() + 640 * 360, static_cast<uint8_t>(16));
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 640;
    desc.Height = 360;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = nv12.data();
    initial.SysMemPitch = 640;
    initial.SysMemSlicePitch = static_cast<UINT>(nv12.size());
    duwn::video::ComPtr<ID3D11Texture2D> texture;
    DUWN_ASSERT(SUCCEEDED(device.Device()->CreateTexture2D(&desc, &initial, texture.GetAddressOf())));
    duwn::video::VideoFrame frame;
    frame.texture = texture;
    frame.width = frame.visible_width = 640;
    frame.height = frame.visible_height = 360;
    frame.format = DXGI_FORMAT_NV12;
    DUWN_ASSERT(renderer.Present(frame, true) == duwn::video::PresentResult::Ok);
}
