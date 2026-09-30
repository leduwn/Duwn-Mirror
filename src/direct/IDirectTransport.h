#pragma once
// IDirectTransport.h — Abstract transport interface for Duwn Direct mode.
// Decouples session and frame assembly from specific network protocols (UDP, QUIC, IPC).

#include <cstdint>
#include <cstddef>
#include <functional>

namespace duwn::direct {

class IDirectTransport {
public:
    virtual ~IDirectTransport() = default;

    // Starts listening / receiving packets.
    virtual bool Start() = 0;

    // Stops transport and terminates reader threads.
    virtual void Stop() = 0;

    // Checks if transport is actively listening.
    virtual bool IsRunning() const noexcept = 0;

    // Sets callback invoked when a raw packet arrives.
    virtual void SetPacketCallback(std::function<void(const uint8_t* data, size_t size)> callback) = 0;

    // Sends a raw packet across the transport (primarily for tests / telemetry / control).
    virtual bool SendPacket(const uint8_t* data, size_t size) = 0;

    // Bound local port (0 if not applicable)
    virtual uint16_t GetBoundPort() const noexcept = 0;
};

} // namespace duwn::direct
