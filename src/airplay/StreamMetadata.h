#pragma once
// StreamMetadata — negotiated stream parameters reported by the AirPlay sidecar.

#include <cstdint>
#include <string>

namespace duwn::airplay {

enum class VideoCodec { Unknown, H264, H265 };
enum class AudioCodec { Unknown, AAC, AAC_ELD, ALAC, PCM };

struct StreamMetadata {
    // Video
    VideoCodec video_codec{VideoCodec::Unknown};
    uint32_t   video_width{0};
    uint32_t   video_height{0};
    double     video_fps{0.0};

    // Audio
    AudioCodec audio_codec{AudioCodec::Unknown};
    uint32_t   audio_sample_rate{0};
    uint8_t    audio_channels{0};

    // RTP clock rates (set when receiver negotiates)
    uint32_t video_rtp_clock_rate{90000}; // AirPlay standard
    uint32_t audio_rtp_clock_rate{0};     // matches audio_sample_rate

    bool IsValid() const noexcept {
        return video_codec != VideoCodec::Unknown
            && video_width > 0
            && video_height > 0;
    }
};

} // namespace duwn::airplay
