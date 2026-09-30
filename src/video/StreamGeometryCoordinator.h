#pragma once

#include "VideoGeometry.h"
#include "VideoFrame.h"
#include "app/Settings.h"
#include <mutex>
#include <cstdint>

namespace duwn::video {

struct GeometrySnapshot {
    uint32_t          coded_width{0};
    uint32_t          coded_height{0};
    AlignedAperture   visible_aperture{};
    StreamOrientation orientation{StreamOrientation::Unknown};
    uint64_t          geometry_generation{0};
    OutputDimensions  resolved_output{};
    PreviewDimensions recommended_preview{};
    bool              is_valid{false};
};

class StreamGeometryCoordinator {
public:
    StreamGeometryCoordinator() noexcept = default;
    ~StreamGeometryCoordinator() = default;

    StreamGeometryCoordinator(const StreamGeometryCoordinator&) = delete;
    StreamGeometryCoordinator& operator=(const StreamGeometryCoordinator&) = delete;

    // Ingest newly arrived decoded frame geometry.
    // Returns true if geometry_generation was incremented (layout or aperture changed).
    bool UpdateFromFrame(
        const VideoFrame& frame,
        app::CaptureCanvas canvas_mode,
        app::OutputQuality output_quality,
        uint32_t custom_out_w,
        uint32_t custom_out_h,
        uint32_t work_area_w,
        uint32_t work_area_h) noexcept;

    // Snapshot query (lock-protected copy)
    GeometrySnapshot GetSnapshot() const noexcept;

    // Fast queries
    uint64_t CurrentGeneration() const noexcept;
    StreamOrientation CurrentOrientation() const noexcept;
    OutputDimensions CurrentOutputDimensions() const noexcept;

    void Reset() noexcept;

private:
    mutable std::mutex  m_mutex;
    GeometrySnapshot    m_snapshot{};
    uint64_t            m_generation{0};

    // Debounce filter for transient decoder aperture jitter
    uint32_t            m_pending_width{0};
    uint32_t            m_pending_height{0};
    uint32_t            m_pending_count{0};
};

} // namespace duwn::video
