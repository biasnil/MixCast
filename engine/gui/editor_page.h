// MixCast GUI - audio editor tab.
//
// Opened from a soundboard pad: "Save to pad" replaces that pad's sound.
// Opened from a file: nothing is overwritten; you Export a new file (or add it
// to the soundboard, after which saves go to that pad).
#pragma once

#include "audio_edit.h"
#include "preview_player.h"

#include <QList>
#include <QWidget>

#include <functional>
#include <memory>

class QLabel;
class QPushButton;
class QScrollBar;
class QToolButton;
class SoundboardPage;

// ---------------------------------------------------------------------------
// Waveform with time ruler, selection, cursor and playhead.
// ---------------------------------------------------------------------------
class WaveformView : public QWidget
{
    Q_OBJECT
public:
    using Range = mixcast::edit::Range;

    explicit WaveformView(QWidget* parent = nullptr);

    void   setAudio(mixcast::edit::Buffer audio, std::shared_ptr<const mixcast::edit::Peaks> peaks, bool keepView);
    size_t totalFrames() const;

    Range  selection() const { return sel_; }
    void   setSelection(Range r);
    size_t cursor() const { return cursor_; }
    void   setCursor(size_t frame);
    void   setPlayhead(long long frame);          // -1 hides it

    void   zoomIn();
    void   zoomOut();
    void   zoomFit();
    void   zoomToSelection();
    void   follow(size_t frame);                  // keep the playhead on screen

    // Horizontal scrollbar sync (units: pixels at the current zoom).
    int    scrollMax() const;
    int    scrollValue() const;
    void   setScrollValue(int v);

signals:
    void selectionChanged();
    void viewChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    enum class Drag { None, Select, EdgeA, EdgeB };

    double frameAtX(double x) const { return first_ + x * fpp_; }
    double xAtFrame(double f) const { return (f - first_) / fpp_; }
    size_t clampFrame(double f) const;
    void   zoomAround(double factor, double x);
    void   clampView();
    void   paintRuler(class QPainter& p);
    void   paintWave(class QPainter& p, int top, int height);

    mixcast::edit::Buffer                       audio_;
    std::shared_ptr<const mixcast::edit::Peaks> peaks_;
    double    first_ = 0.0;   // first visible frame
    double    fpp_   = 1.0;   // frames per pixel
    Range     sel_;
    size_t    cursor_ = 0;
    long long playhead_ = -1;

    Drag      drag_ = Drag::None;
    size_t    anchor_ = 0;
    QPoint    pressPos_;

    static constexpr int kRuler = 26;
};

// ---------------------------------------------------------------------------
class EditorPage : public QWidget
{
    Q_OBJECT
public:
    explicit EditorPage(SoundboardPage* soundboard, QWidget* parent = nullptr);
    ~EditorPage() override;

    void openFile(const QString& path);                                  // file mode
    void openPad(int padKey, const QString& path, const QString& name); // pad mode

    // Asks to save if there are unsaved edits. False = user cancelled.
    bool confirmDiscard();
    bool hasDocument() const { return static_cast<bool>(audio_); }

    void tick();   // ~30 fps: playhead

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    using Samples = mixcast::edit::Samples;
    using Range   = mixcast::edit::Range;

    QWidget* buildFileBar();
    QWidget* buildToolBar();
    void     setupShortcuts();

    void load(const QString& path, const QString& title, int padKey);
    void setAudio(mixcast::edit::Buffer audio, bool keepView);
    void applyEdit(const QString& what, const std::function<Samples(const Samples&)>& op,
                   Range newSelection = {}, size_t newCursor = 0, bool fit = false);
    Range effectRange(bool fadeIn = false, bool fadeOut = false) const;

    // Actions
    void playPause();
    void stopPlayback();
    void cut();
    void copy();
    void paste();
    void deleteSelection();
    void keepSelection();
    void undo();
    void redo();
    void saveOrExport();
    void saveToPad();
    void exportFile();
    void addToSoundboard();

    void updateUi();
    void showStatus(const QString& text, bool warn = false);

    SoundboardPage*            sb_;
    mixcast::PreviewPlayer     player_;
    mixcast::edit::History     history_;
    mixcast::edit::Buffer      audio_;
    mixcast::edit::Buffer      savedAudio_;       // what was last saved/exported ("edited" otherwise)
    QString                    title_;
    QString                    sourcePath_;
    int                        padKey_ = 0;        // 0 = file mode
    bool                       busy_ = false;
    bool                       wasPlaying_ = false;

    static Samples             clipboard_;

    WaveformView* view_      = nullptr;
    QScrollBar*   hscroll_   = nullptr;
    QWidget*      editArea_  = nullptr;
    QLabel*       empty_     = nullptr;
    QLabel*       titleLbl_  = nullptr;
    QLabel*       sourceLbl_ = nullptr;
    QLabel*       lengthLbl_ = nullptr;
    QLabel*       selLbl_    = nullptr;
    QLabel*       statusLbl_ = nullptr;

    QPushButton*  saveBtn_   = nullptr;   // "Save to pad"
    QPushButton*  exportBtn_ = nullptr;
    QPushButton*  addPadBtn_ = nullptr;
    QToolButton*  undoBtn_   = nullptr;
    QToolButton*  redoBtn_   = nullptr;
    QToolButton*  playBtn_   = nullptr;
    QToolButton*  loopBtn_   = nullptr;
    QList<QToolButton*> needsSelection_;
    QList<QWidget*>     needsAudio_;
    QToolButton*  pasteBtn_  = nullptr;
};
