#include "ReplayKitLegacyBackend.h"

#if defined(__APPLE__)
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <ReplayKit/ReplayKit.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>
#include <mach/mach_time.h>
#endif

namespace duwn::direct::ios {

#if defined(__APPLE__)
static uint64_t GetReplayKitHostMonotonicNs() {
    static mach_timebase_info_data_t timebase;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        mach_timebase_info(&timebase);
    });
    uint64_t time = mach_absolute_time();
    return (time * timebase.numer) / timebase.denom;
}

static CaptureOrientation GetReplayKitOrientation() {
    UIInterfaceOrientation orientation = [UIApplication sharedApplication].statusBarOrientation;
    switch (orientation) {
        case UIInterfaceOrientationPortrait:
            return CaptureOrientation::Portrait;
        case UIInterfaceOrientationPortraitUpsideDown:
            return CaptureOrientation::PortraitUpsideDown;
        case UIInterfaceOrientationLandscapeLeft:
            return CaptureOrientation::LandscapeLeft;
        case UIInterfaceOrientationLandscapeRight:
            return CaptureOrientation::LandscapeRight;
        default:
            return CaptureOrientation::Unknown;
    }
}
#endif

ReplayKitLegacyBackend::ReplayKitLegacyBackend() {
    m_capabilities.backend_type = DirectCaptureBackendType::ReplayKitLegacy;
    m_capabilities.backend_name = "ReplayKit (Legacy Deprecated)";
    m_capabilities.capture_scope = CaptureScope::OwnApplication; // In-app capture only: records host app
    m_capabilities.is_legacy = true;
    m_capabilities.uses_deprecated_api = true;
    m_capabilities.supports_video = true;
    m_capabilities.supports_system_audio = true;
    m_capabilities.supports_microphone = true;
    m_capabilities.supports_background_capture = false;
    m_capabilities.supported_pixel_formats = {
        CapturePixelFormat::NV12_VideoRange,
        CapturePixelFormat::NV12_FullRange,
        CapturePixelFormat::BGRA32
    };
    m_capabilities.preferred_pixel_format = CapturePixelFormat::NV12_VideoRange;
    m_capabilities.supports_native_yuv = true;
    m_capabilities.verification_status = BackendVerificationStatus::LegacyUnverified;
}

ReplayKitLegacyBackend::~ReplayKitLegacyBackend() {
    StopCapture();
}

bool ReplayKitLegacyBackend::IsAvailable() noexcept {
#if defined(__APPLE__)
    // Dynamic runtime check: RPScreenRecorder presence
    Class recorderClass = NSClassFromString(@"RPScreenRecorder");
    if (!recorderClass) return false;
    RPScreenRecorder* recorder = [RPScreenRecorder sharedRecorder];
    return (recorder != nil && recorder.isAvailable);
#else
    return false;
#endif
}

bool ReplayKitLegacyBackend::IsVerifiedOnRuntime() noexcept {
    // Cannot claim legacy APIs work without physical Apple build and device testing.
    // Must remain false until verified on device.
    return false;
}

bool ReplayKitLegacyBackend::Initialize() {
    if (m_initialized.load(std::memory_order_relaxed)) return true;

#if defined(__APPLE__)
    if (!IsAvailable()) return false;

    UIScreen* mainScreen = [UIScreen mainScreen];
    CGRect bounds = [mainScreen bounds];
    CGFloat scale = [mainScreen scale];

    m_capabilities.screen_width = static_cast<uint32_t>(bounds.size.width * scale);
    m_capabilities.screen_height = static_cast<uint32_t>(bounds.size.height * scale);
    m_capabilities.screen_scale = static_cast<float>(scale);
    m_capabilities.max_observed_cadence_fps = static_cast<uint32_t>(mainScreen.maximumFramesPerSecond > 0 ? mainScreen.maximumFramesPerSecond : 60);

    m_initialized.store(true, std::memory_order_release);
    return true;
#else
    return false;
#endif
}

bool ReplayKitLegacyBackend::StartCapture() {
    if (m_capturing.load(std::memory_order_relaxed)) return true;
    if (!Initialize()) return false;

#if defined(__APPLE__)
    RPScreenRecorder* recorder = [RPScreenRecorder sharedRecorder];
    if (!recorder.isAvailable) return false;

    recorder.microphoneEnabled = NO;

    [recorder startCaptureWithHandler:^(CMSampleBufferRef _Nonnull sampleBuffer, RPSampleBufferType sampleBufferType, NSError * _Nullable error) {
        if (error) return;
        this->HandleSampleBuffer((__bridge void*)sampleBuffer, static_cast<int>(sampleBufferType));
    } completionHandler:^(NSError * _Nullable error) {
        if (error) {
            m_capturing.store(false, std::memory_order_release);
        }
    }];

    m_capturing.store(true, std::memory_order_release);
    return true;
#else
    return false;
#endif
}

void ReplayKitLegacyBackend::StopCapture() {
    if (!m_capturing.exchange(false, std::memory_order_acq_rel)) return;

#if defined(__APPLE__)
    RPScreenRecorder* recorder = [RPScreenRecorder sharedRecorder];
    if (recorder.isRecording) {
        [recorder stopCaptureWithHandler:^(NSError * _Nullable error) {}];
    }
#endif
    m_slot.Clear();
}

CaptureCapabilities ReplayKitLegacyBackend::GetCapabilities() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_capabilities;
}

void ReplayKitLegacyBackend::SetVideoFrameCallback(std::function<void(const CapturedVideoFrame&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_video_callback = std::move(callback);
}

void ReplayKitLegacyBackend::SetAudioFrameCallback(std::function<void(const CapturedAudioFrame&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_audio_callback = std::move(callback);
}

void ReplayKitLegacyBackend::HandleSampleBuffer(void* sample_buffer_ref, int sample_type) {
#if defined(__APPLE__)
    if (!m_capturing.load(std::memory_order_relaxed) || !sample_buffer_ref) return;

    CMSampleBufferRef sampleBuffer = (__bridge CMSampleBufferRef)sample_buffer_ref;
    uint64_t callback_time_ns = GetReplayKitHostMonotonicNs();

    if (sample_type == RPSampleBufferTypeVideo) {
        CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
        if (!pixelBuffer) return;

        size_t width = CVPixelBufferGetWidth(pixelBuffer);
        size_t height = CVPixelBufferGetHeight(pixelBuffer);
        OSType formatType = CVPixelBufferGetPixelFormatType(pixelBuffer);

        CapturePixelFormat pixel_format = CapturePixelFormat::Unknown;
        if (formatType == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
            pixel_format = CapturePixelFormat::NV12_VideoRange;
        } else if (formatType == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
            pixel_format = CapturePixelFormat::NV12_FullRange;
        } else if (formatType == kCVPixelFormatType_32BGRA) {
            pixel_format = CapturePixelFormat::BGRA32;
        }

        CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
        uint64_t pts_ns = (pts.timescale > 0)
            ? (static_cast<uint64_t>(pts.value) * 1'000'000'000ULL / pts.timescale)
            : callback_time_ns;

        CaptureTimestamp ts;
        ts.source_timestamp_ns = pts_ns;
        ts.callback_timestamp_ns = callback_time_ns;

        CaptureOrientation orientation = GetReplayKitOrientation();
        uint64_t seq = ++m_frame_sequence;

        CapturedVideoFrame frame(pixelBuffer,
                                 static_cast<uint32_t>(width),
                                 static_cast<uint32_t>(height),
                                 pixel_format,
                                 ts,
                                 orientation,
                                 seq);

        std::function<void(const CapturedVideoFrame&)> cb_copy;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            cb_copy = m_video_callback;
        }
        if (cb_copy) {
            cb_copy(frame);
        }

        m_slot.Put(std::move(frame));
    }
#endif
}

} // namespace duwn::direct::ios
