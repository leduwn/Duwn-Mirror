#include "SourceQualityTracker.h"
#include "common/logging/Logger.h"
#include <windows.h>

namespace duwn::video {

static std::string WideToUtf8(std::wstring_view w) noexcept {
    if (w.empty()) return {};
    int size = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string s(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), size, nullptr, nullptr);
    return s;
}

void SourceQualityTracker::OnFrame(uint32_t coded_w, uint32_t coded_h,
                                   uint32_t vis_w, uint32_t vis_h,
                                   double fps, double bitrate_mbps,
                                   const std::string& codec) noexcept {
    m_current_aperture.coded_width = coded_w;
    m_current_aperture.coded_height = coded_h;
    m_current_aperture.visible_width = vis_w;
    m_current_aperture.visible_height = vis_h;
    m_current_aperture.fps = fps;
    m_current_aperture.bitrate_mbps = bitrate_mbps;
    m_current_aperture.codec = codec;

    if (!m_current_aperture.IsValid()) {
        return;
    }

    // Check if current frame matches candidate aperture
    if (vis_w == m_candidate_aperture.visible_width &&
        vis_h == m_candidate_aperture.visible_height &&
        coded_w == m_candidate_aperture.coded_width &&
        coded_h == m_candidate_aperture.coded_height) {
        ++m_stable_frame_count;
        if (m_stable_frame_count >= m_min_stable_frames) {
            bool was_stable = m_is_stable;
            bool dims_changed = (m_stable_aperture.visible_width != vis_w ||
                                 m_stable_aperture.visible_height != vis_h);

            m_is_stable = true;
            m_stable_aperture = m_current_aperture;

            // Update session observations
            uint32_t long_edge = m_stable_aperture.LongEdge();
            if (m_stable_aperture.IsLandscape()) {
                m_observation.max_observed_landscape_long_edge =
                    std::max(m_observation.max_observed_landscape_long_edge, long_edge);
            } else {
                m_observation.max_observed_portrait_long_edge =
                    std::max(m_observation.max_observed_portrait_long_edge, long_edge);
            }
            m_observation.max_observed_long_edge =
                std::max(m_observation.max_observed_long_edge, long_edge);

            if (m_observation.max_observed_long_edge > 1920) {
                m_observation.observed_gain_over_fhd =
                    m_observation.max_observed_long_edge - 1920;
            } else {
                m_observation.observed_gain_over_fhd = 0;
            }

            Reevaluate();

            // Emit telemetry log when quality first stabilizes or dimensions change
            if (!was_stable || dims_changed) {
                DUWN_LOG_INFO("SourceQuality", FormatTelemetryBlock());
            }
        }
    } else {
        // Transient change detected — reset stability counter but retain current stable classification
        m_candidate_aperture = m_current_aperture;
        m_stable_frame_count = 1;
    }
}

void SourceQualityTracker::SetRequestedEnvelope(const RequestedReceiverEnvelope& env) noexcept {
    m_requested = env;
    Reevaluate();
}

void SourceQualityTracker::SetClientInfo(const std::wstring& model,
                                         const std::wstring& marketing,
                                         const std::wstring& ua,
                                         const std::wstring& transport) noexcept {
    if (!model.empty() && model != L"—") m_observation.device_model = model;
    if (!marketing.empty() && marketing != L"—") m_observation.marketing_name = marketing;
    if (!ua.empty() && ua != L"—") m_observation.user_agent = ua;
    if (!transport.empty() && transport != L"—") m_observation.transport = transport;
    Reevaluate();
}

void SourceQualityTracker::ResetSession() noexcept {
    m_stable_frame_count = 0;
    m_is_stable = false;
    m_current_aperture = {};
    m_candidate_aperture = {};
    m_stable_aperture = {};
    m_observation = {};
    m_effectiveness = QualityEffectiveness::Unknown;
}

QualityEffectiveness SourceQualityTracker::Classify(
    const RequestedReceiverEnvelope& req,
    const ActualSourceAperture& actual,
    const DeviceSessionObservation& obs,
    bool is_stable) noexcept {

    if (!is_stable || !actual.IsValid() || req.LongEdge() == 0) {
        return QualityEffectiveness::Unknown;
    }

    // Original Mode: Direct 1:1 passthrough without intentional downscaling
    if (req.is_original) {
        return QualityEffectiveness::DeliveredAsRequested;
    }

    const uint32_t req_long = req.LongEdge();
    const uint32_t actual_long = actual.LongEdge();

    // In portrait mode (e.g. 500x1080), iPhone displays have vertical height = 1080 (1080p class).
    // If the device has demonstrated 1920-class delivery in landscape during this session,
    // or is delivering standard 1080p portrait height (>= 1080 - 16), recognize device capability.
    uint32_t effective_capability_long = std::max(actual_long, obs.max_observed_long_edge);
    if (!actual.IsLandscape() && actual.visible_height >= 1080 - 16) {
        effective_capability_long = std::max(effective_capability_long, 1920u);
    }

    if (req_long <= 1280) {
        // HD 720p: tolerance of 16 pixels
        if (effective_capability_long >= 1280 - 16 || (!actual.IsLandscape() && actual.visible_height >= 720 - 16)) {
            return QualityEffectiveness::DeliveredAsRequested;
        }
        return QualityEffectiveness::SourceLimited;
    }

    if (req_long <= 1920) {
        // Full HD 1080p: tolerance of 16 pixels
        if (effective_capability_long >= 1920 - 16) {
            return QualityEffectiveness::DeliveredAsRequested;
        }
        return QualityEffectiveness::SourceLimited;
    }

    // High resolution requests: 2K (2560) or 4K (3840)
    if (effective_capability_long >= req_long - 32) {
        return QualityEffectiveness::DeliveredAsRequested;
    }

    // If source delivers above 1920 but below requested long edge (e.g. 2560 delivered for 4K request)
    if (effective_capability_long > 1920) {
        return QualityEffectiveness::PartiallyDelivered;
    }

    // Source capped at <= 1920 (e.g. iPhone XS delivering 1920 for 2K or 4K request)
    return QualityEffectiveness::SourceLimited;
}

void SourceQualityTracker::Reevaluate() noexcept {
    m_effectiveness = Classify(m_requested, m_stable_aperture, m_observation, m_is_stable);
}

std::string SourceQualityTracker::FormatTelemetryBlock() const {
    const auto& actual = m_stable_aperture.IsValid() ? m_stable_aperture : m_current_aperture;
    std::string model = WideToUtf8(m_observation.device_model);
    std::string ua = WideToUtf8(m_observation.user_agent);
    std::string trans = WideToUtf8(m_observation.transport);

    return std::format(
        "\n[SOURCE QUALITY]\n"
        "preset={}\n"
        "requested_envelope={}x{}\n"
        "coded={}x{}\n"
        "visible={}x{}\n"
        "long_edge={}\n"
        "fps={:.0f}\n"
        "bitrate={:.1f}\n"
        "codec={}\n"
        "transport={}\n"
        "device_model={}\n"
        "user_agent={}\n"
        "quality_state={}",
        m_requested.preset_name.empty() ? "Auto" : m_requested.preset_name,
        m_requested.width, m_requested.height,
        actual.coded_width, actual.coded_height,
        actual.visible_width, actual.visible_height,
        actual.LongEdge(),
        actual.fps > 0.0 ? actual.fps : 60.0,
        actual.bitrate_mbps,
        actual.codec.empty() ? "H264" : actual.codec,
        trans.empty() ? "Wireless" : trans,
        model.empty() ? "—" : model,
        ua.empty() ? "—" : ua,
        QualityEffectivenessToString(m_effectiveness)
    );
}

} // namespace duwn::video
