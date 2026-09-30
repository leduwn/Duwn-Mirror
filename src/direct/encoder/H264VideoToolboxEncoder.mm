#include "H264VideoToolboxEncoder.h"

#if defined(__APPLE__)
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>
#include <mach/mach_time.h>

static uint64_t GetEncoderMonotonicNs() {
    static mach_timebase_info_data_t timebase;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        mach_timebase_info(&timebase);
    });
    uint64_t time = mach_absolute_time();
    return (time * timebase.numer) / timebase.denom;
}

// C-style VideoToolbox output callback
static void VTCompressionCallback(void* outputCallbackRefCon,
                                  void* sourceFrameRefCon,
                                  OSStatus status,
                                  VTEncodeInfoFlags infoFlags,
                                  CMSampleBufferRef sampleBuffer) {
    auto* encoder = static_cast<duwn::direct::H264VideoToolboxEncoder*>(outputCallbackRefCon);
    if (!encoder) return;

    // sourceFrameRefCon holds a pointer to frame metadata
    struct FrameMetadata {
        uint64_t pts_ns;
        uint64_t start_ns;
        uint64_t seq;
    };
    auto* meta = static_cast<FrameMetadata*>(sourceFrameRefCon);
    uint64_t pts = meta ? meta->pts_ns : 0;
    uint64_t start = meta ? meta->start_ns : 0;
    uint64_t seq = meta ? meta->seq : 0;
    delete meta;

    encoder->OnCompressionOutput(status, infoFlags, (__bridge void*)sampleBuffer, pts, start, seq);
}
#endif

namespace duwn::direct {

H264VideoToolboxEncoder::H264VideoToolboxEncoder() = default;

H264VideoToolboxEncoder::~H264VideoToolboxEncoder() {
    Close();
}

bool H264VideoToolboxEncoder::IsHardwareEncoderAvailable() noexcept {
#if defined(__APPLE__)
    CFArrayRef encoderList = nullptr;
    if (VTCopyVideoEncoderList(nullptr, &encoderList) != noErr || !encoderList) {
        return false;
    }

    bool has_hw_h264 = false;
    CFIndex count = CFArrayGetCount(encoderList);
    for (CFIndex i = 0; i < count; ++i) {
        CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(encoderList, i);
        CFNumberRef codecTypeNum = (CFNumberRef)CFDictionaryGetValue(dict, kVTVideoEncoderList_CodecType);
        CFBooleanRef isHw = (CFBooleanRef)CFDictionaryGetValue(dict, kVTVideoEncoderList_IsHardwareAccelerated);

        if (codecTypeNum && isHw) {
            uint32_t codecType = 0;
            CFNumberGetValue(codecTypeNum, kCFNumberSInt32Type, &codecType);
            if (codecType == kCMVideoCodecType_H264 && CFBooleanGetValue(isHw)) {
                has_hw_h264 = true;
                break;
            }
        }
    }
    CFRelease(encoderList);
    return has_hw_h264;
#else
    return false;
#endif
}

bool H264VideoToolboxEncoder::Initialize(const DirectEncoderConfig& config) {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (m_initialized.load(std::memory_order_relaxed)) {
        return true;
    }

    m_config = config;

#if defined(__APPLE__)
    // Explicit encoder specifications: Require Hardware Acceleration
    NSDictionary* encoderSpecs = @{
        (id)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder: @(YES),
        (id)kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder: @(YES)
    };

    // Pixel buffer attributes for incoming frames
    NSDictionary* pixelBufferAttributes = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        (id)kCVPixelBufferWidthKey: @(config.width),
        (id)kCVPixelBufferHeightKey: @(config.height),
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
    };

    VTCompressionSessionRef session = nullptr;
    OSStatus status = VTCompressionSessionCreate(kCFAllocatorDefault,
                                                 config.width,
                                                 config.height,
                                                 kCMVideoCodecType_H264,
                                                 (__bridge CFDictionaryRef)encoderSpecs,
                                                 (__bridge CFDictionaryRef)pixelBufferAttributes,
                                                 kCFAllocatorDefault,
                                                 VTCompressionCallback,
                                                 this,
                                                 &session);

    if (status != noErr || !session) {
        // Fallback: try hardware without strict requirement if device has specific profile restriction
        encoderSpecs = @{
            (id)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder: @(YES)
        };
        status = VTCompressionSessionCreate(kCFAllocatorDefault,
                                            config.width,
                                            config.height,
                                            kCMVideoCodecType_H264,
                                            (__bridge CFDictionaryRef)encoderSpecs,
                                            (__bridge CFDictionaryRef)pixelBufferAttributes,
                                            kCFAllocatorDefault,
                                            VTCompressionCallback,
                                            this,
                                            &session);
        if (status != noErr || !session) {
            return false;
        }
    }

    m_session = session;
    m_is_hardware.store(true, std::memory_order_release);

    // 1. Enforce Real-Time mode
    VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);

    // 2. Enforce High or Baseline profile depending on latency mode
    CFStringRef profile = kVTProfileLevel_H264_High_AutoLevel;
    if (config.latency_mode == DirectEncoderLatencyMode::LowestLatency) {
        profile = kVTProfileLevel_H264_Main_AutoLevel;
    }
    VTSessionSetProperty(session, kVTCompressionPropertyKey_ProfileLevel, profile);

    // 3. CRITICAL INVARIANT: NO FRAME REORDERING / NO B-FRAMES
    VTSessionSetProperty(session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);

    // 4. Zero frame delay / lookahead: emit compressed slices immediately
    int delayCount = static_cast<int>(config.max_frame_delay_count);
    CFNumberRef delayNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &delayCount);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_MaxFrameDelayCount, delayNum);
    CFRelease(delayNum);

    // 5. Expected frame rate
    int fps = static_cast<int>(config.fps > 0 ? config.fps : 60);
    CFNumberRef fpsNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &fps);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_ExpectedFrameRate, fpsNum);
    CFRelease(fpsNum);

    // 6. Target bitrate & Data rate limits
    int bitrate = static_cast<int>(config.bitrate_target_bps);
    CFNumberRef bitrateNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &bitrate);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_AverageBitRate, bitrateNum);
    CFRelease(bitrateNum);

    // Ceiling limit: e.g. target * 1.5 per 1-second window
    int ceilingBytes = static_cast<int>(config.bitrate_ceiling_bps / 8);
    NSArray* limits = @[@(ceilingBytes), @(1.0)];
    VTSessionSetProperty(session, kVTCompressionPropertyKey_DataRateLimits, (__bridge CFArrayRef)limits);

    // 7. Keyframe interval
    int keyframeInterval = static_cast<int>(config.fps * config.keyframe_interval_seconds);
    CFNumberRef keyframeNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &keyframeInterval);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_MaxKeyFrameInterval, keyframeNum);
    CFRelease(keyframeNum);

    CFNumberRef keyframeSecNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &config.keyframe_interval_seconds);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, keyframeSecNum);
    CFRelease(keyframeSecNum);

    // Prepare session for compression
    VTCompressionSessionPrepareToEncodeFrames(session);

    m_running.store(true, std::memory_order_release);
    m_worker_thread = std::thread(&H264VideoToolboxEncoder::WorkerLoop, this);
    m_initialized.store(true, std::memory_order_release);
    return true;
#else
    return false;
#endif
}

bool H264VideoToolboxEncoder::Reconfigure(uint32_t bitrate_target_bps, uint32_t fps) {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_config.bitrate_target_bps = bitrate_target_bps;
    m_config.fps = fps;

#if defined(__APPLE__)
    if (m_session) {
        VTCompressionSessionRef session = static_cast<VTCompressionSessionRef>(m_session);

        int bitrate = static_cast<int>(bitrate_target_bps);
        CFNumberRef bitrateNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &bitrate);
        VTSessionSetProperty(session, kVTCompressionPropertyKey_AverageBitRate, bitrateNum);
        CFRelease(bitrateNum);

        int fpsVal = static_cast<int>(fps);
        CFNumberRef fpsNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &fpsVal);
        VTSessionSetProperty(session, kVTCompressionPropertyKey_ExpectedFrameRate, fpsNum);
        CFRelease(fpsNum);
        return true;
    }
#endif
    return false;
}

void H264VideoToolboxEncoder::RequestKeyframe() {
    m_force_keyframe.store(true, std::memory_order_release);
}

void H264VideoToolboxEncoder::SubmitFrame(RawVideoFrameInput&& frame) {
    // 0 or 1 latest-frame invariant: replaces any unconsumed pending frame immediately
    m_input_slot.Put(std::move(frame));
    m_cv.notify_one();
}

bool H264VideoToolboxEncoder::EncodeImmediate(const RawVideoFrameInput& frame) {
#if defined(__APPLE__)
    if (!m_session || !frame.buffer_handle) return false;

    VTCompressionSessionRef session = static_cast<VTCompressionSessionRef>(m_session);
    CVPixelBufferRef pixelBuffer = static_cast<CVPixelBufferRef>(frame.buffer_handle);

    uint64_t start_ns = GetEncoderMonotonicNs();

    struct FrameMetadata {
        uint64_t pts_ns;
        uint64_t start_ns;
        uint64_t seq;
    };
    auto* meta = new FrameMetadata{frame.source_timestamp_ns, start_ns, frame.frame_sequence};

    CMTime pts = CMTimeMake(static_cast<int64_t>(frame.source_timestamp_ns), 1000000000);
    CMTime duration = CMTimeMake(1, static_cast<int32_t>(m_config.fps > 0 ? m_config.fps : 60));

    NSDictionary* frameProperties = nil;
    if (frame.force_keyframe || m_force_keyframe.exchange(false, std::memory_order_acq_rel)) {
        frameProperties = @{ (id)kVTEncodeFrameOptionKey_ForceKeyFrame: @(YES) };
    }

    VTEncodeInfoFlags flagsOut = 0;
    OSStatus status = VTCompressionSessionEncodeFrame(session,
                                                     pixelBuffer,
                                                     pts,
                                                     duration,
                                                     (__bridge CFDictionaryRef)frameProperties,
                                                     meta,
                                                     &flagsOut);
    return (status == noErr);
#else
    return false;
#endif
}

void H264VideoToolboxEncoder::Flush() {
#if defined(__APPLE__)
    if (m_session) {
        VTCompressionSessionCompleteFrames(static_cast<VTCompressionSessionRef>(m_session), kCMTimeInvalid);
    }
#endif
    m_input_slot.Clear();
}

void H264VideoToolboxEncoder::Close() {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) return;

    m_cv.notify_all();
    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }

#if defined(__APPLE__)
    if (m_session) {
        VTCompressionSessionInvalidate(static_cast<VTCompressionSessionRef>(m_session));
        CFRelease(static_cast<VTCompressionSessionRef>(m_session));
        m_session = nullptr;
    }
#endif

    m_initialized.store(false, std::memory_order_release);
    m_input_slot.Clear();
}

void H264VideoToolboxEncoder::SetOutputCallback(std::function<void(EncodedVideoFrame&&)> callback) {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_output_callback = std::move(callback);
}

DirectEncoderConfig H264VideoToolboxEncoder::GetConfig() const {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_config;
}

EncoderMetrics H264VideoToolboxEncoder::GetMetrics() const {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    EncoderMetrics m = m_metrics;
    m.superseded_input_frames = m_input_slot.GetSupersededCount();
    m.total_input_frames = m_input_slot.GetTotalInputFrames();
    return m;
}

void H264VideoToolboxEncoder::WorkerLoop() {
    while (m_running.load(std::memory_order_relaxed)) {
        RawVideoFrameInput frame;
        {
            std::unique_lock<std::mutex> lock(m_worker_mutex);
            m_cv.wait(lock, [this]() {
                return !m_running.load(std::memory_order_relaxed) || m_input_slot.HasFrame();
            });

            if (!m_running.load(std::memory_order_relaxed)) {
                break;
            }

            if (!m_input_slot.Take(frame)) {
                continue;
            }
        }

        EncodeImmediate(frame);
    }
}

void H264VideoToolboxEncoder::OnCompressionOutput(int status,
                                                 unsigned int info_flags,
                                                 void* sample_buffer_ref,
                                                 uint64_t source_pts_ns,
                                                 uint64_t encode_start_ns,
                                                 uint64_t frame_seq) {
#if defined(__APPLE__)
    if (status != noErr || !sample_buffer_ref) {
        return;
    }

    CMSampleBufferRef sampleBuffer = static_cast<CMSampleBufferRef>(sample_buffer_ref);
    uint64_t encode_end_ns = GetEncoderMonotonicNs();

    bool is_keyframe = false;
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    if (attachments && CFArrayGetCount(attachments) > 0) {
        CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
        CFBooleanRef notSync = (CFBooleanRef)CFDictionaryGetValue(dict, kCMSampleAttachmentKey_NotSync);
        is_keyframe = (!notSync || !CFBooleanGetValue(notSync));
    }

    EncodedVideoFrame encoded_frame;
    encoded_frame.frame_id = frame_seq;
    encoded_frame.is_keyframe = is_keyframe;
    encoded_frame.source_timestamp_ns = source_pts_ns;
    encoded_frame.encode_start_timestamp_ns = encode_start_ns;
    encoded_frame.encode_end_timestamp_ns = encode_end_ns;
    encoded_frame.width = m_config.width;
    encoded_frame.height = m_config.height;

    // Convert sample buffer to Annex-B format NAL units
    CMFormatDescriptionRef formatDesc = CMSampleBufferGetFormatDescription(sampleBuffer);

    // If keyframe, extract SPS and PPS and prepend Annex-B start codes (0x00000001)
    if (is_keyframe && formatDesc) {
        size_t spsSize = 0, ppsSize = 0;
        size_t paramCount = 0;
        const uint8_t* spsPtr = nullptr;
        const uint8_t* ppsPtr = nullptr;

        CMVideoFormatDescriptionGetH264ParameterSetAtIndex(formatDesc, 0, &spsPtr, &spsSize, &paramCount, nullptr);
        CMVideoFormatDescriptionGetH264ParameterSetAtIndex(formatDesc, 1, &ppsPtr, &ppsSize, &paramCount, nullptr);

        if (spsPtr && spsSize > 0) {
            uint8_t startCode[] = {0x00, 0x00, 0x00, 0x01};
            encoded_frame.payload.insert(encoded_frame.payload.end(), startCode, startCode + 4);
            encoded_frame.payload.insert(encoded_frame.payload.end(), spsPtr, spsPtr + spsSize);
        }

        if (ppsPtr && ppsSize > 0) {
            uint8_t startCode[] = {0x00, 0x00, 0x00, 0x01};
            encoded_frame.payload.insert(encoded_frame.payload.end(), startCode, startCode + 4);
            encoded_frame.payload.insert(encoded_frame.payload.end(), ppsPtr, ppsPtr + ppsSize);
        }
    }

    // Extract slice NAL units from block buffer
    CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (blockBuffer) {
        size_t totalLength = CMBlockBufferGetDataLength(blockBuffer);
        size_t offset = 0;

        while (offset < totalLength) {
            uint32_t nalUnitLength = 0;
            CMBlockBufferCopyDataBytes(blockBuffer, offset, 4, &nalUnitLength);
            nalUnitLength = CFSwapInt32BigToHost(nalUnitLength);
            offset += 4;

            if (offset + nalUnitLength <= totalLength) {
                uint8_t startCode[] = {0x00, 0x00, 0x00, 0x01};
                encoded_frame.payload.insert(encoded_frame.payload.end(), startCode, startCode + 4);

                size_t currentPayloadSize = encoded_frame.payload.size();
                encoded_frame.payload.resize(currentPayloadSize + nalUnitLength);
                CMBlockBufferCopyDataBytes(blockBuffer, offset, nalUnitLength, encoded_frame.payload.data() + currentPayloadSize);
                offset += nalUnitLength;
            } else {
                break;
            }
        }
    }

    // Calculate latency
    double latency_ms = static_cast<double>(encoded_frame.EncodeDurationNs()) / 1'000'000.0;

    std::function<void(EncodedVideoFrame&&)> callback_copy;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_metrics.encoded_frames_count++;
        m_metrics.total_encoded_bytes += encoded_frame.payload.size();
        if (is_keyframe) {
            m_metrics.keyframe_count++;
        }
        UpdateLatencyMetrics(latency_ms);
        callback_copy = m_output_callback;
    }

    if (callback_copy) {
        callback_copy(std::move(encoded_frame));
    }
#endif
}

void H264VideoToolboxEncoder::UpdateLatencyMetrics(double latency_ms) {
    m_metrics.last_encode_latency_ms = latency_ms;
    if (latency_ms > m_metrics.max_encode_latency_ms) {
        m_metrics.max_encode_latency_ms = latency_ms;
    }

    m_latency_history.push_back(latency_ms);
    // Keep bounded history window (last 600 frames = 10s at 60fps)
    if (m_latency_history.size() > 600) {
        m_latency_history.erase(m_latency_history.begin());
    }

    std::vector<double> sorted = m_latency_history;
    std::sort(sorted.begin(), sorted.end());

    size_t count = sorted.size();
    if (count > 0) {
        m_metrics.p50_encode_latency_ms = sorted[count * 50 / 100];
        m_metrics.p95_encode_latency_ms = sorted[count * 95 / 100];
        m_metrics.p99_encode_latency_ms = sorted[count * 99 / 100];
    }
}

} // namespace duwn::direct
