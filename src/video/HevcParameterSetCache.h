#pragma once
// HevcParameterSetCache.h — Cache latest valid VPS, SPS, PPS parameter sets
// per session/generation and prepend them to Access Units when needed.

#include <vector>
#include <cstdint>
#include <cstddef>
#include <span>

namespace duwn::video {

class HevcParameterSetCache {
public:
    HevcParameterSetCache() = default;

    void SetGeneration(uint32_t generation) noexcept {
        if (generation != m_generation) {
            Clear();
            m_generation = generation;
        }
    }

    uint32_t Generation() const noexcept { return m_generation; }

    void Clear() noexcept {
        m_vps.clear();
        m_sps.clear();
        m_pps.clear();
    }

    void Update(uint8_t nal_type, std::span<const uint8_t> nalu_data, uint32_t generation = 0) noexcept {
        if (generation != 0 && generation != m_generation) {
            Clear();
            m_generation = generation;
        }
        if (nalu_data.empty()) return;

        switch (nal_type) {
        case 32: // VPS
            m_vps.assign(nalu_data.begin(), nalu_data.end());
            break;
        case 33: // SPS
            m_sps.assign(nalu_data.begin(), nalu_data.end());
            break;
        case 34: // PPS
            m_pps.assign(nalu_data.begin(), nalu_data.end());
            break;
        default:
            break;
        }
    }

    bool HasVps() const noexcept { return !m_vps.empty(); }
    bool HasSps() const noexcept { return !m_sps.empty(); }
    bool HasPps() const noexcept { return !m_pps.empty(); }
    bool HasAll() const noexcept { return HasVps() && HasSps() && HasPps(); }

    std::span<const uint8_t> Vps() const noexcept { return m_vps; }
    std::span<const uint8_t> Sps() const noexcept { return m_sps; }
    std::span<const uint8_t> Pps() const noexcept { return m_pps; }

    // Prepend missing parameter sets to an Annex B access unit buffer.
    // Adds start code 00 00 00 01 before each prepended parameter set.
    void PrependMissing(std::vector<uint8_t>& au_buf,
                        bool has_vps, bool has_sps, bool has_pps) const noexcept {
        static constexpr uint8_t kStartCode[4] = {0x00, 0x00, 0x00, 0x01};
        std::vector<uint8_t> prefix;
        prefix.reserve((m_vps.size() + m_sps.size() + m_pps.size()) + 12);

        if (!has_vps && !m_vps.empty()) {
            prefix.insert(prefix.end(), kStartCode, kStartCode + 4);
            prefix.insert(prefix.end(), m_vps.begin(), m_vps.end());
        }
        if (!has_sps && !m_sps.empty()) {
            prefix.insert(prefix.end(), kStartCode, kStartCode + 4);
            prefix.insert(prefix.end(), m_sps.begin(), m_sps.end());
        }
        if (!has_pps && !m_pps.empty()) {
            prefix.insert(prefix.end(), kStartCode, kStartCode + 4);
            prefix.insert(prefix.end(), m_pps.begin(), m_pps.end());
        }

        if (!prefix.empty()) {
            au_buf.insert(au_buf.begin(), prefix.begin(), prefix.end());
        }
    }

private:
    std::vector<uint8_t> m_vps;
    std::vector<uint8_t> m_sps;
    std::vector<uint8_t> m_pps;
    uint32_t             m_generation{0};
};

} // namespace duwn::video
