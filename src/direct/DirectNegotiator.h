#pragma once
// DirectNegotiator.h — Capability-driven negotiation engine for Duwn Direct Mode.
// Pure capability intersection; strictly NO device-model or OS version branching.

#include "DirectSessionModel.h"

namespace duwn::direct {

class DirectNegotiator {
public:
    // Derives a SessionPlan from the intersection of sender and receiver capabilities.
    // Preserves quality policy, enforces protocol versioning rules, and keeps
    // source, encoded, and render resolutions strictly separate.
    static SessionPlan Negotiate(
        const DirectProtocolVersion& sender_protocol_version,
        const CaptureCapabilities& sender_capture,
        const EncoderCapabilities& sender_encoder,
        const TransportCapabilities& sender_transport,
        const ReceiverCapabilities& receiver_caps,
        const SessionPreferences& prefs,
        const RuntimeNetworkMetrics& net_metrics = {}) noexcept;

    // Helper to compute codec intersection.
    static std::vector<DirectVideoCodec> IntersectCodecs(
        const std::vector<DirectVideoCodec>& sender_codecs,
        const std::vector<DirectVideoCodec>& receiver_codecs) noexcept;

    // Helper to compute transport intersection.
    static std::vector<DirectTransportType> IntersectTransports(
        const std::vector<DirectTransportType>& sender_transports,
        const std::vector<DirectTransportType>& receiver_transports) noexcept;
};

} // namespace duwn::direct
