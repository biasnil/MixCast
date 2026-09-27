// MixCast GUI - global hotkeys (work while a game or Discord has focus).
#pragma once

#include <QKeySequence>
#include <QMap>
#include <QString>

class QWidget;

class HotkeyManager
{
public:
    explicit HotkeyManager(QWidget* window);
    ~HotkeyManager();

    // Binds `id` to the first key combination of `seq` (empty = unbind).
    // Returns false with a reason if Windows refuses (e.g. another app owns it).
    bool set(int id, const QKeySequence& seq, QString* error = nullptr);
    void clear(int id);
    void clearAll();

    // Call from QWidget::nativeEvent. Returns the hotkey id, or -1.
    static int idFromNativeMessage(void* message);

private:
    QWidget*        window_;
    QMap<int, bool> active_;
};

// Human-readable label, e.g. "Ctrl+Num 1".
QString HotkeyLabel(const QKeySequence& seq);
