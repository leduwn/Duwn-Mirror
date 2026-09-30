#include "InAppSnapshotBackend.h"

#if defined(__APPLE__)
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreVideo/CoreVideo.h>
#include <mach/mach_time.h>

@interface DuwnInAppDisplayLinkTarget : NSObject
@property (nonatomic, assign) std::function<void()>* tickHandler;
- (void)onTick:(CADisplayLink*)link;
@end

@implementation DuwnInAppDisplayLinkTarget
- (void)onTick:(CADisplayLink*)link {
    if (_tickHandler && *_tickHandler) {
        (*_tickHandler)();
    }
}
@end

static uint64_t GetInAppSnapshotHostMonotonicNs() {
    static mach_timebase_info_data_t timebase;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        mach_timebase_info(&timebase);
    });
    uint64_t time = mach_absolute_time();
    return (time * timebase.numer) / timebase.denom;
}
#endif

namespace duwn::direct::ios {

InAppSnapshotBackend::InAppSnapshotBackend() {
    m_capabilities.backend_type = DirectCaptureBackendType::InAppSnapshot;
    m_capabilities.backend_name = "In-App CADisplayLink Snapshot (OwnApplication Only)";
    m_capabilities.capture_scope = CaptureScope::OwnApplication; // Strictly OwnApplication
    m_capabilities.is_legacy = true;
    m_capabilities.uses_deprecated_api = true; // Uses deprecated keyWindow on iOS 13+
    m_capabilities.supports_video = true;
    m_capabilities.supports_system_audio = false;
    m_capabilities.supports_microphone = false;
    m_capabilities.supports_background_capture = false;
    m_capabilities.supported_pixel_formats = {
        CapturePixelFormat::BGRA32
    };
    m_capabilities.preferred_pixel_format = CapturePixelFormat::BGRA32;
    m_capabilities.supports_native_yuv = false;
    m_capabilities.verification_status = BackendVerificationStatus::SourceOnlyNotBuilt;
}

InAppSnapshotBackend::~InAppSnapshotBackend() {
    StopCapture();
}

bool InAppSnapshotBackend::IsAvailable() noexcept {
#if defined(__APPLE__)
    return true; // CADisplayLink is available in UIKit
#else
    return false;
#endif
}

bool InAppSnapshotBackend::Initialize() {
    if (m_initialized.load(std::memory_order_relaxed)) return true;

#if defined(__APPLE__)
    UIScreen* mainScreen = [UIScreen mainScreen];
    CGRect bounds = [mainScreen bounds];
    CGFloat scale = [mainScreen scale];

    m_capabilities.screen_width = static_cast<uint32_t>(bounds.size.width * scale);
    m_capabilities.screen_height = static_cast<uint32_t>(bounds.size.height * scale);
    m_capabilities.screen_scale = static_cast<float>(scale);
    m_capabilities.max_observed_cadence_fps = 60;

    // Zero-allocation buffer pool for BGRA frame snapshots
    NSDictionary* poolAttributes = @{
        (id)kCVPixelBufferPoolMinimumBufferCountKey: @(3)
    };
    NSDictionary* pixelBufferAttributes = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (id)kCVPixelBufferWidthKey: @(m_capabilities.screen_width),
        (id)kCVPixelBufferHeightKey: @(m_capabilities.screen_height),
        (id)kCVPixelFormatOpenGLESCompatibility: @(YES),
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
    };

    CVPixelBufferPoolRef pool = nullptr;
    CVReturn status = CVPixelBufferPoolCreate(kCFAllocatorDefault,
                                              (__bridge CFDictionaryRef)poolAttributes,
                                              (__bridge CFDictionaryRef)pixelBufferAttributes,
                                              &pool);
    if (status == kCVReturnSuccess) {
        m_pixel_buffer_pool = pool;
    }

    m_initialized.store(true, std::memory_order_release);
    return true;
#else
    return false;
#endif
}

bool InAppSnapshotBackend::StartCapture() {
    if (m_capturing.load(std::memory_order_relaxed)) return true;
    if (!Initialize()) return false;

#if defined(__APPLE__)
    dispatch_async(dispatch_get_main_queue(), ^{
        DuwnInAppDisplayLinkTarget* target = [[DuwnInAppDisplayLinkTarget alloc] init];
        auto handler = new std::function<void()>([this]() {
            this->OnDisplayLinkTick();
        });
        target.tickHandler = handler;

        CADisplayLink* link = [CADisplayLink displayLinkWithTarget:target selector:@selector(onTick:)];
        if (@available(iOS 15.0, *)) {
            link.preferredFrameRateRange = CAFrameRateRangeMake(30.0f, 60.0f, 60.0f);
        } else {
            link.preferredFramesPerSecond = 60;
        }

        [link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
        this->m_display_link = (__bridge_retained void*)link;
    });

    m_capturing.store(true, std::memory_order_release);
    return true;
#else
    return false;
#endif
}

void InAppSnapshotBackend::StopCapture() {
    if (!m_capturing.exchange(false, std::memory_order_acq_rel)) return;

#if defined(__APPLE__)
    if (m_display_link) {
        CADisplayLink* link = (__bridge_transfer CADisplayLink*)m_display_link;
        [link invalidate];
        m_display_link = nullptr;
    }

    if (m_pixel_buffer_pool) {
        CVPixelBufferPoolRelease((CVPixelBufferPoolRef)m_pixel_buffer_pool);
        m_pixel_buffer_pool = nullptr;
    }
#endif
    m_slot.Clear();
}

CaptureCapabilities InAppSnapshotBackend::GetCapabilities() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_capabilities;
}

void InAppSnapshotBackend::SetVideoFrameCallback(std::function<void(const CapturedVideoFrame&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_video_callback = std::move(callback);
}

void InAppSnapshotBackend::SetAudioFrameCallback(std::function<void(const CapturedAudioFrame&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_audio_callback = std::move(callback);
}

void InAppSnapshotBackend::OnDisplayLinkTick() {
#if defined(__APPLE__)
    if (!m_capturing.load(std::memory_order_relaxed)) return;

    uint64_t callback_time_ns = GetInAppSnapshotHostMonotonicNs();

    CVPixelBufferPoolRef pool = (CVPixelBufferPoolRef)m_pixel_buffer_pool;
    if (!pool) return;

    CVPixelBufferRef pixelBuffer = nullptr;
    CVReturn err = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &pixelBuffer);
    if (err != kCVReturnSuccess || !pixelBuffer) return;

    CVPixelBufferLockBaseAddress(pixelBuffer, 0);
    void* baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(baseAddress,
                                                 width,
                                                 height,
                                                 8,
                                                 bytesPerRow,
                                                 colorSpace,
                                                 kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);

    UIWindow* keyWindow = [UIApplication sharedApplication].keyWindow;
    if (keyWindow && context) {
        UIGraphicsPushContext(context);
        [keyWindow drawViewHierarchyInRect:keyWindow.bounds afterScreenUpdates:NO];
        UIGraphicsPopContext();
    }

    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);
    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);

    CaptureTimestamp ts;
    ts.source_timestamp_ns = callback_time_ns;
    ts.callback_timestamp_ns = callback_time_ns;

    CapturedVideoFrame frame(pixelBuffer,
                             static_cast<uint32_t>(width),
                             static_cast<uint32_t>(height),
                             CapturePixelFormat::BGRA32,
                             ts,
                             CaptureOrientation::Portrait,
                             ++m_frame_sequence);

    CVPixelBufferRelease(pixelBuffer);

    m_slot.Put(std::move(frame));
#endif
}

} // namespace duwn::direct::ios
