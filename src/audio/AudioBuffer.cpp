// =============================================================================
// AudioBuffer.cpp — Implementation of AudioBuffer.
//
// Key C++20 concepts demonstrated here
// -------------------------------------
// - Member initialiser lists: initialise members in the constructor body
//   header rather than assigning inside {}. This is more efficient because
//   it constructs directly rather than default-constructing then assigning.
//
// - std::format (C++20): type-safe string formatting. Replaces sprintf and
//   most uses of std::ostringstream.
//
// - Structured validation: every constructor validates its inputs and throws
//   a descriptive exception. This is the "fail fast" principle — catch bad
//   inputs at construction time, not buried deep in DSP code.
// =============================================================================

#include "AudioBuffer.hpp"

#include <format>
#include <numeric>
#include <stdexcept>

namespace eq {

// -----------------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------------

AudioBuffer::AudioBuffer(std::size_t   n_frames,
                         std::size_t   n_channels,
                         std::uint32_t sample_rate)
    : samples_(n_frames * n_channels, 0.0f)
    , n_channels_(n_channels)
    , sample_rate_(sample_rate)
{
    if (n_frames == 0) {
        throw std::invalid_argument("AudioBuffer: n_frames must be > 0");
    }
    if (n_channels == 0) {
        throw std::invalid_argument("AudioBuffer: n_channels must be > 0");
    }
    if (sample_rate == 0) {
        throw std::invalid_argument("AudioBuffer: sample_rate must be > 0");
    }
}

AudioBuffer::AudioBuffer(std::vector<float> samples,
                         std::size_t        n_channels,
                         std::uint32_t      sample_rate)
    : samples_(std::move(samples))   // O(1) — transfers ownership, no copy
    , n_channels_(n_channels)
    , sample_rate_(sample_rate)
{
    if (n_channels == 0) {
        throw std::invalid_argument("AudioBuffer: n_channels must be > 0");
    }
    if (sample_rate == 0) {
        throw std::invalid_argument("AudioBuffer: sample_rate must be > 0");
    }
    if (samples_.size() % n_channels != 0) {
        throw std::invalid_argument(std::format(
            "AudioBuffer: samples.size() ({}) is not divisible by "
            "n_channels ({})",
            samples_.size(), n_channels
        ));
    }
}

// -----------------------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------------------

std::size_t AudioBuffer::n_frames() const noexcept
{
    // Guard against division by zero — n_channels_ is validated in constructors
    // but defensive code here costs nothing and prevents UB if misused.
    return n_channels_ > 0 ? samples_.size() / n_channels_ : 0;
}

std::size_t AudioBuffer::n_channels() const noexcept
{
    return n_channels_;
}

std::uint32_t AudioBuffer::sample_rate() const noexcept
{
    return sample_rate_;
}

std::size_t AudioBuffer::size() const noexcept
{
    return samples_.size();
}

double AudioBuffer::duration_seconds() const noexcept
{
    if (sample_rate_ == 0) return 0.0;
    return static_cast<double>(n_frames()) / static_cast<double>(sample_rate_);
}

bool AudioBuffer::empty() const noexcept
{
    return samples_.empty();
}

std::span<float> AudioBuffer::samples() noexcept
{
    // std::span does not own the data — it is a lightweight view.
    // The caller must not use this span after the AudioBuffer is destroyed.
    return std::span<float>(samples_);
}

std::span<const float> AudioBuffer::samples() const noexcept
{
    return std::span<const float>(samples_);
}

std::vector<float> AudioBuffer::channel(std::size_t channel_index) const
{
    if (channel_index >= n_channels_) {
        throw std::out_of_range(std::format(
            "AudioBuffer::channel(): index {} out of range "
            "(n_channels = {})",
            channel_index, n_channels_
        ));
    }

    // Interleaved layout: extract every n_channels_-th sample
    // starting at channel_index.
    //
    // Example: stereo buffer [L0 R0 L1 R1 L2 R2]
    //   channel(0) -> [L0 L1 L2]  (start=0, step=2)
    //   channel(1) -> [R0 R1 R2]  (start=1, step=2)
    std::vector<float> result;
    result.reserve(n_frames());

    for (std::size_t frame = 0; frame < n_frames(); ++frame) {
        result.push_back(samples_[frame * n_channels_ + channel_index]);
    }

    return result;
}

std::string AudioBuffer::describe() const
{
    return std::format(
        "AudioBuffer("
        "frames={}, channels={}, sample_rate={} Hz, "
        "duration={:.3f}s, size={} floats)",
        n_frames(), n_channels_, sample_rate_,
        duration_seconds(), samples_.size()
    );
}

} // namespace eq