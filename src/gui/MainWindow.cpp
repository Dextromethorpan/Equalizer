// =============================================================================
// MainWindow.cpp — Main window implementation.
//
// Both gain and brightness sliders modify the selected region's audio.
// The combined multiplier applied to each selected cell is:
//
//   final_gain = gain_slider_value * brightness_slider_value
//
// This lets you use gain for coarse shaping and brightness for fine
// visual tuning — you look at the spectrogram and adjust brightness
// until the region looks right, trusting that what looks right sounds right.
// =============================================================================

#define NOMINMAX
#include <windows.h>

#include "MainWindow.hpp"

#include <cmath>
#include <format>

#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QString>

#include <portaudio.h>
#include <sndfile.h>

namespace eq {

// =============================================================================
// Slider range constants
// =============================================================================
static constexpr int GAIN_SLIDER_MIN       =   0;  // 0.00
static constexpr int GAIN_SLIDER_MAX       = 300;  // 3.00
static constexpr int GAIN_SLIDER_DEFAULT   = 100;  // 1.00
static constexpr int GAIN_SLIDER_SAFE_MAX  = 100;  // 1.00

static constexpr int BRIGHT_SLIDER_MIN     =  50;  // 0.50
static constexpr int BRIGHT_SLIDER_MAX     = 150;  // 1.50
static constexpr int BRIGHT_SLIDER_DEFAULT = 100;  // 1.00

// =============================================================================
// Construction
// =============================================================================

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , config_{ .sample_rate = 48'000 }
{
    setWindowTitle("Equalizer");
    setMinimumSize(1000, 700);

    setStyleSheet(R"(
        QMainWindow, QWidget {
            background-color: #090e12;
            color: #6ab4c8;
            font-family: 'Segoe UI';
            font-size: 12px;
            letter-spacing: 1px;
        }
        QPushButton {
            background-color: #060c10;
            color: #6ab4c8;
            border: 1px solid #1a2a35;
            border-radius: 0px;
            padding: 8px 14px;
            min-width: 100px;
            letter-spacing: 2px;
            text-transform: uppercase;
            font-size: 10px;
        }
        QPushButton:hover   { background-color: #0d1e28; border-color: #6ab4c8; }
        QPushButton:pressed { background-color: #6ab4c8; color: #090e12; }
        QPushButton:disabled{ color: #1a2a35; border-color: #0d1820; }
        QSlider::groove:horizontal {
            height: 2px;
            background: #0d1e28;
            border-radius: 0px;
        }
        QSlider::handle:horizontal {
            background: #090e12;
            border: 1px solid #6ab4c8;
            width: 12px;
            height: 12px;
            margin: -5px 0;
            border-radius: 6px;
        }
        QSlider::sub-page:horizontal {
            background: #6ab4c8;
            border-radius: 0px;
        }
        QLabel { color: #6ab4c8; }
        QGroupBox {
            border: 1px solid #1a2a35;
            border-radius: 0px;
            margin-top: 10px;
            padding-top: 10px;
            font-size: 9px;
            letter-spacing: 3px;
            text-transform: uppercase;
            font-weight: normal;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            color: #2a4a5a;
        }
        QMenuBar {
            background-color: #060c10;
            color: #6ab4c8;
            border-bottom: 1px solid #1a2a35;
            letter-spacing: 2px;
            font-size: 11px;
        }
        QMenuBar::item:selected { background-color: #0d1e28; }
        QMenu {
            background-color: #060c10;
            border: 1px solid #1a2a35;
            color: #6ab4c8;
        }
        QMenu::item:selected { background-color: #0d1e28; }
        QStatusBar {
            background-color: #060c10;
            color: #2a4a5a;
            border-top: 1px solid #1a2a35;
            letter-spacing: 1px;
            font-size: 10px;
        }
    )");

    setupUi();
    setupMenuBar();
    setControlsEnabled(false);
}

// =============================================================================
// UI Setup
// =============================================================================

void MainWindow::setupUi()
{
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout* root = new QVBoxLayout(central);
    root->setSpacing(8);
    root->setContentsMargins(12, 12, 12, 12);

    // -------------------------------------------------------------------------
    // Title bar
    // -------------------------------------------------------------------------
    QLabel* title = new QLabel("SPECTRAL  ◆  EQUALIZER", this);
    title->setStyleSheet(
        "font-size: 14px; font-weight: 500; color: #6ab4c8;"
        "letter-spacing: 8px;");
    root->addWidget(title);

    // -------------------------------------------------------------------------
    // Spectrograms — side by side, no group boxes, plain panels
    // -------------------------------------------------------------------------
    QHBoxLayout* spectro_row = new QHBoxLayout();
    spectro_row->setSpacing(12);

    // Original panel
    QWidget* orig_panel = new QWidget(this);
    orig_panel->setStyleSheet(
        "QWidget { background: #060c10; border: 0.5px solid #1a2a35; }");
    QVBoxLayout* orig_layout = new QVBoxLayout(orig_panel);
    orig_layout->setContentsMargins(0, 0, 0, 0);
    orig_layout->setSpacing(0);

    QLabel* orig_title = new QLabel("ORIGINAL", orig_panel);
    orig_title->setStyleSheet(
        "color: #2a4a5a; font-size: 8px; letter-spacing: 4px;"
        "padding: 6px 10px 4px; border: none; background: transparent;");
    orig_layout->addWidget(orig_title);

    original_widget_ = new SpectrogramWidget(
        SpectrogramWidget::Mode::ReadOnly, orig_panel);
    orig_layout->addWidget(original_widget_, 1);
    spectro_row->addWidget(orig_panel);

    // Modified panel
    QWidget* mod_panel = new QWidget(this);
    mod_panel->setStyleSheet(
        "QWidget { background: #060c10; border: 0.5px solid #1a2a35; }");
    QVBoxLayout* mod_layout = new QVBoxLayout(mod_panel);
    mod_layout->setContentsMargins(0, 0, 0, 0);
    mod_layout->setSpacing(0);

    QLabel* mod_title = new QLabel("MODIFIED  ·  DRAG TO SELECT", mod_panel);
    mod_title->setStyleSheet(
        "color: #6ab4c8; font-size: 8px; letter-spacing: 4px;"
        "padding: 6px 10px 4px; border: none; background: transparent;");
    mod_layout->addWidget(mod_title);

    modified_widget_ = new SpectrogramWidget(
        SpectrogramWidget::Mode::Editable, mod_panel);
    mod_layout->addWidget(modified_widget_, 1);
    spectro_row->addWidget(mod_panel);

    root->addLayout(spectro_row, 1);

    // -------------------------------------------------------------------------
    // Art deco divider
    // -------------------------------------------------------------------------
    QFrame* divider = new QFrame(this);
    divider->setFixedHeight(1);
    divider->setStyleSheet(
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #090e12, stop:0.2 #1a2a35,"
        "stop:0.5 #6ab4c8, stop:0.8 #1a2a35, stop:1 #090e12);");
    root->addWidget(divider);

    // -------------------------------------------------------------------------
    // Selection info bar — field/separator layout
    // -------------------------------------------------------------------------
    QWidget* sel_bar = new QWidget(this);
    sel_bar->setStyleSheet(
        "QWidget { background: #060c10; border: 0.5px solid #1a2a35; }");
    sel_bar->setFixedHeight(48);
    QHBoxLayout* sel_layout = new QHBoxLayout(sel_bar);
    sel_layout->setContentsMargins(0, 0, 0, 0);
    sel_layout->setSpacing(0);

    auto makeSelField = [&](const QString& key, const QString& val,
                            bool last = false) -> QLabel* {
        QWidget* cell = new QWidget(sel_bar);
        cell->setStyleSheet(last
            ? "QWidget { border: none; background: transparent; }"
            : "QWidget { border-right: 0.5px solid #1a2a35; background: transparent; }");
        QVBoxLayout* cl = new QVBoxLayout(cell);
        cl->setContentsMargins(14, 6, 14, 6);
        cl->setSpacing(2);
        QLabel* k = new QLabel(key, cell);
        k->setStyleSheet(
            "color: #1a2a35; font-size: 7px; letter-spacing: 2px;"
            "border: none; background: transparent;");
        QLabel* v = new QLabel(val, cell);
        v->setStyleSheet(
            "color: #6ab4c8; font-size: 11px; letter-spacing: 1px;"
            "border: none; background: transparent;");
        cl->addWidget(k);
        cl->addWidget(v);
        sel_layout->addWidget(cell);
        return v;
    };

    makeSelField("START", "—");
    makeSelField("END", "—");
    makeSelField("LOW", "—");
    makeSelField("HIGH", "—");

    selection_label_ = new QLabel(
        "No selection — drag on the modified spectrogram", sel_bar);
    selection_label_->setStyleSheet(
        "color: #2a4a5a; font-style: italic; letter-spacing: 1px;"
        "font-size: 10px; border: none; background: transparent;"
        "padding: 0 14px;");
    sel_layout->addWidget(selection_label_, 1);

    root->addWidget(sel_bar);

    // -------------------------------------------------------------------------
    // Sliders — side by side flat panels
    // -------------------------------------------------------------------------
    QHBoxLayout* sliders_row = new QHBoxLayout();
    sliders_row->setSpacing(12);

    // Gain panel
    QWidget* gain_panel = new QWidget(this);
    gain_panel->setStyleSheet(
        "QWidget { background: #060c10; border: 0.5px solid #1a2a35; }");
    QVBoxLayout* gain_layout = new QVBoxLayout(gain_panel);
    gain_layout->setContentsMargins(14, 10, 14, 10);
    gain_layout->setSpacing(6);

    QLabel* gain_title_lbl = new QLabel("GAIN  ·  COARSE", gain_panel);
    gain_title_lbl->setStyleSheet(
        "color: #2a4a5a; font-size: 8px; letter-spacing: 3px;"
        "border: none; background: transparent;");
    gain_layout->addWidget(gain_title_lbl);

    QLabel* gain_zone = new QLabel(
        "◄  SAFE  0.0 — 1.0  ···················  DANGER  1.0 — 3.0  ►",
        gain_panel);
    gain_zone->setStyleSheet(
        "color: #1a2a35; font-size: 8px; letter-spacing: 2px;"
        "border: none; background: transparent;");
    gain_layout->addWidget(gain_zone);

    QHBoxLayout* gain_row = new QHBoxLayout();
    gain_row->addWidget(new QLabel("0.0", gain_panel));

    gain_slider_ = new QSlider(Qt::Horizontal, gain_panel);
    gain_slider_->setRange(GAIN_SLIDER_MIN, GAIN_SLIDER_MAX);
    gain_slider_->setValue(GAIN_SLIDER_DEFAULT);
    gain_slider_->setTickPosition(QSlider::TicksBelow);
    gain_slider_->setTickInterval(100);
    gain_row->addWidget(gain_slider_);

    gain_row->addWidget(new QLabel("3.0", gain_panel));

    gain_label_ = new QLabel("1.00  [safe]", gain_panel);
    gain_label_->setFixedWidth(140);
    gain_label_->setStyleSheet(
        "color: #6ab4c8; font-weight: 500; letter-spacing: 2px;"
        "border: none; background: transparent;");
    gain_row->addWidget(gain_label_);
    gain_layout->addLayout(gain_row);
    sliders_row->addWidget(gain_panel);

    // Brightness panel
    QWidget* bright_panel = new QWidget(this);
    bright_panel->setStyleSheet(
        "QWidget { background: #060c10; border: 0.5px solid #1a2a35; }");
    QVBoxLayout* bright_layout = new QVBoxLayout(bright_panel);
    bright_layout->setContentsMargins(14, 10, 14, 10);
    bright_layout->setSpacing(6);

    QLabel* bright_title_lbl = new QLabel("BRIGHTNESS  ·  FINE", bright_panel);
    bright_title_lbl->setStyleSheet(
        "color: #2a4a5a; font-size: 8px; letter-spacing: 3px;"
        "border: none; background: transparent;");
    bright_layout->addWidget(bright_title_lbl);

    QLabel* bright_hint = new QLabel(
        "Darker = quieter  ·  brighter = louder  ·  adjust by eye",
        bright_panel);
    bright_hint->setStyleSheet(
        "color: #1a2a35; font-size: 8px; letter-spacing: 1px;"
        "border: none; background: transparent;");
    bright_layout->addWidget(bright_hint);

    QHBoxLayout* bright_row = new QHBoxLayout();
    bright_row->addWidget(new QLabel("0.5", bright_panel));

    brightness_slider_ = new QSlider(Qt::Horizontal, bright_panel);
    brightness_slider_->setRange(BRIGHT_SLIDER_MIN, BRIGHT_SLIDER_MAX);
    brightness_slider_->setValue(BRIGHT_SLIDER_DEFAULT);
    brightness_slider_->setTickPosition(QSlider::TicksBelow);
    brightness_slider_->setTickInterval(25);
    bright_row->addWidget(brightness_slider_);

    bright_row->addWidget(new QLabel("1.5", bright_panel));

    brightness_label_ = new QLabel("1.00", bright_panel);
    brightness_label_->setFixedWidth(140);
    brightness_label_->setStyleSheet(
        "color: #6ab4c8; letter-spacing: 1px;"
        "border: none; background: transparent;");
    bright_row->addWidget(brightness_label_);
    bright_layout->addLayout(bright_row);
    sliders_row->addWidget(bright_panel);

    root->addWidget(gain_panel);
    sliders_row->removeWidget(gain_panel);
    sliders_row->insertWidget(0, gain_panel);
    root->addLayout(sliders_row);

    // -------------------------------------------------------------------------
    // Buttons — art deco sharp corners
    // -------------------------------------------------------------------------
    QHBoxLayout* btn_row = new QHBoxLayout();
    btn_row->setSpacing(8);

    load_button_      = new QPushButton("LOAD FILE",     this);
    play_orig_button_ = new QPushButton("▶  ORIGINAL",   this);
    play_mod_button_  = new QPushButton("▶  MODIFIED",   this);
    reset_button_     = new QPushButton("RESET",         this);

    btn_row->addWidget(load_button_);
    btn_row->addStretch();
    btn_row->addWidget(play_orig_button_);
    btn_row->addWidget(play_mod_button_);
    btn_row->addWidget(reset_button_);
    root->addLayout(btn_row);

    // -------------------------------------------------------------------------
    // Status bar
    // -------------------------------------------------------------------------
    statusBar()->showMessage("Ready  ·  Load a WAV file to begin");

    // -------------------------------------------------------------------------
    // Signal/slot connections
    // -------------------------------------------------------------------------
    connect(load_button_,       &QPushButton::clicked,
            this,               &MainWindow::onLoadFile);
    connect(play_orig_button_,  &QPushButton::clicked,
            this,               &MainWindow::onPlayOriginal);
    connect(play_mod_button_,   &QPushButton::clicked,
            this,               &MainWindow::onPlayModified);
    connect(reset_button_,      &QPushButton::clicked,
            this,               &MainWindow::onReset);
    connect(gain_slider_,       &QSlider::valueChanged,
            this,               &MainWindow::onGainSliderChanged);
    connect(brightness_slider_, &QSlider::valueChanged,
            this,               &MainWindow::onBrightnessSliderChanged);
    connect(modified_widget_,   &SpectrogramWidget::selectionChanged,
            this,               &MainWindow::onSelectionChanged);
    connect(modified_widget_,   &SpectrogramWidget::selectionCleared,
            this,               &MainWindow::onSelectionCleared);
}

void MainWindow::setupMenuBar()
{
    QMenu* file_menu = menuBar()->addMenu("File");
    file_menu->addAction("Load WAV...",
                         QKeySequence::Open,
                         this, &MainWindow::onLoadFile);
    file_menu->addSeparator();
    file_menu->addAction("Quit",
                         QKeySequence::Quit,
                         this, &QWidget::close);

    QMenu* edit_menu = menuBar()->addMenu("Edit");
    edit_menu->addAction("Reset", QKeySequence(),
                         this, &MainWindow::onReset);
    edit_menu->addAction("Clear selection", QKeySequence(),
                         modified_widget_,
                         &SpectrogramWidget::clearSelection);
}

// =============================================================================
// Slots
// =============================================================================

void MainWindow::onLoadFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Load WAV File", QString(),
        "WAV Files (*.wav);;All Files (*)");
    if (path.isEmpty()) return;

    SF_INFO info{};
    SNDFILE* sf = sf_open(path.toStdString().c_str(), SFM_READ, &info);
    if (!sf) {
        QMessageBox::critical(this, "Error",
            QString("Could not open file:\n%1").arg(path));
        return;
    }

    std::vector<float> samples(
        static_cast<std::size_t>(info.frames * info.channels));
    sf_readf_float(sf, samples.data(), info.frames);
    sf_close(sf);

    if (info.channels == 2) {
        std::vector<float> mono(static_cast<std::size_t>(info.frames));
        for (std::size_t i = 0; i < mono.size(); ++i) {
            mono[i] = (samples[i * 2] + samples[i * 2 + 1]) * 0.5f;
        }
        samples = std::move(mono);
    }

    const std::uint32_t sr = static_cast<std::uint32_t>(info.samplerate);
    original_buffer_ = std::make_unique<AudioBuffer>(
        std::move(samples), 1, sr);

    loaded_file_path_ = path;
    config_ = StftConfig{ .sample_rate = sr };

    processAudio();
    setControlsEnabled(true);

    statusBar()->showMessage(
        QString("Loaded: %1  |  %2 Hz  |  %3s")
            .arg(QFileInfo(path).fileName())
            .arg(sr)
            .arg(original_buffer_->duration_seconds(), 0, 'f', 2));
}

void MainWindow::processAudio()
{
    if (!original_buffer_) return;

    processor_ = std::make_unique<StftProcessor>(config_);
    equalizer_ = std::make_unique<Equalizer>(config_);

    original_matrix_ = std::make_unique<StftMatrix>(
        processor_->transform(*original_buffer_));
    modified_matrix_ = std::make_unique<StftMatrix>(*original_matrix_);

    original_widget_->setMatrix(*original_matrix_, config_);
    modified_widget_->setMatrix(*modified_matrix_, config_);
}

void MainWindow::onGainSliderChanged(int /*value*/)
{
    updateSliderLabels();
    applyToSelection();
}

void MainWindow::onBrightnessSliderChanged(int /*value*/)
{
    updateSliderLabels();
    applyToSelection();
}

void MainWindow::applyToSelection()
{
    if (!current_selection_ || !original_matrix_) {
        qDebug() << "applyToSelection: SKIPPED —"
                 << "selection=" << current_selection_.has_value()
                 << "original_matrix=" << (original_matrix_ != nullptr);
        return;
    }

    const float gain       =
        static_cast<float>(gain_slider_->value()) / 100.0f;
    const float brightness =
        static_cast<float>(brightness_slider_->value()) / 100.0f;
    const float combined   = gain * brightness;

    qDebug() << "applyToSelection:"
             << "gain=" << gain
             << "brightness=" << brightness
             << "combined=" << combined
             << "| bins" << current_selection_->bin_start
             << "-" << current_selection_->bin_end
             << "| frames" << current_selection_->frame_start
             << "-" << current_selection_->frame_end;

    // Always start from original so changes don't accumulate.
    *modified_matrix_ = *original_matrix_;

    const auto& sel = *current_selection_;
    std::size_t cells_modified = 0;
    for (std::size_t bin = sel.bin_start; bin <= sel.bin_end; ++bin) {
        for (std::size_t frame = sel.frame_start;
             frame <= sel.frame_end; ++frame) {
            (*modified_matrix_)(bin, frame) *= combined;
            ++cells_modified;
        }
    }

    qDebug() << "  cells modified:" << cells_modified;
    modified_widget_->setMatrix(*modified_matrix_, config_);
}

void MainWindow::onSelectionChanged(eq::SelectionRegion region)
{
    current_selection_ = region;
    updateSelectionLabel();
    applyToSelection();
}

void MainWindow::onSelectionCleared()
{
    current_selection_.reset();
    updateSelectionLabel();
}

void MainWindow::onPlayOriginal()
{
    if (!original_buffer_) return;
    stopPlayback();

    // Copy the buffer so the thread owns its own data.
    // This is critical — the thread must not reference memory
    // that could be freed or modified on the main thread.
    auto buffer_copy = std::make_shared<std::vector<float>>(
        original_buffer_->samples().begin(),
        original_buffer_->samples().end());
    const std::uint32_t sr = original_buffer_->sample_rate();

    stop_playback_.store(false);
    play_orig_button_->setText("■  STOP");
    play_mod_button_->setEnabled(false);

    playback_thread_ = std::jthread([this, buffer_copy, sr]
                                    (std::stop_token stop_token) {
        playBufferOnThread(buffer_copy->data(),
                           buffer_copy->size(),
                           sr,
                           stop_token);

        // Notify the main thread that playback finished.
        // QMetaObject::invokeMethod is the safe way to call a slot
        // from a non-Qt thread — it posts to the event queue.
        QMetaObject::invokeMethod(this, [this]() {
            play_orig_button_->setText("▶  ORIGINAL");
            play_mod_button_->setEnabled(true);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onPlayModified()
{
    if (!modified_matrix_ || !processor_) return;
    stopPlayback();

    // Reconstruct on the main thread before launching the audio thread.
    // StftProcessor is not thread-safe.
    const AudioBuffer modified = processor_->reconstruct(*modified_matrix_);

    auto buffer_copy = std::make_shared<std::vector<float>>(
        modified.samples().begin(),
        modified.samples().end());
    const std::uint32_t sr = modified.sample_rate();

    qDebug() << "onPlayModified: launching thread"
             << "samples=" << buffer_copy->size()
             << "sr=" << sr;

    stop_playback_.store(false);
    play_mod_button_->setText("■  STOP");
    play_orig_button_->setEnabled(false);

    playback_thread_ = std::jthread([this, buffer_copy, sr]
                                    (std::stop_token stop_token) {
        playBufferOnThread(buffer_copy->data(),
                           buffer_copy->size(),
                           sr,
                           stop_token);

        QMetaObject::invokeMethod(this, [this]() {
            play_mod_button_->setText("▶  MODIFIED");
            play_orig_button_->setEnabled(true);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onReset()
{
    if (!original_matrix_) return;
    *modified_matrix_ = *original_matrix_;
    gain_slider_->setValue(GAIN_SLIDER_DEFAULT);
    brightness_slider_->setValue(BRIGHT_SLIDER_DEFAULT);
    modified_widget_->setMatrix(*modified_matrix_, config_);
    modified_widget_->clearSelection();
    statusBar()->showMessage("Reset — all modifications cleared");
}

// =============================================================================
// Thread-safe audio playback
// =============================================================================

void MainWindow::stopPlayback()
{
    // Signal the audio thread to stop via std::stop_token.
    // request_stop() sets the stop token — the audio thread checks it
    // in the Pa_Sleep loop and exits cleanly.
    if (playback_thread_.joinable()) {
        playback_thread_.request_stop();
        playback_thread_.join();  // wait for clean exit
    }
    stop_playback_.store(false);
}

// ---------------------------------------------------------------------------
// PlaybackThreadData — all data the audio thread needs, fully owned.
// Passed by value into the lambda so the thread owns its own copy.
// ---------------------------------------------------------------------------
struct PlaybackThreadData {
    const float*        samples;   // non-owning — kept alive by shared_ptr
    std::size_t         total;
    std::size_t         position;
    std::atomic<bool>*  stop_flag; // pointer to MainWindow::stop_playback_
};

static int paThreadCallback(const void*,
                             void* output,
                             unsigned long frames_per_buffer,
                             const PaStreamCallbackTimeInfo*,
                             PaStreamCallbackFlags,
                             void* user_data)
{
    auto* pb  = static_cast<PlaybackThreadData*>(user_data);
    auto* out = static_cast<float*>(output);

    // Check stop flag — set by main thread when user clicks stop.
    if (pb->stop_flag->load()) {
        std::fill(out, out + frames_per_buffer, 0.0f);
        return paComplete;
    }

    for (unsigned long i = 0; i < frames_per_buffer; ++i) {
        out[i] = (pb->position < pb->total)
                 ? pb->samples[pb->position++]
                 : 0.0f;
    }
    return pb->position >= pb->total ? paComplete : paContinue;
}

void MainWindow::playBufferOnThread(const float*       samples,
                                     std::size_t        n_samples,
                                     std::uint32_t      sample_rate,
                                     std::stop_token    stop_token)
{
    // PortAudio initialise — each thread that uses PA must initialise it.
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        qDebug() << "Pa_Initialize failed:" << Pa_GetErrorText(err);
        return;
    }

    PlaybackThreadData pb_data{
        .samples   = samples,
        .total     = n_samples,
        .position  = 0,
        .stop_flag = &stop_playback_
    };

    PaStream* stream = nullptr;
    err = Pa_OpenDefaultStream(&stream, 0, 1, paFloat32,
                               sample_rate, 512,
                               paThreadCallback, &pb_data);
    if (err != paNoError) {
        qDebug() << "Pa_OpenDefaultStream failed:" << Pa_GetErrorText(err);
        Pa_Terminate();
        return;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        qDebug() << "Pa_StartStream failed:" << Pa_GetErrorText(err);
        Pa_CloseStream(stream);
        Pa_Terminate();
        return;
    }

    // Poll until playback finishes or stop is requested.
    // std::stop_token::stop_requested() is checked alongside Pa_IsStreamActive.
    // This is the correct real-time pattern:
    //   - The audio callback runs on PortAudio's internal thread
    //   - This thread just waits and watches for stop signals
    //   - No locks, no allocations in the audio callback
    while (Pa_IsStreamActive(stream) == 1 &&
           !stop_token.stop_requested()) {
        Pa_Sleep(50);
    }

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    qDebug() << "playBufferOnThread: finished";
}

// =============================================================================
// UI helpers
// =============================================================================

void MainWindow::updateSelectionLabel()
{
    if (!current_selection_) {
        selection_label_->setText(
            "No selection — drag on the modified spectrogram");
        selection_label_->setStyleSheet(
            "color: #2a4a5a; font-style: italic; letter-spacing: 1px; font-size: 10px;");
        return;
    }

    const auto& sel = *current_selection_;
    const double t_start =
        static_cast<double>(sel.frame_start)
        * config_.hop_duration_ms() / 1000.0;
    const double t_end =
        static_cast<double>(sel.frame_end)
        * config_.hop_duration_ms() / 1000.0;

    selection_label_->setText(
        QString("Selection:  %1s — %2s   |   %3 Hz — %4 Hz")
            .arg(t_start, 0, 'f', 3)
            .arg(t_end,   0, 'f', 3)
            .arg(static_cast<int>(config_.hz_for_bin(sel.bin_start)))
            .arg(static_cast<int>(config_.hz_for_bin(sel.bin_end))));
    selection_label_->setStyleSheet(
        "color: #6ab4c8; letter-spacing: 1px; font-size: 10px;");
}

void MainWindow::updateSliderLabels()
{
    const float gain =
        static_cast<float>(gain_slider_->value()) / 100.0f;
    const float brightness =
        static_cast<float>(brightness_slider_->value()) / 100.0f;
    const float combined = gain * brightness;

    const bool safe = combined <= Equalizer::SAFE_MAX_GAIN;

    gain_label_->setText(
        QString("%1  [%2]")
            .arg(gain, 0, 'f', 2)
            .arg(safe ? "safe" : "DANGER"));
    gain_label_->setStyleSheet(
        safe ? "color: #6ab4c8; font-weight: 500; letter-spacing: 2px;"
             : "color: #e05050; font-weight: 500; letter-spacing: 2px;");

    brightness_label_->setText(
        QString("%1  · combined %2")
            .arg(brightness, 0, 'f', 2)
            .arg(combined,   0, 'f', 2));
    brightness_label_->setStyleSheet(
        "color: #6ab4c8; letter-spacing: 1px;");
}

void MainWindow::setControlsEnabled(bool enabled)
{
    play_orig_button_->setEnabled(enabled);
    play_mod_button_->setEnabled(enabled);
    reset_button_->setEnabled(enabled);
    gain_slider_->setEnabled(enabled);
    brightness_slider_->setEnabled(enabled);
}

} // namespace eq