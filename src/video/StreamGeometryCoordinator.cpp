#include "StreamGeometryCoordinator.h"
#include "common/logging/Logger.h"
#include <algorithm>

namespace duwn::video {

bool StreamGeometryCoordinator::UpdateFromFrame(
    const VideoFrame& frame,
    app::CaptureCanvas canvas_mode,
    app::OutputQuality output_quality,
    uint32_t custom_out_w,
    uint32_t custom_out_h,
    uint32_t work_area_w,
    uint32_t work_area_h) noexcept
{
    if (frame.width == 0 || frame.height == 0) return false;

    const uint32_t raw_vw = frame.visible_width > 0 ? frame.visible_width : frame.width;
    const uint32_t raw_vh = frame.visible_height > 0 ? frame.visible_height : frame.height;
    const int32_t  raw_vx = static_cast<int32_t>(frame.visible_x);
    const int32_t  raw_vy = static_cast<int32_t>(frame.visible_y);

    AlignedAperture aligned = AlignApertureInward(raw_vx, raw_vy, raw_vw, raw_vh, frame.width, frame.height);
    StreamOrientation orient = DeriveStreamOrientation(aligned.width, aligned.height);

    // Orientation match check against coded texture
    if (!IsApertureOrientationMatching(aligned.width, aligned.height, frame.width, frame.height)) {
        return false;
    }

    std::lock_guard lock{m_mutex};

    const bool dims_changed = (!m_snapshot.is_valid ||
                               aligned.width != m_snapshot.visible_aperture.width ||
                               aligned.height != m_snapshot.visible_aperture.height ||
                               frame.width != m_snapshot.coded_width ||
                               frame.height != m_snapshot.coded_height);

    if (!dims_changed) {
        m_pending_count = 0;
        return false;
    }

    // 2-frame debouncing filter or format generation bump
    const bool is_format_bump = (frame.format_generation > m_snapshot.geometry_generation);
    if (!is_format_bump && m_snapshot.is_valid) {
        if (aligned.width == m_pending_width && aligned.height == m_pending_height) {
            ++m_pending_count;
        } else {
            m_pending_width = aligned.width;
            m_pending_height = aligned.height;
            m_pending_count = 1;
        }
        if (m_pending_count < 2) {
            return false; // Wait for second coherent frame
        }
    }

    m_pending_count = 0;
    ++m_generation;

    // Resolve Output Dimensions
    OutputDimensions out_dims{};
    if (canvas_mode == app::CaptureCanvas::FollowSource) {
        uint32_t long_edge = app::GetOutputQualityLongEdge(output_quality);
        if (output_quality == app::OutputQuality::Custom) {
            out_dims = {custom_out_w, custom_out_h};
        } else {
            out_dims = ComputeAspectAwareOutputDimensions(aligned.width, aligned.height, long_edge);
        }
    } else {
        switch (canvas_mode) {
        case app::CaptureCanvas::Canvas_16_9_HD:
            out_dims = {1280, 720}; break;
        case app::CaptureCanvas::Canvas_16_9_FullHD:
            out_dims = {1920, 1080}; break;
        case app::CaptureCanvas::Canvas_16_9_2K:
            out_dims = {2560, 1440}; break;
        default:
            out_dims = {custom_out_w, custom_out_h}; break;
        }
    }

    PreviewDimensions prev_dims = ComputeComfortablePreviewDimensions(
        aligned.width, aligned.height, work_area_w, work_area_h);

    m_snapshot.coded_width = frame.width;
    m_snapshot.coded_height = frame.height;
    m_snapshot.visible_aperture = aligned;
    m_snapshot.orientation = orient;
    m_snapshot.geometry_generation = m_generation;
    m_snapshot.resolved_output = out_dims;
    m_snapshot.recommended_preview = prev_dims;
    m_snapshot.is_valid = true;

    return true;
}

GeometrySnapshot StreamGeometryCoordinator::GetSnapshot() const noexcept {
    std::lock_guard lock{m_mutex};
    return m_snapshot;
}

uint64_t StreamGeometryCoordinator::CurrentGeneration() const noexcept {
    std::lock_guard lock{m_mutex};
    return m_generation;
}

StreamOrientation StreamGeometryCoordinator::CurrentOrientation() const noexcept {
    std::lock_guard lock{m_mutex};
    return m_snapshot.orientation;
}

OutputDimensions StreamGeometryCoordinator::CurrentOutputDimensions() const noexcept {
    std::lock_guard lock{m_mutex};
    return m_snapshot.resolved_output;
}

void StreamGeometryCoordinator::Reset() noexcept {
    std::lock_guard lock{m_mutex};
    m_snapshot = {};
    m_pending_width = 0;
    m_pending_height = 0;
    m_pending_count = 0;
    m_generation = 0;
}

} // namespace duwn::video
