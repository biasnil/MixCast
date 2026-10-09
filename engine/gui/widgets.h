// MixCast GUI - custom painted controls.
#pragma once

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QElapsedTimer>
#include <QWidget>

// Vertical LED ladder, -60..0 dBFS, with ballistics and peak hold.
class LevelMeter : public QWidget
{
    Q_OBJECT
public:
    explicit LevelMeter(QWidget* parent = nullptr);
    void  setLevel(float linearPeak);   // call ~30x per second
    QSize sizeHint() const override { return { 14, 170 }; }
    QSize minimumSizeHint() const override { return { 10, 120 }; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    float         displayDb_ = -60.0f;
    float         peakDb_    = -60.0f;
    qint64        peakAtMs_  = 0;
    QElapsedTimer clock_;
    qint64        lastMs_    = 0;
};

// Pill fader: a fat track that fills up to a round knob. Value is tenths of a
// dB: -600 (-60 dB) .. +120 (+12 dB). 0 dB sits at three quarters of the
// travel. Double-click resets to 0 dB.
class Fader : public QAbstractSlider
{
    Q_OBJECT
public:
    explicit Fader(QWidget* parent = nullptr);
    QSize sizeHint() const override { return { 40, 170 }; }
    QSize minimumSizeHint() const override { return { 40, 120 }; }

    static float DbFromValue(int v) { return v / 10.0f; }
    void  setDimmed(bool dimmed);   // channel switched off: muted colours

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    static double PosFromDb(double db);     // 0 = bottom, 1 = top
    static double DbFromPos(double pos);
    double travelTop() const;
    double travelBottom() const;
    double yFromValue(int v) const;
    int    valueFromY(double y) const;
    QRectF capRect() const;

    bool   dragging_   = false;
    bool   dimmed_     = false;
    double dragOffset_ = 0.0;
};

// Round lamp: lit red while you're talking.
class TallyLamp : public QWidget
{
    Q_OBJECT
public:
    explicit TallyLamp(QWidget* parent = nullptr);
    void  setLit(bool lit);
    QSize sizeHint() const override { return { 14, 14 }; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    bool lit_ = false;
};

// What the mic clean-up is doing, live: one column per frequency band.
// Green = your voice that's kept; red above it = background and clicks removed.
class CleanupScope : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kBands = 20;
    explicit CleanupScope(QWidget* parent = nullptr);
    void  setBands(const float* inDb, const float* outDb, float removedDb);   // ~30x per second
    QSize sizeHint() const override { return { 116, 40 }; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    float         in_[kBands], out_[kBands];
    float         removedDb_ = 0.0f;
    QElapsedTimer clock_;
    qint64        lastMs_ = 0;
};

// ---------------------------------------------------------------------------
// One pad: click to play/stop, right-click for options.
// ---------------------------------------------------------------------------
class SoundPad : public QAbstractButton
{
    Q_OBJECT
public:
    enum class State { Loading, Ready, Failed };

    explicit SoundPad(QWidget* parent = nullptr);
    void  setName(const QString& name)      { name_ = name; update(); }
    void  setHotkey(const QString& label)   { hotkey_ = label; update(); }
    void  setState(State s, const QString& detail = {});
    void  setProgress(float p);             // < 0 = not playing
    QSize sizeHint() const override { return { 172, 88 }; }

signals:
    void menuRequested(const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent*) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    QString name_, hotkey_, detail_;
    State   state_ = State::Loading;
    float   progress_ = -1.0f;
};
