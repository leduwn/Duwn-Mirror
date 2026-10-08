#pragma once
// Metrics — lock-free atomic counters for hot-path telemetry.
// All values readable from any thread without locking.
// MetricsThread snapshots these at 1 Hz for diagnostics display.

#include <atomic>
#include <cstdint>

namespace duwn {

// All fields are relaxed atomics — updated on hot paths with no ordering
// guarantee between fields. MetricsThread reads a snapshot periodically;
// slight inconsistency between fields is acceptable for display purposes.
struct alignas(64) Metrics {
    // Network & Video RTP
    std::atomic<uint64_t> network_raw_udp_packets{0};
    std::atomic<uint64_t> network_received_packets{0};
    std::atomic<uint64_t> network_lost_packets{0};
    std::atomic<uint64_t> network_reordered_packets{0};
    std::atomic<uint64_t> network_malformed_packets{0};
    std::atomic<double>   network_jitter_ms{0.0};

    std::atomic<uint64_t> video_rtp_packets{0};
    std::atomic<uint64_t> video_rtp_frame_boundaries{0}; // RTP marker: end of encoded frame, not packet count
    std::atomic<uint64_t> video_rtp_bytes{0};
    std::atomic<uint64_t> video_dropped_min_ready{0};
    std::atomic<uint64_t> video_dropped_ipc_active{0};
    std::atomic<uint64_t> video_dropped_decoder_not_ready{0};
    std::atomic<uint64_t> video_scheduler_rejected_frames{0};
    std::atomic<uint64_t> video_access_units{0};
    std::atomic<uint64_t> video_access_units_submitted{0};

    // Video Decoding & Rendering
    std::atomic<uint64_t> decoder_process_input_calls{0};
    std::atomic<uint64_t> decoder_process_input_success{0};
    std::atomic<uint64_t> decoder_process_input_failed{0};
    std::atomic<uint64_t> decoder_process_output_calls{0};
    std::atomic<uint64_t> decoder_process_output_success{0};
    std::atomic<uint64_t> decoder_need_more_input{0};
    std::atomic<uint64_t> decoder_stream_change{0};
    std::atomic<uint64_t> decoder_output_failed{0};

    std::atomic<uint64_t> video_decoded_frames{0};
    std::atomic<int>      video_decoder_kind{0}; // 0 unknown, 1 hardware, 2 software
    std::atomic<bool>     video_decoder_zero_copy{false};
    std::atomic<uint64_t> video_rendered_frames{0};
    std::atomic<uint64_t> video_dropped_frames{0};
    std::atomic<uint64_t> video_late_frames{0};
    std::atomic<uint64_t> video_format_transition_drops{0};
    std::atomic<uint64_t> video_queue_full_drops{0};
    std::atomic<uint64_t> video_stale_generation_drops{0};
    std::atomic<uint64_t> video_latency_catchup_drops{0}; // Oldest stale frame dropped when 3-frame queue full
    std::atomic<uint64_t> video_stale_age_drops{0};       // Stale age drops from latency control

    // Separated Drop & Discard Counters
    std::atomic<uint64_t> video_decoded_superseded{0};
    std::atomic<uint64_t> video_superseded_before_present{0};
    std::atomic<uint64_t> mailbox_replacements{0};
    std::atomic<uint64_t> video_presentation_late_drops{0};
    std::atomic<uint64_t> video_queue_overflow_drops{0};
    std::atomic<uint64_t> video_decoder_gap_drops{0};
    std::atomic<uint64_t> preview_skips{0};

    // Video Presentation Call Classification (Primary Output vs Preview)
    std::atomic<uint64_t> video_present_attempts{0};
    std::atomic<uint64_t> video_present_ok{0};
    std::atomic<uint64_t> video_present_skipped{0};
    std::atomic<uint64_t> video_present_errors{0};

    std::atomic<uint64_t> preview_present_attempts{0};
    std::atomic<uint64_t> preview_present_ok{0};
    std::atomic<uint64_t> preview_present_skipped{0};
    std::atomic<uint64_t> preview_present_errors{0};
    std::atomic<double>   preview_vp_duration_avg_ms{0.0};
    std::atomic<double>   preview_present_duration_avg_ms{0.0};

    // HEVC RFC 7798 Depacketizer Counters (Phase 15E)
    std::atomic<uint64_t> hevc_fu_started{0};
    std::atomic<uint64_t> hevc_fu_completed{0};
    std::atomic<uint64_t> hevc_fu_aborted{0};
    std::atomic<uint64_t> hevc_ap_packets{0};
    std::atomic<uint64_t> hevc_single_nals{0};
    std::atomic<uint64_t> hevc_parameter_sets{0};

    // Decode Output Gap Buckets & Burst Metrics (Phase 11)
    std::atomic<uint64_t> decode_gap_lt_1ms{0};
    std::atomic<uint64_t> decode_gap_1_3ms{0};
    std::atomic<uint64_t> decode_gap_3_8ms{0};
    std::atomic<uint64_t> decode_gap_8_14ms{0};
    std::atomic<uint64_t> decode_gap_14_20ms{0};
    std::atomic<uint64_t> decode_gap_gt_20ms{0};
    std::atomic<uint64_t> burst_2_frames{0};
    std::atomic<uint64_t> burst_3_frames{0};
    std::atomic<uint64_t> burst_max{0};
    std::atomic<double>   decode_output_gap_p50{0.0};
    std::atomic<double>   decode_output_gap_p95{0.0};
    std::atomic<double>   last_decode_output_gap_ms{0.0};

    // DXGI Presentation Waitable Clock Signals & Intervals (Phase 10)
    std::atomic<uint64_t> dxgi_ready_signals{0};
    std::atomic<double>   dxgi_ready_interval_avg_ms{0.0};
    std::atomic<double>   dxgi_ready_interval_p50_ms{0.0};
    std::atomic<double>   dxgi_ready_interval_p95_ms{0.0};
    std::atomic<double>   dxgi_ready_interval_max_ms{0.0};

    // End-to-End Frame Lifecycle Durations (Phase 12)
    std::atomic<double>   queue_residence_avg_ms{0.0};
    std::atomic<double>   queue_residence_p50_ms{0.0};
    std::atomic<double>   queue_residence_p95_ms{0.0};
    std::atomic<uint64_t> queue_residence_sample_count{0};
    std::atomic<double>   vp_duration_avg_ms{0.0};
    std::atomic<double>   vp_duration_p50_ms{0.0};
    std::atomic<double>   vp_duration_p95_ms{0.0};
    std::atomic<uint64_t> vp_sample_count{0};
    std::atomic<double>   present_duration_avg_ms{0.0};
    std::atomic<double>   present_duration_p50_ms{0.0};
    std::atomic<double>   present_duration_p95_ms{0.0};
    std::atomic<uint64_t> present_sample_count{0};
    std::atomic<double>   total_pipeline_avg_ms{0.0};
    std::atomic<double>   total_pipeline_p95_ms{0.0};

    // Queue Depth Percentiles
    std::atomic<double>   queue_depth_p50{0.0};
    std::atomic<double>   queue_depth_p95{0.0};
    std::atomic<uint32_t> queue_depth_max{0};

    // Accounting invariant delta: decoded - (presented + explicit_drops + queued)
    std::atomic<int64_t>  accounting_delta{0};

    // Session-local counters (reset on disconnect/generation change)
    std::atomic<uint64_t> session_q_full{0};
    std::atomic<uint64_t> session_drops{0};

    // Presentation Clock & Tick Metrics
    std::atomic<uint64_t> presentation_ticks{0};
    std::atomic<uint64_t> presentation_unique_frames{0};
    std::atomic<uint64_t> presentation_repeated_ticks{0};
    std::atomic<uint64_t> display_opportunities{0};
    std::atomic<double>   display_wait_ms{0.0};
    std::atomic<double>   dxgi_wait_avg_ms{0.0};
    std::atomic<double>   dxgi_wait_p50_ms{0.0};
    std::atomic<double>   dxgi_wait_p95_ms{0.0};
    std::atomic<double>   dxgi_wait_max_ms{0.0};
    std::atomic<uint64_t> dxgi_wait_sample_count{0};

    // Source Cadence Classification
    std::atomic<double>   source_nominal_fps{30.0};
    std::atomic<double>   source_jitter_p50_ms{0.0};
    std::atomic<double>   source_jitter_p95_ms{0.0};
    std::atomic<uint64_t> source_outliers{0};

    std::atomic<double>   video_decode_time_ms{0.0};  // rolling avg / 1s avg
    std::atomic<double>   video_decode_p50_ms{0.0};
    std::atomic<double>   video_decode_p95_ms{0.0};
    std::atomic<uint64_t> video_decode_sample_count{0};
    std::atomic<double>   video_render_time_ms{0.0};  // rolling avg
    std::atomic<int32_t>  video_queue_depth{0};
    std::atomic<uint64_t> video_format_generation{0};

    // Granular Source, Scheduler, and Present Pacing Metrics
    std::atomic<double>   video_pts_delta_avg_ms{0.0};
    std::atomic<double>   video_pts_delta_p50_ms{0.0};
    std::atomic<double>   video_pts_delta_p95_ms{0.0};

    std::atomic<double>   video_wake_error_avg_ms{0.0};
    std::atomic<double>   video_wake_error_p95_ms{0.0};

    std::atomic<double>   video_schedule_lateness_avg_ms{0.0};
    std::atomic<double>   video_decode_to_present_avg_ms{0.0};
    std::atomic<double>   video_queue_age_avg_ms{0.0};

    std::atomic<double>   video_present_call_avg_ms{0.0};
    std::atomic<double>   video_present_call_p50_ms{0.0};
    std::atomic<double>   video_present_call_p95_ms{0.0};

    std::atomic<double>   video_present_interval_avg_ms{0.0};
    std::atomic<double>   video_present_interval_p95_ms{0.0};

    // Stutter intervals & Frame drop classification
    std::atomic<uint64_t> stutter_intervals_gt_25ms{0};
    std::atomic<uint64_t> stutter_intervals_gt_33ms{0};
    std::atomic<uint64_t> stutter_intervals_gt_50ms{0};
    std::atomic<uint64_t> duwn_dropped_frames{0};

    // Video Resolutions
    std::atomic<uint32_t> video_coded_width{0};
    std::atomic<uint32_t> video_coded_height{0};
    std::atomic<uint32_t> video_visible_width{0};
    std::atomic<uint32_t> video_visible_height{0};

    // Audio
    std::atomic<uint64_t> audio_rtp_packets{0};
    std::atomic<uint64_t> audio_rtp_bytes{0};
    std::atomic<uint64_t> audio_sequence_gaps{0};
    std::atomic<uint64_t> audio_out_of_order_packets{0};
    std::atomic<uint64_t> audio_duplicate_packets{0};
    std::atomic<uint64_t> audio_malformed_packets{0};
    std::atomic<uint32_t> audio_payload_type{96};
    std::atomic<uint32_t> audio_input_rate{44100};
    std::atomic<uint32_t> audio_output_rate{48000};
    std::atomic<uint32_t> audio_channels{2};
    std::atomic<uint64_t> audio_underruns{0};
    std::atomic<uint64_t> audio_real_underruns{0};
    std::atomic<uint64_t> audio_silence_fill_frames{0};
    std::atomic<uint64_t> audio_network_gap_frames{0};
    std::atomic<uint64_t> audio_resume_events{0};
    std::atomic<double>   audio_silence_duration_ms{0.0};
    std::atomic<double>   resume_first_packet_to_submit_ms{0.0};
    std::atomic<bool>     wasapi_running{false};
    std::atomic<int64_t>  last_audio_rtp_arrival_ns{0};
    std::atomic<uint64_t> audio_samples_rendered{0};
    std::atomic<double>   audio_buffer_ms{0.0};
    std::atomic<double>   audio_packet_duration_ms{0.0};
    std::atomic<double>   audio_arrival_gap_ms{0.0};
    std::atomic<double>   audio_arrival_gap_max_ms{0.0};
    std::atomic<double>   audio_a0_a1_ms{0.0};
    std::atomic<double>   audio_a1_a2_ms{0.0};
    std::atomic<double>   audio_a2_a3_ms{0.0};
    std::atomic<double>   audio_a3_a4_ms{0.0};
    std::atomic<double>   audio_a5_a6_ms{0.0};
    std::atomic<double>   audio_wasapi_padding_ms{0.0};
    std::atomic<double>   audio_engine_period_ms{0.0};
    std::atomic<double>   audio_stream_latency_ms{0.0};
    std::atomic<uint64_t> audio_ring_overrun_frames{0};
    std::atomic<uint64_t> audio_underrun_frames{0};
    std::atomic<uint64_t> audio_backlog_recovery_drops{0};
    std::atomic<double>   audio_target_buffer_ms{0.0};
    std::atomic<double>   audio_servo_correction_ppm{0.0};
    std::atomic<double>   audio_resampler_group_delay_ms{0.0};
    std::atomic<uint64_t> audio_discontinuity_recoveries{0};

    // Sync
    std::atomic<double>   av_offset_ms{0.0};
    std::atomic<double>   drift_ms_per_min{0.0};

    // Session
    std::atomic<bool>     sidecar_alive{false};
    std::atomic<uint32_t> output_window_width{0};
    std::atomic<uint32_t> output_window_height{0};

    // Client Reported Performance (from UxPlay -FPSdata)
    std::atomic<double>   client_fps{0.0};
    std::atomic<uint64_t> client_dropped_frames{0};
    std::atomic<uint64_t> client_total_frames{0};
    std::atomic<double>   client_bitrate_kbps{0.0};

    // Direct IPC Transport Metrics
    std::atomic<uint64_t> ipc_frames_written{0};
    std::atomic<uint64_t> ipc_frames_consumed{0};
    std::atomic<uint64_t> ipc_producer_dropped{0};
    std::atomic<double>   ipc_transport_latency_ms{0.0}; // Producer write to consumer read
};

// Singleton — one global metrics object.
// Access via Metrics::Global().
Metrics& GlobalMetrics() noexcept;

} // namespace duwn
