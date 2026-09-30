#include "video/PipelineTier.h"
#include "video/D3D11Device.h"
#include "video/MFVideoDecoder.h"
#include <string_view>
#include <cstdio>

DUWN_TEST(PipelineTierNames) {
    using namespace duwn::video;
    DUWN_ASSERT(std::string_view(PipelineTierName(PipelineTier::FullPerformance)) == "Tier 1 — FULL PERFORMANCE");
    DUWN_ASSERT(std::string_view(PipelineTierName(PipelineTier::Compatibility)) == "Tier 2 — COMPATIBILITY");
    DUWN_ASSERT(std::string_view(PipelineTierName(PipelineTier::Emergency)) == "Tier 3 — EMERGENCY");
}

DUWN_TEST(AdapterVendorNames) {
    using namespace duwn::video;
    DUWN_ASSERT(std::string_view(VendorName(0x10DE)) == "NVIDIA");
    DUWN_ASSERT(std::string_view(VendorName(0x1002)) == "AMD");
    DUWN_ASSERT(std::string_view(VendorName(0x8086)) == "Intel");
    DUWN_ASSERT(std::string_view(VendorName(0x1414)) == "Microsoft");
    DUWN_ASSERT(std::string_view(VendorName(0x5143)) == "Qualcomm");
    DUWN_ASSERT(std::string_view(VendorName(0x9999)) == "Generic/Unknown");
}

DUWN_TEST(D3D11Device_CreateAndProbe) {
    using namespace duwn::video;
    D3D11Device dev;
    bool ok = dev.Create(false, true);
    DUWN_ASSERT(ok);
    DUWN_ASSERT(dev.IsValid());
    DUWN_ASSERT(dev.Device() != nullptr);
    DUWN_ASSERT(dev.Context() != nullptr);
    const auto& ai = dev.GetAdapterInfo();
    DUWN_ASSERT(!ai.description.empty());
    // Either hardware or WARP
    DUWN_ASSERT(ai.is_hardware || ai.is_warp);
}

DUWN_TEST(MFVideoDecoder_ThreeTierFallbackValidation) {
    using namespace duwn::video;

    // --- Tier 1: Hardware MF + D3D11 zero-copy (or auto fallback) ---
    {
        D3D11Device dev;
        bool dev_ok = dev.Create(false, true);
        DUWN_ASSERT(dev_ok);
        const auto& ai = dev.GetAdapterInfo();

        MFVideoDecoder dec(dev, [](VideoFrame){});
        DecoderConfig cfg{
            .width = 1920,
            .height = 1080,
            .preference = DecoderPreference::Auto
        };
        bool dec_ok = dec.Init(cfg);
        DUWN_ASSERT(dec_ok);
        const auto& info = dec.GetDecoderInfo();

        printf("\n--- GPU TIER 1 VALIDATION (Auto / Hardware) ---\n");
        printf("  Adapter: %ls (Vendor: %s, 0x%04X)\n",
               ai.description.c_str(), VendorName(ai.vendor_id), ai.vendor_id);
        printf("  Feature Level: 0x%X, Hardware: %s, WARP: %s\n",
               ai.feature_level, ai.is_hardware ? "yes" : "no", ai.is_warp ? "yes" : "no");
        printf("  MFT Decoder Name: %ls\n", info.name.c_str());
        printf("  Hardware Flag: %s, D3D11-Aware: %s, Zero-Copy: %s\n",
               info.is_hardware ? "true" : "false",
               info.is_d3d11_aware ? "true" : "false",
               info.is_zero_copy ? "true" : "false");
    }

    // --- Tier 2: Software MF + Hardware D3D11 ---
    {
        D3D11Device dev;
        bool dev_ok = dev.Create(false, true);
        DUWN_ASSERT(dev_ok);
        const auto& ai = dev.GetAdapterInfo();

        MFVideoDecoder dec(dev, [](VideoFrame){});
        DecoderConfig cfg{
            .width = 1920,
            .height = 1080,
            .preference = DecoderPreference::SoftwareOnly
        };
        bool dec_ok = dec.Init(cfg);
        DUWN_ASSERT(dec_ok);
        const auto& info = dec.GetDecoderInfo();

        printf("\n--- GPU TIER 2 VALIDATION (Software MF + Hardware D3D11) ---\n");
        printf("  Adapter: %ls (Vendor: %s)\n", ai.description.c_str(), VendorName(ai.vendor_id));
        printf("  MFT Decoder Name: %ls\n", info.name.c_str());
        printf("  Hardware Flag: %s (Expected: false)\n", info.is_hardware ? "true" : "false");
        DUWN_ASSERT(!info.is_hardware);
    }

    // --- Tier 3: Software MF + WARP Rasterizer ---
    {
        D3D11Device dev;
        bool dev_ok = dev.Create(true, false); // Force WARP
        DUWN_ASSERT(dev_ok);
        const auto& ai = dev.GetAdapterInfo();
        DUWN_ASSERT(ai.is_warp);

        MFVideoDecoder dec(dev, [](VideoFrame){});
        DecoderConfig cfg{
            .width = 1920,
            .height = 1080,
            .preference = DecoderPreference::SoftwareOnly
        };
        bool dec_ok = dec.Init(cfg);
        DUWN_ASSERT(dec_ok);
        const auto& info = dec.GetDecoderInfo();

        printf("\n--- GPU TIER 3 VALIDATION (Software MF + Microsoft WARP) ---\n");
        printf("  Adapter: %ls (Vendor: %s)\n", ai.description.c_str(), VendorName(ai.vendor_id));
        printf("  MFT Decoder Name: %ls\n", info.name.c_str());
        printf("  WARP Adapter: %s, Hardware Flag: %s\n",
               ai.is_warp ? "true" : "false", info.is_hardware ? "true" : "false");
        DUWN_ASSERT(ai.is_warp);
        DUWN_ASSERT(!info.is_hardware);
    }
}

DUWN_TEST(MFVideoDecoder_HEVCCapabilityProbe) {
    using namespace duwn::video;
    D3D11Device dev;
    DUWN_ASSERT(dev.Create(false, true));

    MFVideoDecoder dec(dev, [](VideoFrame){});
    DecoderConfig cfg{
        .width = 1920,
        .height = 1080,
        .codec = VideoCodecType::H265,
        .preference = DecoderPreference::Auto
    };
    if (!dec.Init(cfg)) {
        printf("HEVC MF decoder: unavailable on this host\n");
        return;
    }
    const auto& info = dec.GetDecoderInfo();
    printf("HEVC MF decoder: %ls, hardware=%s, D3D11-aware=%s, output=%s\n",
           info.name.c_str(), info.is_hardware ? "yes" : "no",
           info.is_d3d11_aware ? "yes" : "no",
           info.output_format == DXGI_FORMAT_P010 ? "P010" : "NV12");
    DUWN_ASSERT(info.output_format == DXGI_FORMAT_NV12 ||
                info.output_format == DXGI_FORMAT_P010);
}

DUWN_TEST(D3D11VideoDevice_HEVCProfileAudit) {
    using namespace duwn::video;
    D3D11Device dev;
    DUWN_ASSERT(dev.Create(false, true));

    Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_dev;
    HRESULT hr = dev.Device()->QueryInterface(IID_PPV_ARGS(&video_dev));
    DUWN_ASSERT(SUCCEEDED(hr));
    DUWN_ASSERT(video_dev != nullptr);

    UINT profile_count = video_dev->GetVideoDecoderProfileCount();
    printf("\n--- D3D11 VIDEO DEVICE HEVC PROFILE AUDIT ---\n");
    printf("  Total Video Decoder Profiles: %u\n", profile_count);

    bool hevc_main_supported = false;
    bool hevc_main10_supported = false;
    bool nv12_supported = false;
    bool p010_supported = false;

    for (UINT i = 0; i < profile_count; ++i) {
        GUID profile{};
        if (SUCCEEDED(video_dev->GetVideoDecoderProfile(i, &profile))) {
            if (profile == D3D11_DECODER_PROFILE_HEVC_VLD_MAIN) {
                hevc_main_supported = true;
                BOOL sup_nv12 = FALSE;
                if (SUCCEEDED(video_dev->CheckVideoDecoderFormat(&profile, DXGI_FORMAT_NV12, &sup_nv12)) && sup_nv12) {
                    nv12_supported = true;
                }
            } else if (profile == D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10) {
                hevc_main10_supported = true;
                BOOL sup_p010 = FALSE;
                if (SUCCEEDED(video_dev->CheckVideoDecoderFormat(&profile, DXGI_FORMAT_P010, &sup_p010)) && sup_p010) {
                    p010_supported = true;
                }
            }
        }
    }

    printf("  HEVC Main hardware video decode profile: %s\n", hevc_main_supported ? "YES" : "NO");
    printf("  HEVC Main10 hardware video decode profile: %s\n", hevc_main10_supported ? "YES" : "NO");
    printf("  NV12 decode target supported: %s\n", nv12_supported ? "YES" : "NO");
    printf("  P010 decode target supported: %s\n", p010_supported ? "YES" : "NO");
}


