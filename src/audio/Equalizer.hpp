// =============================================================================
// Equalizer.hpp — Applies per-band gains to an STFT magnitude matrix.
//
// Design decisions worth understanding
// -------------------------------------
// 1. constexpr frequency band definitions
//    The frequency bands are defined at compile time using constexpr.
//    This means the compiler computes the band boundaries once — there
//    is zero runtime cost and the values are embedded directly in the
//    binary. constexpr is the C++20 replacement for #define constants.
//
// 2. std::array instead of std::vector for fixed-size data
//    The number of bands is known at compile time. std::array<T, N>
//    lives on the stack, has zero heap allocation, and its size is
//    part of its type — the compiler catches size mismatches.
//    std::vector would be correct only if the number of bands were
//    determined at runtime.
//
// 3. Gain clamping — safe vs unsafe zone
//    Gains in [0.0, 1.0] are safe: they only suppress existing energy.
//    Gains above 1.0 boost energy and can introduce artifacts if the
//    reconstructed signal clips. We clamp to [0.0, MAX_GAIN] and expose
//    the boundary so the GUI can draw the safe/unsafe line.
//
// 4. Separation of concerns
//    Equalizer knows nothing about FFTW, AudioBuffer, or Qt.
//    It takes an StftMatrix, returns a modified StftMatrix.
//    The caller (main or the GUI) owns the full pipeline.
//
// 5. Value semantics for the result
//    apply() returns a new StftMatrix rather than modifying in place.
//    This makes the data flow explicit and enables the GUI to keep
//    both the original and modified matrices for comparison display.
// =============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "StftProcessor.hpp"

namespace eq {

// -----------------------------------------------------------------------------
// FrequencyBand — defines one named band of the equalizer.
//
// A plain aggregate struct — no constructor needed, initialised with
// designated initialisers (C++20): FrequencyBand{ .name="Bass", ... }
// -----------------------------------------------------------------------------
struct FrequencyBand {
    std::string_view name;        // display name for the GUI
    double           low_hz;      // lower frequency boundary (Hz)
    double           high_hz;     // upper frequency boundary (Hz)
};

// -----------------------------------------------------------------------------
// Equalizer — 7-band graphic equalizer operating on STFT matrices.
//
// The 7 bands cover the full audible spectrum from 20 Hz to 20 kHz.
// Each band maps to a contiguous range of STFT frequency bins.
//
// Band layout (standard graphic EQ):
//   0  Sub-bass   20  -  80  Hz   — rumble, kick drum body
//   1  Bass       80  -  250 Hz   — warmth, bass guitar
//   2  Low-mid    250 -  500 Hz   — muddiness or fullness
//   3  Mid        500 - 2000 Hz   — vocals, guitar presence
//   4  High-mid  2000 - 4000 Hz   — clarity, attack
//   5  Presence  4000 - 8000 Hz   — brightness, consonants
//   6  Brilliance 8000 - 20000 Hz — air, shimmer
// -----------------------------------------------------------------------------
class Equalizer {
public:
    // Number of bands — fixed at compile time.
    static constexpr std::size_t N_BANDS = 7;

    // Gain boundaries.
    static constexpr float MIN_GAIN     = 0.0f;  // silence
    static constexpr float UNITY_GAIN   = 1.0f;  // no change
    static constexpr float MAX_GAIN     = 3.0f;  // maximum boost (danger zone)
    static constexpr float SAFE_MAX_GAIN = 1.0f; // safe zone upper limit

    // The 7 frequency bands — constexpr, computed at compile time.
    static constexpr std::array<FrequencyBand, N_BANDS> BANDS = {{
        { .name = "Sub-bass",  .low_hz =    20.0, .high_hz =    80.0 },
        { .name = "Bass",      .low_hz =    80.0, .high_hz =   250.0 },
        { .name = "Low-mid",   .low_hz =   250.0, .high_hz =   500.0 },
        { .name = "Mid",       .low_hz =   500.0, .high_hz =  2000.0 },
        { .name = "High-mid",  .low_hz =  2000.0, .high_hz =  4000.0 },
        { .name = "Presence",  .low_hz =  4000.0, .high_hz =  8000.0 },
        { .name = "Brilliance",.low_hz =  8000.0, .high_hz = 20000.0 },
    }};

    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    // Construct with a StftConfig so the equalizer knows how to map
    // frequency bands to STFT bin indices.
    // All gains are initialised to UNITY_GAIN (no change).
    explicit Equalizer(const StftConfig& config);

    ~Equalizer()                           = default;
    Equalizer(const Equalizer&)            = default;
    Equalizer& operator=(const Equalizer&) = default;
    Equalizer(Equalizer&&)                 noexcept = default;
    Equalizer& operator=(Equalizer&&)      noexcept = default;

    // -------------------------------------------------------------------------
    // Gain control
    // -------------------------------------------------------------------------

    // Set the gain for a single band.
    // Gain is clamped to [MIN_GAIN, MAX_GAIN] automatically.
    // band_index must be in [0, N_BANDS).
    // Throws std::out_of_range if band_index >= N_BANDS.
    void set_gain(std::size_t band_index, float gain);

    // Set all gains at once from a span of N_BANDS values.
    // Throws std::invalid_argument if gains.size() != N_BANDS.
    void set_gains(std::span<const float> gains);

    // Reset all gains to UNITY_GAIN.
    void reset();

    // Get the current gain for a band.
    // Throws std::out_of_range if band_index >= N_BANDS.
    [[nodiscard]] float gain(std::size_t band_index) const;

    // Get all current gains as a read-only span.
    [[nodiscard]] std::span<const float> gains() const noexcept;

    // -------------------------------------------------------------------------
    // Processing
    // -------------------------------------------------------------------------

    // Apply the current gains to the STFT matrix.
    // Returns a new StftMatrix with the gains applied.
    // The input matrix is not modified.
    //
    // For each band:
    //   - Find the STFT bins corresponding to [low_hz, high_hz]
    //   - Multiply all complex cells in those rows by the band gain
    //
    // Multiplying a complex number by a real scalar scales its magnitude
    // while leaving the phase unchanged. This is the key property that
    // makes magnitude-only EQ reconstruction clean.
    [[nodiscard]] StftMatrix apply(const StftMatrix& matrix) const;

    // Human-readable description of current band gains.
    [[nodiscard]] std::string describe() const;

private:
    // -------------------------------------------------------------------------
    // Band-to-bin mapping
    //
    // Precomputed at construction time from the StftConfig.
    // For each band, we store the first and one-past-last bin index.
    // This avoids recomputing bin boundaries on every apply() call.
    // -------------------------------------------------------------------------
    struct BinRange {
        std::size_t first; // first bin in this band (inclusive)
        std::size_t last;  // last bin in this band (inclusive)
    };

    StftConfig                        config_;
    std::array<float, N_BANDS>        gains_;     // current gain per band
    std::array<BinRange, N_BANDS>     bin_ranges_;// precomputed bin ranges
};

} // namespace eq