#pragma once
// DirectLatencyTracker.h — Granular 10-checkpoint pipeline latency measurement for Duwn Direct.
// Tracks:
// S0: Capture/source timestamp (PTS)
// S1: Capture callback entry
// S2: Encoder input submission
// S3: Encoder output emission
// S4: Packet send time
// R0: First packet arrival
// R1: Access Unit assembly complete
// R2: Decoder input submission
// R3: Decoder output (D3D11 zero-copy surface)
// R4: Present time
//
// Explicit Rule: Segmented pipeline latency ONLY. Do NOT call this physical glass-to-glass latency.

#include <cstdint>
#include <vector>
#include <mutex>
#include <algorithm>
#include <string>

namespace duwn::direct {

struct FrameLatencyCheckpoints {
    uint64_t frame_id{0};

    // Sender timestamps (nanoseconds)
    uint64_t s0_capture_source_ns{0};
    uint64_t s1_capture_callback_ns{0};
    uint64_t s2_encoder_input_ns{0};
    uint64_t s3_encoder_output_ns{0};
    uint64_t s4_packet_send_ns{0};

    // Receiver timestamps (nanoseconds)
    uint64_t r0_packet_arrival_ns{0};
    uint64_t r1_au_complete_ns{0};
    uint64_t r2_decoder_input_ns{0};
    uint64_t r3_decoder_output_ns{0};
    uint64_t r4_present_ns{0};

    // Stage latencies (milliseconds)
    double EncodeMs() const noexcept {
        return (s3_encoder_output_ns > s2_encoder_input_ns)
            ? (s3_encoder_output_ns - s2_encoder_input_ns) / 1'000'000.0 : 0.0;
    }

    double AssemblyMs() const noexcept {
        return (r1_au_complete_ns > r0_packet_arrival_ns)
            ? (r1_au_complete_ns - r0_packet_arrival_ns) / 1'000'000.0 : 0.0;
    }

    double DecodeMs() const noexcept {
        return (r3_decoder_output_ns > r2_decoder_input_ns)
            ? (r3_decoder_output_ns - r2_decoder_input_ns) / 1'000'000.0 : 0.0;
    }

    double RenderMs() const noexcept {
        return (r4_present_ns > r3_decoder_output_ns)
            ? (r4_present_ns - r3_decoder_output_ns) / 1'000'000.0 : 0.0;
    }

    // Capture-callback to Present delivery latency (when clocks are synchronized or on loopback test)
    double CallbackToPresentMs() const noexcept {
        return (r4_present_ns > s1_capture_callback_ns)
            ? (r4_present_ns - s1_capture_callback_ns) / 1'000'000.0 : 0.0;
    }
};

struct PipelineLatencySummary {
    double avg_encode_ms{0.0};
    double p95_encode_ms{0.0};

    double avg_assembly_ms{0.0};
    double p95_assembly_ms{0.0};

    double avg_decode_ms{0.0};
    double p95_decode_ms{0.0};

    double avg_render_ms{0.0};
    double p95_render_ms{0.0};

    double avg_callback_to_present_ms{0.0};
    double p95_callback_to_present_ms{0.0};
};

class DirectLatencyTracker {
public:
    DirectLatencyTracker() = default;

    void RecordFrame(FrameLatencyCheckpoints checkpoints) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_samples.push_back(checkpoints);
        if (m_samples.size() > 1000) {
            m_samples.erase(m_samples.begin());
        }
    }

    PipelineLatencySummary GetSummary() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_samples.empty()) return {};

        std::vector<double> encode_list;
        std::vector<double> assembly_list;
        std::vector<double> decode_list;
        std::vector<double> render_list;
        std::vector<double> e2e_list;

        double sum_enc = 0, sum_asm = 0, sum_dec = 0, sum_rnd = 0, sum_e2e = 0;

        for (const auto& s : m_samples) {
            double enc = s.EncodeMs();
            double asm_m = s.AssemblyMs();
            double dec = s.DecodeMs();
            double rnd = s.RenderMs();
            double e2e = s.CallbackToPresentMs();

            encode_list.push_back(enc);
            assembly_list.push_back(asm_m);
            decode_list.push_back(dec);
            render_list.push_back(rnd);
            if (e2e > 0.0) e2e_list.push_back(e2e);

            sum_enc += enc;
            sum_asm += asm_m;
            sum_dec += dec;
            sum_rnd += rnd;
            sum_e2e += e2e;
        }

        auto p95 = [](std::vector<double>& v) -> double {
            if (v.empty()) return 0.0;
            std::sort(v.begin(), v.end());
            return v[v.size() * 95 / 100];
        };

        PipelineLatencySummary summary;
        size_t n = m_samples.size();
        summary.avg_encode_ms = sum_enc / n;
        summary.p95_encode_ms = p95(encode_list);

        summary.avg_assembly_ms = sum_asm / n;
        summary.p95_assembly_ms = p95(assembly_list);

        summary.avg_decode_ms = sum_dec / n;
        summary.p95_decode_ms = p95(decode_list);

        summary.avg_render_ms = sum_rnd / n;
        summary.p95_render_ms = p95(render_list);

        if (!e2e_list.empty()) {
            summary.avg_callback_to_present_ms = sum_e2e / e2e_list.size();
            summary.p95_callback_to_present_ms = p95(e2e_list);
        }

        return summary;
    }

    void Clear() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_samples.clear();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<FrameLatencyCheckpoints> m_samples;
};

} // namespace duwn::direct
