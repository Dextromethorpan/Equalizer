// =============================================================================
// StftProcessor.cpp — STFT implementation using FFTW3.
//
// Key concepts demonstrated
// --------------------------
// - Custom deleters with unique_ptr to wrap a C library's resources
// - fftwf_ prefix: single-precision (float) FFTW. fftw_ would be double.
// - FFTW_ESTIMATE vs FFTW_MEASURE: ESTIMATE picks a reasonable plan without
//   benchmarking. MEASURE times several algorithms and picks the fastest —
//   better throughput but slower startup. We use ESTIMATE for simplicity.
// - Overlap-add reconstruction: each output frame is windowed and added
//   to overlapping regions of the output buffer to reconstruct the signal.
// =============================================================================

#include "StftProcessor.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <numbers>  // std::numbers::pi_v
#include <numeric>
#include <stdexcept>

namespace eq {

// =============================================================================
// StftMatrix
// =============================================================================

StftMatrix::StftMatrix(std::size_t n_bins, std::size_t n_frames)
    : n_bins_(n_bins)
    , n_frames_(n_frames)
    , data_(n_bins * n_frames, std::complex<float>{0.0f, 0.0f})
{
}

std::complex<float>&
StftMatrix::operator()(std::size_t bin, std::size_t frame) noexcept
{
    assert(bin < n_bins_ && "StftMatrix: bin index out of range");
    assert(frame < n_frames_ && "StftMatrix: frame index out of range");
    return data_[bin * n_frames_ + frame];
}

const std::complex<float>&
StftMatrix::operator()(std::size_t bin, std::size_t frame) const noexcept
{
    assert(bin < n_bins_ && "StftMatrix: bin index out of range");
    assert(frame < n_frames_ && "StftMatrix: frame index out of range");
    return data_[bin * n_frames_ + frame];
}

std::vector<float> StftMatrix::magnitude() const
{
    std::vector<float> result(data_.size());
    std::ranges::transform(data_, result.begin(),
        [](const std::complex<float>& c) { return std::abs(c); });
    return result;
}

std::vector<float> StftMatrix::phase() const
{
    std::vector<float> result(data_.size());
    std::ranges::transform(data_, result.begin(),
        [](const std::complex<float>& c) { return std::arg(c); });
    return result;
}

std::vector<float> StftMatrix::magnitude_db() const
{
    std::vector<float> result(data_.size());
    std::ranges::transform(data_, result.begin(),
        [](const std::complex<float>& c) {
            const float mag = std::max(std::abs(c), 1e-8f);
            return 20.0f * std::log10(mag);
        });
    return result;
}

std::span<std::complex<float>> StftMatrix::data() noexcept
{
    return std::span<std::complex<float>>(data_);
}

std::span<const std::complex<float>> StftMatrix::data() const noexcept
{
    return std::span<const std::complex<float>>(data_);
}

std::string StftMatrix::describe() const
{
    return std::format(
        "StftMatrix(bins={}, frames={}, cells={}, size={:.2f} KB)",
        n_bins_, n_frames_, n_bins_ * n_frames_,
        static_cast<double>(n_bins_ * n_frames_ * sizeof(std::complex<float>))
        / 1024.0
    );
}

// =============================================================================
// StftProcessor
// =============================================================================

StftProcessor::StftProcessor(StftConfig config)
    : config_(config)
    , window_(config.n_fft)
    , fft_in_(static_cast<float*>(
          fftwf_malloc(sizeof(float) * config.n_fft)))
    , fft_out_raw_(nullptr)
    , forward_plan_(nullptr)
    , inverse_plan_(nullptr)
    , fft_out_(nullptr)
{
    // -------------------------------------------------------------------------
    // Validate configuration
    // -------------------------------------------------------------------------
    if (config_.n_fft == 0) {
        throw std::invalid_argument("StftProcessor: n_fft must be > 0");
    }
    if (config_.hop_length == 0 || config_.hop_length > config_.n_fft) {
        throw std::invalid_argument(
            "StftProcessor: hop_length must be in (0, n_fft]");
    }
    if (!fft_in_) {
        throw std::runtime_error(
            "StftProcessor: fftwf_malloc failed for fft_in_");
    }

    // -------------------------------------------------------------------------
    // Allocate output buffer for complex spectrum.
    // FFTW R2C produces n_fft/2+1 complex values (n_bins).
    // fftwf_complex is float[2]: [real, imag].
    // We allocate as raw float* and alias as fftwf_complex*.
    // -------------------------------------------------------------------------
    const std::size_t n_complex_floats = config_.n_bins() * 2;
    fft_out_raw_.reset(
        static_cast<float*>(fftwf_malloc(sizeof(float) * n_complex_floats)));

    if (!fft_out_raw_) {
        throw std::runtime_error(
            "StftProcessor: fftwf_malloc failed for fft_out_raw_");
    }

    // Non-owning alias — fft_out_ points into fft_out_raw_'s memory.
    fft_out_ = reinterpret_cast<fftwf_complex*>(fft_out_raw_.get());

    // -------------------------------------------------------------------------
    // Create FFTW plans.
    // Plans must be created before any calls to fftwf_execute.
    // FFTW_ESTIMATE: choose a plan without benchmarking (fast startup).
    // -------------------------------------------------------------------------
    forward_plan_.reset(fftwf_plan_dft_r2c_1d(
        static_cast<int>(config_.n_fft),
        fft_in_.get(),
        fft_out_,
        FFTW_ESTIMATE
    ));

    if (!forward_plan_) {
        throw std::runtime_error(
            "StftProcessor: fftwf_plan_dft_r2c_1d failed");
    }

    inverse_plan_.reset(fftwf_plan_dft_c2r_1d(
        static_cast<int>(config_.n_fft),
        fft_out_,
        fft_in_.get(),
        FFTW_ESTIMATE
    ));

    if (!inverse_plan_) {
        throw std::runtime_error(
            "StftProcessor: fftwf_plan_dft_c2r_1d failed");
    }

    // -------------------------------------------------------------------------
    // Pre-compute Hann window.
    // w[n] = 0.5 * (1 - cos(2*pi*n / (N-1)))
    // Applied to each frame before FFT to reduce spectral leakage.
    // -------------------------------------------------------------------------
    const double two_pi = 2.0 * std::numbers::pi_v<double>;
    const double N      = static_cast<double>(config_.n_fft - 1);

    for (std::size_t n = 0; n < config_.n_fft; ++n) {
        window_[n] = static_cast<float>(
            0.5 * (1.0 - std::cos(two_pi * static_cast<double>(n) / N))
        );
    }
}

StftProcessor::~StftProcessor() = default;
// unique_ptr members clean up automatically in declaration order (reverse):
// inverse_plan_ -> forward_plan_ -> fft_out_raw_ -> fft_in_

// -----------------------------------------------------------------------------
// transform: AudioBuffer -> StftMatrix
// -----------------------------------------------------------------------------
StftMatrix StftProcessor::transform(const AudioBuffer& buffer) const
{
    if (buffer.n_channels() != 1) {
        throw std::invalid_argument(std::format(
            "StftProcessor::transform: expected mono buffer, "
            "got {} channels", buffer.n_channels()));
    }
    if (buffer.empty()) {
        throw std::invalid_argument(
            "StftProcessor::transform: buffer is empty");
    }

    const auto raw_samples = buffer.samples();

    // Zero-pad the signal at both ends by n_fft/2 samples.
    // This gives edge frames enough overlapping neighbours to reconstruct
    // correctly. Without padding, the first and last ~n_fft samples have
    // insufficient overlap and reconstruct with large error.
    const std::size_t pad       = config_.n_fft / 2;
    const std::size_t padded_len = raw_samples.size() + 2 * pad;

    std::vector<float> padded(padded_len, 0.0f);
    std::copy(raw_samples.begin(), raw_samples.end(),
              padded.begin() + static_cast<std::ptrdiff_t>(pad));

    const std::span<const float> samples(padded);
    const std::size_t n_frames = n_frames_for(samples.size());
    const std::size_t n_bins   = config_.n_bins();

    StftMatrix matrix(n_bins, n_frames);

    // Temporary frame buffer — copy samples here before windowing.
    std::vector<float> frame(config_.n_fft, 0.0f);

    for (std::size_t f = 0; f < n_frames; ++f) {
        const std::size_t offset = f * config_.hop_length;

        // Copy samples into frame buffer, zero-padding if near the end.
        std::fill(frame.begin(), frame.end(), 0.0f);
        const std::size_t available = std::min(
            config_.n_fft, samples.size() - std::min(offset, samples.size()));

        std::copy_n(samples.data() + offset, available, frame.begin());

        // Apply Hann window in-place.
        apply_window(std::span<float>(frame));

        // Copy windowed frame into FFTW input buffer.
        std::copy(frame.begin(), frame.end(), fft_in_.get());

        // Execute forward FFT.
        fftwf_execute(forward_plan_.get());

        // Copy complex output into the matrix column for this frame.
        for (std::size_t bin = 0; bin < n_bins; ++bin) {
            matrix(bin, f) = std::complex<float>(
                fft_out_[bin][0],   // real part
                fft_out_[bin][1]    // imaginary part
            );
        }
    }

    return matrix;
}

// -----------------------------------------------------------------------------
// reconstruct: StftMatrix -> AudioBuffer (overlap-add)
// -----------------------------------------------------------------------------
AudioBuffer StftProcessor::reconstruct(const StftMatrix& matrix) const
{
    const std::size_t n_frames   = matrix.n_frames();
    const std::size_t n_bins     = matrix.n_bins();
    const std::size_t n_samples  =
        (n_frames - 1) * config_.hop_length + config_.n_fft;

    // Output buffer and normalisation accumulator.
    std::vector<float> output(n_samples, 0.0f);
    std::vector<float> norm(n_samples, 0.0f);

    for (std::size_t f = 0; f < n_frames; ++f) {
        // Copy matrix column into FFTW complex input.
        for (std::size_t bin = 0; bin < n_bins; ++bin) {
            const auto& c    = matrix(bin, f);
            fft_out_[bin][0] = c.real();
            fft_out_[bin][1] = c.imag();
        }

        // Execute inverse FFT. Output is in fft_in_.
        fftwf_execute(inverse_plan_.get());

        // FFTW inverse FFT output is not normalised — divide by n_fft.
        const float scale = 1.0f / static_cast<float>(config_.n_fft);
        const std::size_t offset = f * config_.hop_length;

        for (std::size_t i = 0; i < config_.n_fft; ++i) {
            output[offset + i] += fft_in_.get()[i] * scale * window_[i];
            norm[offset + i]   += window_[i] * window_[i];
        }
    }

    // Normalise by the window overlap sum to recover original amplitude.
    for (std::size_t i = 0; i < n_samples; ++i) {
        if (norm[i] > 1e-8f) {
            output[i] /= norm[i];
        }
    }

    // Trim the zero-padding added during transform() from both ends.
    const std::size_t pad = config_.n_fft / 2;
    const std::size_t trim_start = std::min(pad, output.size());
    const std::size_t trim_end   = output.size() > pad
                                   ? output.size() - pad : 0;

    std::vector<float> trimmed(
        output.begin() + static_cast<std::ptrdiff_t>(trim_start),
        output.begin() + static_cast<std::ptrdiff_t>(trim_end));

    return AudioBuffer(std::move(trimmed), 1, config_.sample_rate);
}

// -----------------------------------------------------------------------------
// Private helpers
// -----------------------------------------------------------------------------

void StftProcessor::apply_window(std::span<float> frame) const noexcept
{
    assert(frame.size() == config_.n_fft);
    for (std::size_t i = 0; i < frame.size(); ++i) {
        frame[i] *= window_[i];
    }
}

std::size_t StftProcessor::n_frames_for(std::size_t n_samples) const noexcept
{
    if (n_samples < config_.n_fft) return 1;
    return 1 + (n_samples - config_.n_fft) / config_.hop_length;
}

} // namespace eq