// =============================================================================
// SpectrogramWidget.hpp — Renders an STFT matrix as a pixelized spectrogram.
//
// Design decisions worth understanding
// -------------------------------------
// 1. QWidget subclass
//    All Qt custom widgets inherit from QWidget. We override paintEvent()
//    to draw the spectrogram, and mouse event handlers for selection.
//    Qt calls these virtual functions automatically — we never call them
//    directly.
//
// 2. Q_OBJECT macro
//    Required for any class that uses Qt signals and slots. It tells the
//    Meta-Object Compiler (MOC) to generate the plumbing code that makes
//    signals and slots work at runtime.
//
// 3. Signals and slots
//    Qt's mechanism for communication between objects without tight coupling.
//    SpectrogramWidget emits selectionChanged() when the user finishes
//    dragging. MainWindow connects to this signal and responds.
//    Neither class needs to know about the other's internals.
//
// 4. QImage for pixel manipulation
//    We render the spectrogram into a QImage (an in-memory pixel buffer),
//    then draw that image onto the widget in paintEvent(). This is faster
//    than drawing individual pixels directly — we update the QImage only
//    when the data changes, not on every paint event.
//
// 5. Mode enum
//    ReadOnly: left spectrogram — no mouse interaction, shows original.
//    Editable: right spectrogram — handles mouse events, shows modified.
// =============================================================================

#pragma once

#include <QWidget>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <optional>

#include "audio/StftProcessor.hpp"

namespace eq {

// -----------------------------------------------------------------------------
// SelectionRegion — the result of a mouse drag on the spectrogram.
// Stores which bins and frames the user selected.
// -----------------------------------------------------------------------------
struct SelectionRegion {
    std::size_t bin_start;    // lowest frequency bin selected
    std::size_t bin_end;      // highest frequency bin selected
    std::size_t frame_start;  // first time frame selected
    std::size_t frame_end;    // last time frame selected

    [[nodiscard]] double duration_seconds(const StftConfig& config) const {
        return static_cast<double>(frame_end - frame_start)
               * config.hop_duration_ms() / 1000.0;
    }

    [[nodiscard]] double low_hz(const StftConfig& config) const {
        return config.hz_for_bin(bin_start);
    }

    [[nodiscard]] double high_hz(const StftConfig& config) const {
        return config.hz_for_bin(bin_end);
    }
};

// -----------------------------------------------------------------------------
// SpectrogramWidget
// -----------------------------------------------------------------------------
class SpectrogramWidget : public QWidget {
    Q_OBJECT  // required for signals and slots

public:
    enum class Mode { ReadOnly, Editable };

    explicit SpectrogramWidget(Mode mode, QWidget* parent = nullptr);
    ~SpectrogramWidget() override = default;

    // -------------------------------------------------------------------------
    // Data
    // -------------------------------------------------------------------------

    // Set the STFT matrix to display. Triggers a re-render.
    void setMatrix(const StftMatrix& matrix, const StftConfig& config);

    // Clear the display.
    void clearMatrix();

    // -------------------------------------------------------------------------
    // Display controls
    // -------------------------------------------------------------------------

    // Brightness multiplier for the colour mapping.
    // Higher values make quiet regions more visible.
    // Range: 0.1 (very dark) to 5.0 (very bright).
    void setBrightness(double brightness);
    [[nodiscard]] double brightness() const noexcept { return brightness_; }

    // -------------------------------------------------------------------------
    // Selection
    // -------------------------------------------------------------------------

    // Returns the current selection, or std::nullopt if nothing is selected.
    [[nodiscard]] std::optional<SelectionRegion> selection() const noexcept {
        return selection_;
    }

    // Clear the current selection.
    void clearSelection();

signals:
    // Emitted when the user completes a drag selection (mouse release).
    // Only emitted in Editable mode.
    void selectionChanged(eq::SelectionRegion region);

    // Emitted when the selection is cleared.
    void selectionCleared();

protected:
    // -------------------------------------------------------------------------
    // Qt event overrides
    // -------------------------------------------------------------------------
    void paintEvent(QPaintEvent* event)         override;
    void mousePressEvent(QMouseEvent* event)    override;
    void mouseMoveEvent(QMouseEvent* event)     override;
    void mouseReleaseEvent(QMouseEvent* event)  override;
    void resizeEvent(QResizeEvent* event)       override;

private:
    // -------------------------------------------------------------------------
    // Rendering
    // -------------------------------------------------------------------------

    // Re-render the QImage from the current matrix and brightness.
    // Called when the matrix changes or brightness changes.
    void renderImage();

    // Map a magnitude_db value to an inferno colour (ARGB).
    // vmin/vmax define the display range.
    [[nodiscard]] static QRgb infernoColour(float value_db,
                                             float vmin,
                                             float vmax) noexcept;

    // Convert a pixel position to (frame, bin) indices.
    // Returns false if the position is outside the valid range.
    [[nodiscard]] bool pixelToCell(const QPoint& pos,
                                   std::size_t& frame,
                                   std::size_t& bin) const noexcept;

    // Convert a (frame, bin) cell to pixel position.
    [[nodiscard]] QPoint cellToPixel(std::size_t frame,
                                     std::size_t bin) const noexcept;

    // Draw the selection rectangle onto the widget (called from paintEvent).
    void drawSelection(QPainter& painter) const;

    // -------------------------------------------------------------------------
    // Member data
    // -------------------------------------------------------------------------
    Mode                           mode_;
    StftConfig                     config_;
    const StftMatrix*              matrix_  = nullptr; // non-owning view
    QImage                         image_;             // rendered pixels
    double                         brightness_ = 1.0;

    // Mouse drag state
    bool                           dragging_   = false;
    QPoint                         drag_start_;        // pixel where drag began
    QPoint                         drag_current_;      // current drag position

    // Completed selection
    std::optional<SelectionRegion> selection_;
};

} // namespace eq

// Make SelectionRegion usable in Qt signals across thread boundaries.
Q_DECLARE_METATYPE(eq::SelectionRegion)