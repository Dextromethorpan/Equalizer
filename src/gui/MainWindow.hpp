// =============================================================================
// MainWindow.hpp — Top-level application window.
//
// Owns the full audio pipeline:
//   AudioBuffer -> StftProcessor -> Equalizer -> StftMatrix (modified)
//
// Two sliders both modify the selected region's audio:
//   Gain slider       — coarse control, 0.0 to 3.0
//   Brightness slider — fine control,   0.5 to 1.5
//
// The display range of the spectrogram is always computed from the
// modified matrix — so both sliders give immediate visual feedback.
// =============================================================================

#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "audio/AudioBuffer.hpp"
#include "audio/Equalizer.hpp"
#include "audio/StftProcessor.hpp"
#include "gui/SpectrogramWidget.hpp"

namespace eq {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override { stopPlayback(); }

private slots:
    void onLoadFile();
    void onPlayOriginal();
    void onPlayModified();
    void onReset();
    void onSelectionChanged(eq::SelectionRegion region);
    void onSelectionCleared();
    void onGainSliderChanged(int value);
    void onBrightnessSliderChanged(int value);

private:
    // -------------------------------------------------------------------------
    // UI setup
    // -------------------------------------------------------------------------
    void setupUi();
    void setupMenuBar();
    void updateSelectionLabel();
    void updateSliderLabels();
    void setControlsEnabled(bool enabled);

    // -------------------------------------------------------------------------
    // Audio pipeline
    // -------------------------------------------------------------------------
    void processAudio();
    void applyToSelection();
    void stopPlayback();
    void playBufferOnThread(const float*    samples,
                            std::size_t     n_samples,
                            std::uint32_t   sample_rate,
                            std::stop_token stop_token);

    // -------------------------------------------------------------------------
    // Widgets
    // -------------------------------------------------------------------------
    SpectrogramWidget* original_widget_   = nullptr;
    SpectrogramWidget* modified_widget_   = nullptr;

    // Gain slider — coarse (0.0 to 3.0)
    QSlider*     gain_slider_             = nullptr;
    QLabel*      gain_label_             = nullptr;

    // Brightness slider — fine (0.5 to 1.5)
    QSlider*     brightness_slider_       = nullptr;
    QLabel*      brightness_label_        = nullptr;

    QLabel*      selection_label_         = nullptr;
    QPushButton* load_button_             = nullptr;
    QPushButton* play_orig_button_        = nullptr;
    QPushButton* play_mod_button_         = nullptr;
    QPushButton* reset_button_            = nullptr;

    // -------------------------------------------------------------------------
    // Audio pipeline state
    // -------------------------------------------------------------------------
    StftConfig                     config_;
    std::unique_ptr<StftProcessor> processor_;
    std::unique_ptr<Equalizer>     equalizer_;
    std::unique_ptr<AudioBuffer>   original_buffer_;
    std::unique_ptr<StftMatrix>    original_matrix_;
    std::unique_ptr<StftMatrix>    modified_matrix_;

    std::optional<SelectionRegion> current_selection_;
    QString                        loaded_file_path_;

    // -------------------------------------------------------------------------
    // Playback thread
    //
    // std::jthread automatically joins on destruction — no manual cleanup.
    // stop_playback_ is an atomic flag the audio thread checks each callback.
    // Using atomic<bool> instead of a mutex because the audio thread must
    // never block — reading an atomic is lock-free and real-time safe.
    // -------------------------------------------------------------------------
    std::jthread            playback_thread_;
    std::atomic<bool>       stop_playback_{ false };
};

} // namespace eq