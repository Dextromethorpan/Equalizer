// =============================================================================
// Equalizer.cpp — Implementation of the 7-band graphic equalizer.
//
// Key C++20 concepts demonstrated
// --------------------------------
// - std::clamp: clamps a value to [min, max] in one call. Cleaner than
//   std::min(std::max(value, min), max).
// - std::ranges::fill: fills a range with a value. Cleaner than a raw loop.
// - Designated initialisers: BinRange{ .first = x, .last = y }
// - std::span as a function parameter: accepts any contiguous range
//   (vector, array, C array) without copying.
// =============================================================================

#include "Equalizer.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

namespace eq {

// -----------------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------------

Equalizer::Equalizer(const StftConfig& config)
    : config_(config)
{
    // Initialise all gains to unity (no change).
    std::ranges::fill(gains_, UNITY_GAIN);

    // Precompute bin ranges for each frequency band.
    // bin_for_hz() returns the nearest bin index for a given frequency.
    // We clamp to valid bin range [0, n_bins - 1].
    const std::size_t max_bin = config_.n_bins() - 1;

    for (std::size_t b = 0; b < N_BANDS; ++b) {
        const auto& band = BANDS[b];

        std::size_t first = static_cast<std::size_t>(
            std::clamp(config_.bin_for_hz(band.low_hz),
                       0, static_cast<int>(max_bin)));

        std::size_t last = static_cast<std::size_t>(
            std::clamp(config_.bin_for_hz(band.high_hz),
                       0, static_cast<int>(max_bin)));

        // Ensure first <= last even if rounding causes them to coincide.
        if (first > last) std::swap(first, last);

        bin_ranges_[b] = BinRange{ .first = first, .last = last };
    }
}

// -----------------------------------------------------------------------------
// Gain control
// -----------------------------------------------------------------------------

void Equalizer::set_gain(std::size_t band_index, float gain)
{
    if (band_index >= N_BANDS) {
        throw std::out_of_range(std::format(
            "Equalizer::set_gain: band_index {} out of range "
            "(N_BANDS = {})", band_index, N_BANDS));
    }
    gains_[band_index] = std::clamp(gain, MIN_GAIN, MAX_GAIN);
}

void Equalizer::set_gains(std::span<const float> gains)
{
    if (gains.size() != N_BANDS) {
        throw std::invalid_argument(std::format(
            "Equalizer::set_gains: expected {} gains, got {}",
            N_BANDS, gains.size()));
    }
    for (std::size_t b = 0; b < N_BANDS; ++b) {
        gains_[b] = std::clamp(gains[b], MIN_GAIN, MAX_GAIN);
    }
}

void Equalizer::reset()
{
    std::ranges::fill(gains_, UNITY_GAIN);
}

float Equalizer::gain(std::size_t band_index) const
{
    if (band_index >= N_BANDS) {
        throw std::out_of_range(std::format(
            "Equalizer::gain: band_index {} out of range "
            "(N_BANDS = {})", band_index, N_BANDS));
    }
    return gains_[band_index];
}

std::span<const float> Equalizer::gains() const noexcept
{
    return std::span<const float>(gains_);
}

// -----------------------------------------------------------------------------
// Processing
// -----------------------------------------------------------------------------

StftMatrix Equalizer::apply(const StftMatrix& matrix) const
{
    // Start with a copy of the input matrix.
    // We return a new matrix — the input is not modified.
    StftMatrix result = matrix;

    for (std::size_t b = 0; b < N_BANDS; ++b) {
        const float      gain  = gains_[b];
        const BinRange&  range = bin_ranges_[b];

        // Skip unity gain — multiplying by 1.0 is a no-op.
        // This is a micro-optimisation but also makes the intent clear:
        // only modified bands are touched.
        if (gain == UNITY_GAIN) continue;

        // Apply gain to every bin in this band, across all time frames.
        // Multiplying a complex number by a real scalar:
        //   (a + bi) * g = (a*g) + (b*g)i
        // This scales magnitude by g while leaving phase unchanged.
        for (std::size_t bin = range.first; bin <= range.last; ++bin) {
            for (std::size_t frame = 0; frame < matrix.n_frames(); ++frame) {
                result(bin, frame) *= gain;
            }
        }
    }

    return result;
}

// -----------------------------------------------------------------------------
// Description
// -----------------------------------------------------------------------------

std::string Equalizer::describe() const
{
    std::string out = "Equalizer gains:\n";

    for (std::size_t b = 0; b < N_BANDS; ++b) {
        const auto& band  = BANDS[b];
        const auto& range = bin_ranges_[b];
        const float g     = gains_[b];

        const std::string zone =
            g <= SAFE_MAX_GAIN ? "safe" : "DANGER";

        out += std::format(
            "  [{:d}] {:12s} {:5.0f}-{:5.0f} Hz  "
            "bins {:3d}-{:3d}  gain = {:.2f}  [{}]\n",
            b, std::string(band.name),
            band.low_hz, band.high_hz,
            range.first, range.last,
            g, zone
        );
    }
    return out;
}

} // namespace eq