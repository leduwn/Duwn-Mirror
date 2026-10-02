// capture_consumer_harness.cpp — Independent consumer process validating DUWN_MIRROR_CAPTURE protocol
#include "capture/CaptureServer.h"
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <set>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;
using duwn::capture::CaptureMemoryHeader;

static void WriteBmp(const std::string& path, uint32_t width, uint32_t height, const uint8_t* bgra_data, uint32_t row_pitch) {
    BITMAPFILEHEADER bfh{};
    BITMAPINFOHEADER bih{};

    uint32_t image_size = width * height * 4;
    bfh.bfType = 0x4D42; // "BM"
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + image_size;

    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = static_cast<LONG>(width);
    bih.biHeight = -static_cast<LONG>(height); // top-down
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = image_size;

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return;

    out.write(reinterpret_cast<const char*>(&bfh), sizeof(bfh));
    out.write(reinterpret_cast<const char*>(&bih), sizeof(bih));

    for (uint32_t y = 0; y < height; ++y) {
        const char* row = reinterpret_cast<const char*>(bgra_data + (y * row_pitch));
        out.write(row, width * 4);
    }
}

int main(int argc, char* argv[]) {
    int iterations = 30;
    bool reopen_test = false;
    std::string export_bmp;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--iterations" && i + 1 < argc) {
            iterations = std::atoi(argv[++i]);
        } else if (arg == "--reopen-test") {
            reopen_test = true;
        } else if (arg == "--export-raw-frame" && i + 1 < argc) {
            export_bmp = argv[++i];
        }
    }

    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                   D3D11_SDK_VERSION, &device, &fl, &context);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &device, &fl, &context);
    }
    if (FAILED(hr) || !device) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"D3D11CreateDevice failed\"}\n";
        return 1;
    }

    HANDLE h_map = nullptr;
    auto start_wait = std::chrono::steady_clock::now();
    while (!h_map) {
        h_map = ::OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\DUWN_MIRROR_CAPTURE");
        if (!h_map) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_wait).count();
            if (elapsed > 10) {
                std::cerr << "{\"status\":\"FAIL\",\"error\":\"Timed out waiting for Local\\\\DUWN_MIRROR_CAPTURE\"}\n";
                return 2;
            }
            ::Sleep(100);
        }
    }

    auto* header = static_cast<const CaptureMemoryHeader*>(
        ::MapViewOfFile(h_map, FILE_MAP_READ, 0, 0, sizeof(CaptureMemoryHeader)));
    if (!header) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"MapViewOfFile failed\"}\n";
        ::CloseHandle(h_map);
        return 3;
    }

    HANDLE h_event = ::OpenEventW(SYNCHRONIZE, FALSE, L"Local\\DUWN_MIRROR_CAPTURE_FRAME_READY");
    if (!h_event) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"OpenEventW failed\"}\n";
        ::UnmapViewOfFile(header);
        ::CloseHandle(h_map);
        return 4;
    }
    uint64_t last_frame_index = 0;
    uint32_t verified_frames = 0;
    std::set<uint32_t> sample_hashes;
    uint32_t captured_w = 0;
    uint32_t captured_h = 0;
    uint32_t captured_fmt = 0;

    ComPtr<ID3D11Texture2D> staging_tex;
    uint32_t staging_w = 0;
    uint32_t staging_h = 0;

    for (int i = 0; i < iterations; ++i) {
        DWORD wr = ::WaitForSingleObject(h_event, 1000);
        if (wr != WAIT_OBJECT_0) {
            std::cerr << "{\"status\":\"FAIL\",\"error\":\"WaitForSingleObject timed out\"}\n";
            ::UnmapViewOfFile(header);
            ::CloseHandle(h_event);
            ::CloseHandle(h_map);
            return 5;
        }

        if (std::memcmp(header->magic, "DUWNCAP", 7) != 0 || header->version != 1) {
            std::cerr << "{\"status\":\"FAIL\",\"error\":\"Invalid magic or version\"}\n";
            return 6;
        }

        uint64_t f_idx = header->frame_index;
        if (f_idx <= last_frame_index && last_frame_index != 0) {
            std::cerr << "{\"status\":\"FAIL\",\"error\":\"Frame index did not increment\"}\n";
            return 7;
        }
        last_frame_index = f_idx;

        captured_w = header->width;
        captured_h = header->height;
        captured_fmt = header->dxgi_format;
        HANDLE shared_h = reinterpret_cast<HANDLE>(header->shared_handle);

        if (!shared_h || captured_w == 0 || captured_h == 0) {
            std::cerr << "{\"status\":\"FAIL\",\"error\":\"Empty shared handle or dimensions\"}\n";
            return 8;
        }

        ComPtr<ID3D11Texture2D> shared_tex;
        hr = device->OpenSharedResource(shared_h, IID_PPV_ARGS(&shared_tex));
        if (FAILED(hr) || !shared_tex) {
            std::cerr << "{\"status\":\"FAIL\",\"error\":\"OpenSharedResource failed\"}\n";
            return 9;
        }

        D3D11_TEXTURE2D_DESC desc{};
        shared_tex->GetDesc(&desc);
        if (desc.Width != captured_w || desc.Height != captured_h || desc.Format != static_cast<DXGI_FORMAT>(captured_fmt)) {
            std::cerr << "{\"status\":\"FAIL\",\"error\":\"Texture descriptor mismatch\"}\n";
            return 10;
        }

        if (!staging_tex || staging_w != captured_w || staging_h != captured_h) {
            D3D11_TEXTURE2D_DESC sdesc = desc;
            sdesc.Usage = D3D11_USAGE_STAGING;
            sdesc.BindFlags = 0;
            sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            sdesc.MiscFlags = 0;
            hr = device->CreateTexture2D(&sdesc, nullptr, staging_tex.ReleaseAndGetAddressOf());
            if (FAILED(hr) || !staging_tex) return 11;
            staging_w = captured_w;
            staging_h = captured_h;
        }

        context->CopyResource(staging_tex.Get(), shared_tex.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = context->Map(staging_tex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr)) {
            const uint8_t* pixels = static_cast<const uint8_t*>(mapped.pData);
            uint32_t hash = 2166136261u;
            for (uint32_t sy = 100; sy < captured_h; sy += 100) {
                const uint32_t* row = reinterpret_cast<const uint32_t*>(pixels + (sy * mapped.RowPitch));
                for (uint32_t sx = 100; sx < captured_w; sx += 100) {
                    hash = (hash ^ row[sx]) * 16777619u;
                }
            }
            sample_hashes.insert(hash);

            if (i == iterations - 1 && !export_bmp.empty()) {
                WriteBmp(export_bmp, captured_w, captured_h, pixels, mapped.RowPitch);
            }
            context->Unmap(staging_tex.Get(), 0);
        }
        verified_frames++;
    }

    bool reopen_ok = true;
    if (reopen_test) {
        staging_tex.Reset();
        ::UnmapViewOfFile(header);
        ::CloseHandle(h_event);
        ::CloseHandle(h_map);

        ::Sleep(400);

        h_map = ::OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\DUWN_MIRROR_CAPTURE");
        if (!h_map) reopen_ok = false;
        else {
            header = static_cast<const CaptureMemoryHeader*>(
                ::MapViewOfFile(h_map, FILE_MAP_READ, 0, 0, sizeof(CaptureMemoryHeader)));
            h_event = ::OpenEventW(SYNCHRONIZE, FALSE, L"Local\\DUWN_MIRROR_CAPTURE_FRAME_READY");
            if (!header || !h_event) reopen_ok = false;
            else {
                for (int r = 0; r < 10; ++r) {
                    if (::WaitForSingleObject(h_event, 1000) == WAIT_OBJECT_0) {
                        uint64_t fresh_idx = header->frame_index;
                        if (fresh_idx > last_frame_index) {
                            last_frame_index = fresh_idx;
                            verified_frames++;
                        }
                    }
                }
            }
            if (header) ::UnmapViewOfFile(header);
            if (h_event) ::CloseHandle(h_event);
            if (h_map) ::CloseHandle(h_map);
        }
    } else {
        ::UnmapViewOfFile(header);
        ::CloseHandle(h_event);
        ::CloseHandle(h_map);
    }

    bool pass = (verified_frames >= 20 && sample_hashes.size() > 4 && reopen_ok);
    std::cout << "{\n"
              << "  \"status\": \"" << (pass ? "PASS" : "FAIL") << "\",\n"
              << "  \"verified_frames\": " << verified_frames << ",\n"
              << "  \"distinct_hashes\": " << sample_hashes.size() << ",\n"
              << "  \"width\": " << captured_w << ",\n"
              << "  \"height\": " << captured_h << ",\n"
              << "  \"dxgi_format\": " << captured_fmt << ",\n"
              << "  \"last_frame_index\": " << last_frame_index << ",\n"
              << "  \"reopen_test\": \"" << (reopen_ok ? "PASS" : "FAIL") << "\",\n"
              << "  \"moving_frames_confirmed\": " << (sample_hashes.size() > 4 ? "true" : "false") << "\n"
              << "}\n";

    return pass ? 0 : 1;
}

