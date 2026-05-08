// =============================================================================
// AudioBuffer.hpp — Owns a block of audio samples in memory.
//
// Design decisions worth understanding
// -------------------------------------
// 1. NOT copyable — copying large audio buffers silently is a bug waiting to
//    happen. We delete the copy constructor and copy assignment operator to
//    make accidental copies a compile-time error instead of a runtime surprise.
//
// 2. IS movable — transferring ownership of a buffer is cheap and explicit.
//    std::move() makes the intent visible at the call site.
//
// 3. std::span for non-owning access — when another module needs to read or
//    write samples, we give it a std::span<float> rather than a raw pointer.
//    A span carries both the pointer and the size, so there is no way to
//    accidentally read past the end of the buffer.
//
// 4. [[nodiscard]] — functions marked [[nodiscard]] produce a compiler warning
//    if their return value is ignored. Applied to all accessors because
//    ignoring the return value of samples() or sampleRate() is always a bug.
//
// 5. const correctness — every method that does not modify the buffer is
//    marked const. This means a const AudioBuffer& can only call const methods,
//    and the compiler enforces it.
// =============================================================================

#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace eq {

// -----------------------------------------------------------------------------
// AudioBuffer
//
// Owns a contiguous block of 32-bit float audio samples.
// Samples are stored interleaved for multi-channel audio:
//   [L0, R0, L1, R1, L2, R2, ...]
//
// For mono audio there is only one channel, so the layout is simply:
//   [S0, S1, S2, S3, ...]
// -----------------------------------------------------------------------------
class AudioBuffer {
public:
    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    // Construct an AudioBuffer with the given number of frames, channels,
    // and sample rate. All samples are zero-initialised.
    //
    // Parameters
    // ----------
    // n_frames      : number of sample frames (one frame = one sample per channel)
    // n_channels    : number of audio channels (1 = mono, 2 = stereo)
    // sample_rate   : samples per second (e.g. 44100, 48000)
    //
    // Throws std::invalid_argument if any parameter is zero.
    AudioBuffer(std::size_t n_frames,
                std::size_t n_channels,
                std::uint32_t sample_rate);

    // Construct from an existing vector of samples.
    // The vector is moved into the buffer — no copy is made.
    //
    // Throws std::invalid_argument if n_channels or sample_rate is zero,
    // or if samples.size() is not divisible by n_channels.
    AudioBuffer(std::vector<float> samples,
                std::size_t n_channels,
                std::uint32_t sample_rate);

    // Default destructor — std::vector cleans up automatically (RAII).
    ~AudioBuffer() = default;

    // -------------------------------------------------------------------------
    // Non-copyable, movable
    //
    // Copying is deleted to prevent accidental expensive copies.
    // Moving is defaulted — transfers ownership of the internal vector
    // in O(1) time, leaving the source in a valid but empty state.
    // -------------------------------------------------------------------------
    AudioBuffer(const AudioBuffer&)            = delete;
    AudioBuffer& operator=(const AudioBuffer&) = delete;

    AudioBuffer(AudioBuffer&&)            noexcept = default;
    AudioBuffer& operator=(AudioBuffer&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Accessors — all const-correct and [[nodiscard]]
    // -------------------------------------------------------------------------

    // Total number of sample frames.
    // For stereo audio: n_frames() == samples().size() / 2
    [[nodiscard]] std::size_t n_frames()   const noexcept;

    // Number of audio channels (1 = mono, 2 = stereo).
    [[nodiscard]] std::size_t n_channels() const noexcept;

    // Sample rate in Hz (e.g. 44100, 48000).
    [[nodiscard]] std::uint32_t sample_rate() const noexcept;

    // Total number of individual float samples stored.
    // Equal to n_frames() * n_channels().
    [[nodiscard]] std::size_t size() const noexcept;

    // Duration of the buffer in seconds.
    [[nodiscard]] double duration_seconds() const noexcept;

    // True if the buffer contains no samples.
    [[nodiscard]] bool empty() const noexcept;

    // Non-owning view of all samples (read-write).
    // The span is valid as long as this AudioBuffer is alive.
    // Do NOT store the span longer than the buffer.
    [[nodiscard]] std::span<float> samples() noexcept;

    // Non-owning view of all samples (read-only).
    [[nodiscard]] std::span<const float> samples() const noexcept;

    // Non-owning view of a single channel's samples.
    // For mono audio, channel_index must be 0.
    // For stereo, 0 = left, 1 = right.
    //
    // Note: this returns a COPY of the channel samples into a new vector
    // because interleaved storage means channel samples are not contiguous.
    // Use samples() directly if you need maximum performance.
    //
    // Throws std::out_of_range if channel_index >= n_channels().
    [[nodiscard]] std::vector<float> channel(std::size_t channel_index) const;

    // Human-readable description for debugging and logging.
    [[nodiscard]] std::string describe() const;

private:
    std::vector<float> samples_;    // interleaved sample data
    std::size_t        n_channels_; // number of channels
    std::uint32_t      sample_rate_;// samples per second
};

} // namespace eq