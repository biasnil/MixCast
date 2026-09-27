// MixCast GUI - custom painted controls.
#pragma once

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
    QSize sizeHint() const override { return { 12, 220 }; }
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

// Console-style fader. Value is tenths of a dB: -600 (-60 dB) .. +120 (+12 dB).
// 0 dB sits at three quarters of the travel. Double-click resets to 0 dB.
class Fader : public QAbstractSlider
{
    Q_OBJECT
public:
    explicit Fader(QWidget* parent = nullptr);
    QSize sizeHint() const override { return { 40, 220 }; }
    QSize minimumSizeHint() const override { return { 36, 120 }; }

    static float DbFromValue(int v) { return v / 10.0f; }

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
