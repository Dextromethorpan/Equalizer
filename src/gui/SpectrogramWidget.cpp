// =============================================================================
// SpectrogramWidget.cpp — Spectrogram rendering and mouse interaction.
//
// Key Qt concepts demonstrated
// -----------------------------
// - paintEvent: called by Qt whenever the widget needs to be redrawn.
//   Never call it directly — use update() to request a repaint.
// - QPainter: the Qt drawing API. Must be constructed inside paintEvent.
// - QImage: an in-memory pixel buffer. We write to it directly using
//   setPixel() then draw it scaled to the widget size in paintEvent.
// - Mouse events: mousePressEvent, mouseMoveEvent, mouseReleaseEvent.
//   We track drag start/end and emit a signal on release.
// - emit: Qt keyword that triggers a signal, notifying all connected slots.
// =============================================================================

#include "SpectrogramWidget.hpp"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QMouseEvent>
#include <QResizeEvent>

namespace eq {

// =============================================================================
// Inferno colour map
// Pre-computed control points for the inferno palette.
// Each entry is {r, g, b} for a value in [0, 1].
// =============================================================================
static constexpr int INFERNO_SIZE = 16;
static constexpr float INFERNO_R[INFERNO_SIZE] = {
    0.000f, 0.064f, 0.173f, 0.302f,
    0.431f, 0.553f, 0.659f, 0.749f,
    0.824f, 0.887f, 0.936f, 0.970f,
    0.990f, 0.998f, 0.993f, 0.988f
};
static constexpr float INFERNO_G[INFERNO_SIZE] = {
    0.000f, 0.027f, 0.047f, 0.063f,
    0.082f, 0.118f, 0.184f, 0.271f,
    0.369f, 0.467f, 0.560f, 0.640f,
    0.712f, 0.782f, 0.855f, 0.998f
};
static constexpr float INFERNO_B[INFERNO_SIZE] = {
    0.014f, 0.212f, 0.376f, 0.451f,
    0.439f, 0.376f, 0.290f, 0.212f,
    0.153f, 0.117f, 0.100f, 0.098f,
    0.102f, 0.106f, 0.102f, 0.644f
};

// =============================================================================
// Construction
// =============================================================================

SpectrogramWidget::SpectrogramWidget(Mode mode, QWidget* parent)
    : QWidget(parent)
    , mode_(mode)
{
    // Accept mouse tracking even when no button is pressed.
    // Needed for live drag feedback.
    if (mode_ == Mode::Editable) {
        setMouseTracking(true);
    }

    // Dark background — matches the inferno colour map's darkest colour.
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(6, 12, 16));
    setPalette(pal);

    // Minimum size so the widget is always usable.
    setMinimumSize(300, 200);
}

// =============================================================================
// Data
// =============================================================================

void SpectrogramWidget::setMatrix(const StftMatrix& matrix,
                                   const StftConfig& config)
{
    matrix_ = &matrix;
    config_ = config;
    renderImage();
    update(); // request a repaint
}

void SpectrogramWidget::clearMatrix()
{
    matrix_ = nullptr;
    image_  = QImage();
    update();
}

// =============================================================================
// Display controls
// =============================================================================

void SpectrogramWidget::setBrightness(double brightness)
{
    brightness_ = std::clamp(brightness, 0.1, 5.0);
    if (matrix_) {
        renderImage();
        update();
    }
}

void SpectrogramWidget::clearSelection()
{
    selection_.reset();
    dragging_ = false;
    update();
    emit selectionCleared();
}

// =============================================================================
// Rendering
// =============================================================================

void SpectrogramWidget::renderImage()
{
    if (!matrix_) return;

    const std::size_t n_bins   = matrix_->n_bins();
    const std::size_t n_frames = matrix_->n_frames();

    // Create image: width = n_frames, height = n_bins.
    // Each pixel = one STFT cell.
    // We flip vertically so low frequencies are at the bottom.
    image_ = QImage(
        static_cast<int>(n_frames),
        static_cast<int>(n_bins),
        QImage::Format_RGB32
    );

    // Compute magnitude_db for the whole matrix and find the range.
    const std::vector<float> mag_db = matrix_->magnitude_db();

    // Find peak dB — used to set the display range.
    float peak_db = *std::max_element(mag_db.begin(), mag_db.end());

    // Dynamic range controlled by brightness.
    // Higher brightness → smaller range → more detail in quiet regions.
    const float dynamic_range = static_cast<float>(80.0 / brightness_);
    const float vmax = peak_db;
    const float vmin = peak_db - dynamic_range;

    // Write pixels.
    // Matrix layout: data_[bin * n_frames + frame]
    // Image layout:  pixel(x=frame, y=flipped_bin)
    for (std::size_t bin = 0; bin < n_bins; ++bin) {
        // Flip: bin 0 (DC) at bottom, bin n_bins-1 (Nyquist) at top.
        const int y = static_cast<int>(n_bins - 1 - bin);

        for (std::size_t frame = 0; frame < n_frames; ++frame) {
            const int   x     = static_cast<int>(frame);
            const float db    = mag_db[bin * n_frames + frame];
            const QRgb  colour = infernoColour(db, vmin, vmax);
            image_.setPixel(x, y, colour);
        }
    }
}

QRgb SpectrogramWidget::infernoColour(float value_db,
                                       float vmin,
                                       float vmax) noexcept
{
    // Normalise value to [0, 1].
    float t = (value_db - vmin) / (vmax - vmin);
    t = std::clamp(t, 0.0f, 1.0f);

    // Interpolate between the nearest two control points.
    const float idx_f  = t * static_cast<float>(INFERNO_SIZE - 1);
    const int   idx_lo = static_cast<int>(idx_f);
    const int   idx_hi = std::min(idx_lo + 1, INFERNO_SIZE - 1);
    const float frac   = idx_f - static_cast<float>(idx_lo);

    const float r = INFERNO_R[idx_lo] + frac * (INFERNO_R[idx_hi] - INFERNO_R[idx_lo]);
    const float g = INFERNO_G[idx_lo] + frac * (INFERNO_G[idx_hi] - INFERNO_G[idx_lo]);
    const float b = INFERNO_B[idx_lo] + frac * (INFERNO_B[idx_hi] - INFERNO_B[idx_lo]);

    return qRgb(
        static_cast<int>(r * 255.0f),
        static_cast<int>(g * 255.0f),
        static_cast<int>(b * 255.0f)
    );
}

// =============================================================================
// Qt event handlers
// =============================================================================

void SpectrogramWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    if (!image_.isNull()) {
        painter.drawImage(rect(), image_);
    } else {
        painter.fillRect(rect(), QColor(6, 12, 16));
        painter.setPen(QColor(42, 74, 90));
        painter.drawText(rect(), Qt::AlignCenter, "No audio loaded");
    }

    // Draw selection rectangle on top.
    if (mode_ == Mode::Editable) {
        drawSelection(painter);
    }

    // Draw art deco corner brackets.
    const int W = width();
    const int H = height();
    const int L = 12;   // bracket arm length
    const int M = 5;    // margin from edge

    painter.setPen(QPen(QColor(106, 180, 200), 1));

    // Top-left
    painter.drawLine(M, M, M + L, M);
    painter.drawLine(M, M, M, M + L);

    // Top-right
    painter.drawLine(W - M, M, W - M - L, M);
    painter.drawLine(W - M, M, W - M, M + L);

    // Bottom-left
    painter.drawLine(M, H - M, M + L, H - M);
    painter.drawLine(M, H - M, M, H - M - L);

    // Bottom-right
    painter.drawLine(W - M, H - M, W - M - L, H - M);
    painter.drawLine(W - M, H - M, W - M, H - M - L);

    // Frequency axis labels along the bottom edge.
    painter.setPen(QColor(42, 74, 90));
    QFont f = painter.font();
    f.setPointSize(7);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1);
    painter.setFont(f);

    const QStringList freq_labels = {"20", "250", "1k", "4k", "20k"};
    for (int i = 0; i < freq_labels.size(); ++i) {
        const int x = M + static_cast<int>(
            static_cast<float>(i) / (freq_labels.size() - 1)
            * static_cast<float>(W - 2 * M));
        painter.drawText(x - 8, H - M - 2, 20, 10,
                         Qt::AlignCenter, freq_labels[i]);
    }
}

void SpectrogramWidget::mousePressEvent(QMouseEvent* event)
{
    if (mode_ != Mode::Editable) return;
    if (event->button() != Qt::LeftButton) return;

    dragging_    = true;
    drag_start_  = event->pos();
    drag_current_= event->pos();
    selection_.reset();
    update();
}

void SpectrogramWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (mode_ != Mode::Editable) return;
    if (!dragging_) return;

    drag_current_ = event->pos();
    update(); // repaint to show live rectangle
}

void SpectrogramWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (mode_ != Mode::Editable) return;
    if (event->button() != Qt::LeftButton) return;
    if (!dragging_) return;

    dragging_     = false;
    drag_current_ = event->pos();

    // Convert pixel coordinates to matrix cell indices.
    std::size_t frame_start, bin_start, frame_end, bin_end;

    const bool start_ok = pixelToCell(drag_start_,   frame_start, bin_start);
    const bool end_ok   = pixelToCell(drag_current_, frame_end,   bin_end);

    if (start_ok && end_ok && matrix_) {
        // Normalise so start <= end.
        if (frame_start > frame_end) std::swap(frame_start, frame_end);
        if (bin_start   > bin_end)   std::swap(bin_start,   bin_end);

        selection_ = SelectionRegion{
            .bin_start   = bin_start,
            .bin_end     = bin_end,
            .frame_start = frame_start,
            .frame_end   = frame_end,
        };

        emit selectionChanged(*selection_);
    }

    update();
}

void SpectrogramWidget::resizeEvent(QResizeEvent* /*event*/)
{
    // Widget resized — no need to re-render the image,
    // paintEvent will scale it to the new size automatically.
    update();
}

// =============================================================================
// Coordinate conversion
// =============================================================================

bool SpectrogramWidget::pixelToCell(const QPoint& pos,
                                     std::size_t& frame,
                                     std::size_t& bin) const noexcept
{
    if (!matrix_ || width() == 0 || height() == 0) return false;

    const std::size_t n_frames = matrix_->n_frames();
    const std::size_t n_bins   = matrix_->n_bins();

    // Map pixel x to frame index.
    const double frame_f = static_cast<double>(pos.x())
                         / static_cast<double>(width())
                         * static_cast<double>(n_frames);

    // Map pixel y to bin index (flipped — y=0 is top = high frequency).
    const double bin_f = static_cast<double>(height() - pos.y())
                       / static_cast<double>(height())
                       * static_cast<double>(n_bins);

    frame = static_cast<std::size_t>(std::clamp(
        static_cast<int>(frame_f), 0, static_cast<int>(n_frames - 1)));

    bin = static_cast<std::size_t>(std::clamp(
        static_cast<int>(bin_f), 0, static_cast<int>(n_bins - 1)));

    return true;
}

QPoint SpectrogramWidget::cellToPixel(std::size_t frame,
                                       std::size_t bin) const noexcept
{
    if (!matrix_ || matrix_->n_frames() == 0 || matrix_->n_bins() == 0) {
        return QPoint(0, 0);
    }

    const int x = static_cast<int>(
        static_cast<double>(frame) / static_cast<double>(matrix_->n_frames())
        * static_cast<double>(width()));

    // Flip y — bin 0 is at the bottom.
    const int y = static_cast<int>(
        (1.0 - static_cast<double>(bin) / static_cast<double>(matrix_->n_bins()))
        * static_cast<double>(height()));

    return QPoint(x, y);
}

void SpectrogramWidget::drawSelection(QPainter& painter) const
{
    // Draw live drag rectangle.
    if (dragging_) {
        const QRect rect = QRect(drag_start_, drag_current_).normalized();
        painter.setPen(QPen(Qt::white, 1, Qt::SolidLine));
        painter.setBrush(QBrush(QColor(255, 255, 255, 30))); // semi-transparent fill
        painter.drawRect(rect);
    }
    // Draw completed selection rectangle.
    else if (selection_.has_value()) {
        const QPoint p1 = cellToPixel(selection_->frame_start, selection_->bin_start);
        const QPoint p2 = cellToPixel(selection_->frame_end,   selection_->bin_end);
        const QRect  rect = QRect(p1, p2).normalized();
        painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
        painter.setBrush(QBrush(QColor(255, 255, 255, 20)));
        painter.drawRect(rect);
    }
}

} // namespace eq