// =============================================================================
// StftProcessor.hpp — Short-Time Fourier Transform using FFTW3.
//
// Design decisions worth understanding
// -------------------------------------
// 1. RAII wrapping of a C library
//    FFTW uses C-style resource management: fftw_malloc / fftw_free and
//    fftw_plan / fftw_destroy_plan. We wrap these in RAII types so that
//    resources are always released, even if an exception is thrown.
//    std::unique_ptr with a custom deleter is the standard tool for this.
//
// 2. std::complex<float>
//    The STFT output is a matrix of complex numbers. Each cell encodes
//    magnitude and phase as a single complex<float>. This is the C++
//    standard way to represent complex arithmetic — no separate real/imag
//    arrays needed.
//
// 3. Non-copyable, non-movable
//    FFTW plans contain raw pointers to aligned memory buffers. Moving them
//    safely is non-trivial. We delete both copy and move to keep ownership
//    unambiguous. The processor is created once and used in-place.
//
// 4. StftMatrix as a value type
//    The result of transform() is an StftMatrix — a plain data container
//    that IS copyable and movable. It owns its data and has no FFTW
//    dependency. This separation means the GUI can hold an StftMatrix
//    without knowing anything about FFTW.
//
// 5. Hann window
//    Applied to each frame before the FFT to reduce spectral leakage.
//    Pre-computed once at construction time and stored as std::vector<float>.
// =============================================================================

#pragma once

#include <complex>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <fftw3.h>

#include "AudioBuffer.hpp"

namespace eq {

// -----------------------------------------------------------------------------
// StftConfig — parameters controlling the STFT computation.
//
// Stored as a plain struct with no behaviour. All fields are const after
// construction, enforced by the const member variables.
// -----------------------------------------------------------------------------
struct StftConfig {
    std::uint32_t sample_rate  = 48'000; // Hz
    std::size_t   n_fft        = 960;    // FFT window size in samples
    std::size_t   hop_length   = 480;    // hop size in samples (50% overlap)

    // Derived properties — computed from the above.
    [[nodiscard]] std::size_t n_bins()     const noexcept { return n_fft / 2 + 1; }
    [[nodiscard]] double      hz_per_bin() const noexcept {
        return static_cast<double>(sample_rate) / static_cast<double>(n_fft);
    }
    [[nodiscard]] double frame_duration_ms() const noexcept {
        return (static_cast<double>(n_fft) / sample_rate) * 1000.0;
    }
    [[nodiscard]] double hop_duration_ms() const noexcept {
        return (static_cast<double>(hop_length) / sample_rate) * 1000.0;
    }
    [[nodiscard]] int bin_for_hz(double frequency_hz) const noexcept {
        return static_cast<int>(frequency_hz / hz_per_bin() + 0.5);
    }
    [[nodiscard]] double hz_for_bin(std::size_t bin) const noexcept {
        return static_cast<double>(bin) * hz_per_bin();
    }
};

// -----------------------------------------------------------------------------
// StftMatrix — owns the complex STFT output matrix.
//
// Layout: matrix[bin][frame] — row = frequency bin, column = time frame.
// This is IS copyable and movable — it has no FFTW dependency.
// It is a plain data container, not a processor.
// -----------------------------------------------------------------------------
class StftMatrix {
public:
    // Construct an empty matrix with the given dimensions.
    // All cells are initialised to zero.
    StftMatrix(std::size_t n_bins, std::size_t n_frames);

    // Default destructor, copy, and move — std::vector handles everything.
    ~StftMatrix()                              = default;
    StftMatrix(const StftMatrix&)              = default;
    StftMatrix& operator=(const StftMatrix&)   = default;
    StftMatrix(StftMatrix&&)                   noexcept = default;
    StftMatrix& operator=(StftMatrix&&)        noexcept = default;

    // -------------------------------------------------------------------------
    // Dimensions
    // -------------------------------------------------------------------------
    [[nodiscard]] std::size_t n_bins()   const noexcept { return n_bins_;   }
    [[nodiscard]] std::size_t n_frames() const noexcept { return n_frames_; }

    // -------------------------------------------------------------------------
    // Element access — checked with bounds assertion in debug builds.
    // matrix(bin, frame) returns a reference to the complex cell.
    // -------------------------------------------------------------------------
    [[nodiscard]] std::complex<float>&
    operator()(std::size_t bin, std::size_t frame) noexcept;

    [[nodiscard]] const std::complex<float>&
    operator()(std::size_t bin, std::size_t frame) const noexcept;

    // -------------------------------------------------------------------------
    // Derived views — computed on demand, no storage overhead.
    // -------------------------------------------------------------------------

    // Magnitude matrix: |complex| for each cell.
    // Returns a new matrix of float (not complex).
    [[nodiscard]] std::vector<float> magnitude() const;

    // Phase matrix: angle in radians for each cell.
    [[nodiscard]] std::vector<float> phase() const;

    // Magnitude in decibels, floored at -80 dB.
    [[nodiscard]] std::vector<float> magnitude_db() const;

    // Raw access to underlying storage (bin-major, contiguous).
    [[nodiscard]] std::span<std::complex<float>>       data() noexcept;
    [[nodiscard]] std::span<const std::complex<float>> data() const noexcept;

    // Human-readable description.
    [[nodiscard]] std::string describe() const;

private:
    std::size_t                    n_bins_;
    std::size_t                    n_frames_;
    std::vector<std::complex<float>> data_; // size = n_bins_ * n_frames_
};

// -----------------------------------------------------------------------------
// StftProcessor — computes forward and inverse STFT using FFTW3.
//
// NON-COPYABLE, NON-MOVABLE.
// Create once and keep alive for the duration of processing.
// -----------------------------------------------------------------------------
class StftProcessor {
public:
    // Construct with the given configuration.
    // Allocates FFTW buffers and creates plans. Throws std::runtime_error
    // if FFTW plan creation fails.
    explicit StftProcessor(StftConfig config = {});

    // Destructor releases all FFTW resources via RAII wrappers.
    ~StftProcessor();

    // Non-copyable, non-movable.
    StftProcessor(const StftProcessor&)            = delete;
    StftProcessor& operator=(const StftProcessor&) = delete;
    StftProcessor(StftProcessor&&)                 = delete;
    StftProcessor& operator=(StftProcessor&&)      = delete;

    // -------------------------------------------------------------------------
    // Core operations
    // -------------------------------------------------------------------------

    // Forward STFT: waveform -> complex frequency matrix.
    // The input buffer must be mono (n_channels == 1).
    // Throws std::invalid_argument if the buffer is stereo or empty.
    [[nodiscard]] StftMatrix transform(const AudioBuffer& buffer) const;

    // Inverse STFT: complex frequency matrix -> waveform.
    // Returns a mono AudioBuffer reconstructed from the matrix.
    [[nodiscard]] AudioBuffer reconstruct(const StftMatrix& matrix) const;

    // Configuration accessor.
    [[nodiscard]] const StftConfig& config() const noexcept { return config_; }

private:
    // -------------------------------------------------------------------------
    // RAII wrappers for FFTW C resources
    //
    // fftwf_plan is an opaque C pointer. We wrap it in unique_ptr with a
    // custom deleter so it is automatically destroyed when StftProcessor
    // goes out of scope — even if the constructor throws halfway through.
    // -------------------------------------------------------------------------
    struct FftwPlanDeleter {
        void operator()(fftwf_plan p) const noexcept {
            if (p) fftwf_destroy_plan(p);
        }
    };
    using FftwPlanPtr = std::unique_ptr<fftwf_plan_s, FftwPlanDeleter>;

    struct FftwBufferDeleter {
        void operator()(float* p) const noexcept {
            if (p) fftwf_free(p);
        }
    };
    using FftwBufferPtr = std::unique_ptr<float, FftwBufferDeleter>;

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    // Apply Hann window to a frame of samples in-place.
    void apply_window(std::span<float> frame) const noexcept;

    // Compute the number of STFT frames for a given number of samples.
    [[nodiscard]] std::size_t
    n_frames_for(std::size_t n_samples) const noexcept;

    // -------------------------------------------------------------------------
    // Member data
    // -------------------------------------------------------------------------
    StftConfig     config_;
    std::vector<float> window_;      // pre-computed Hann window, size = n_fft

    // FFTW aligned buffers — must use fftwf_malloc/free for SIMD alignment.
    FftwBufferPtr  fft_in_;          // real input,    size = n_fft
    FftwBufferPtr  fft_out_raw_;     // complex output (raw float pairs)

    // FFTW plans — created once, reused for every frame.
    FftwPlanPtr    forward_plan_;    // real -> complex (R2C)
    FftwPlanPtr    inverse_plan_;    // complex -> real (C2R)

    // Typed view of fft_out_raw_ as complex<float>* for convenient access.
    fftwf_complex* fft_out_;         // non-owning alias into fft_out_raw_
};

} // namespace eq
