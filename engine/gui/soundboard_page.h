// MixCast GUI - soundboard page (pads, hotkeys, "hear it myself").
#pragma once

#include "soundboard.h"
#include "widgets.h"

#include <QDialog>
#include <QKeySequence>
#include <QWidget>

#include <memory>
#include <vector>

class QCheckBox;
class QGridLayout;
class QKeySequenceEdit;
class QLabel;
class QPushButton;
class QScrollArea;
class QSlider;
class HotkeyManager;

// ---------------------------------------------------------------------------
// "Press a key" dialog.
// ---------------------------------------------------------------------------
class HotkeyDialog : public QDialog
{
    Q_OBJECT
public:
    HotkeyDialog(const QString& forWhat, const QKeySequence& current, QWidget* parent = nullptr);
    QKeySequence sequence() const;

private:
    QKeySequenceEdit* edit_ = nullptr;
};

// ---------------------------------------------------------------------------
class SoundboardPage : public QWidget
{
    Q_OBJECT
public:
    SoundboardPage(mixcast::Soundboard* sb, HotkeyManager* hotkeys, QWidget* parent = nullptr);
    ~SoundboardPage() override;

    void tick();                  // ~30 fps: pad progress
    bool handleHotkey(int id);    // true if the hotkey belongs to the soundboard
    void loadSettings();
    void saveSettings();

    int  soundCount() const { return static_cast<int>(entries_.size()); }

    // For the editor.
    QString padPath(int key);                                   // empty if the pad is gone
    bool    replaceSound(int key, const QString& newPath);       // swap a pad's file, keep its hotkey/volume
    int     addStoredSound(const QString& path, const QString& name);   // new pad, returns its key
    int  playingCount() const { return playing_; }

signals:
    void settingsChanged();
    void editRequested(int key, const QString& path, const QString& name);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    struct Entry
    {
        int               key = 0;          // stable id for this pad
        mixcast::ClipId   clip = 0;         // 0 while loading or failed
        QString           path, name;
        float             gainDb = 0.0f;
        QKeySequence      hotkey;
        bool              loading = true;
        QString           error;
        SoundPad*         pad = nullptr;
    };

    void   addFiles(const QStringList& paths);
    Entry* addEntry(const QString& path, const QString& name, float gainDb, const QKeySequence& key);
    void   startDecode(int key);
    void   onDecoded(int key, std::shared_ptr<const mixcast::Clip> clip, const QString& error);
    void   removeEntry(int key);
    void   showPadMenu(int key, const QPoint& globalPos);
    void   editHotkey(int key);
    void   editStopAllHotkey();
    bool   bindHotkey(Entry& e, const QKeySequence& seq, bool quiet);
    void   relayout();
    void   updateStopAllLabel();
    Entry* find(int key);
    void   changed();

    static int HotkeyIdFor(int key) { return 1000 + key; }
    static constexpr int kStopAllHotkeyId = 1;

    mixcast::Soundboard* sb_;
    HotkeyManager*       hotkeys_;

    std::vector<std::unique_ptr<Entry>> entries_;
    int                  nextKey_ = 1;
    int                  playing_ = 0;
    int                  columns_ = 0;
    QKeySequence         stopAllKey_;

    QScrollArea*  scroll_      = nullptr;
    QWidget*      grid_        = nullptr;
    QGridLayout*  gridLayout_  = nullptr;
    QLabel*       empty_       = nullptr;
    QLabel*       notice_      = nullptr;
    QPushButton*  stopAllKeyBtn_ = nullptr;
    QCheckBox*    oneAtATime_  = nullptr;
    QPushButton*  monitorBtn_  = nullptr;
    QSlider*      monitorVol_  = nullptr;
    QLabel*       monitorLbl_  = nullptr;
};
