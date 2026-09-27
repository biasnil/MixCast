// MixCast GUI - global hotkeys (work while a game or Discord has focus).
#include "hotkeys.h"

#include <QWidget>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

// Qt key + modifiers -> Win32 virtual key + MOD_* flags.
bool ToWin32(const QKeyCombination& kc, UINT* vk, UINT* mods)
{
    const Qt::KeyboardModifiers m = kc.keyboardModifiers();
    const int  k      = kc.key();
    const bool keypad = m.testFlag(Qt::KeypadModifier);

    *mods = MOD_NOREPEAT;
    if (m & Qt::ControlModifier) *mods |= MOD_CONTROL;
    if (m & Qt::AltModifier)     *mods |= MOD_ALT;
    if (m & Qt::ShiftModifier)   *mods |= MOD_SHIFT;
    if (m & Qt::MetaModifier)    *mods |= MOD_WIN;

    if (k >= Qt::Key_0 && k <= Qt::Key_9) { *vk = keypad ? VK_NUMPAD0 + (k - Qt::Key_0) : '0' + (k - Qt::Key_0); return true; }
    if (k >= Qt::Key_A && k <= Qt::Key_Z) { *vk = 'A' + (k - Qt::Key_A); return true; }
    if (k >= Qt::Key_F1 && k <= Qt::Key_F24) { *vk = VK_F1 + (k - Qt::Key_F1); return true; }

    switch (k)
    {
    case Qt::Key_Space:      *vk = VK_SPACE;  return true;
    case Qt::Key_Tab:        *vk = VK_TAB;    return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:      *vk = VK_RETURN; return true;
    case Qt::Key_Backspace:  *vk = VK_BACK;   return true;
    case Qt::Key_Insert:     *vk = VK_INSERT; return true;
    case Qt::Key_Delete:     *vk = keypad ? VK_DECIMAL : VK_DELETE; return true;
    case Qt::Key_Home:       *vk = VK_HOME;   return true;
    case Qt::Key_End:        *vk = VK_END;    return true;
    case Qt::Key_PageUp:     *vk = VK_PRIOR;  return true;
    case Qt::Key_PageDown:   *vk = VK_NEXT;   return true;
    case Qt::Key_Left:       *vk = VK_LEFT;   return true;
    case Qt::Key_Right:      *vk = VK_RIGHT;  return true;
    case Qt::Key_Up:         *vk = VK_UP;     return true;
    case Qt::Key_Down:       *vk = VK_DOWN;   return true;
    case Qt::Key_Pause:      *vk = VK_PAUSE;  return true;
    case Qt::Key_ScrollLock: *vk = VK_SCROLL; return true;
    case Qt::Key_Asterisk:   *vk = VK_MULTIPLY; return true;
    case Qt::Key_Plus:       *vk = keypad ? VK_ADD : VK_OEM_PLUS; return true;
    case Qt::Key_Minus:      *vk = keypad ? VK_SUBTRACT : VK_OEM_MINUS; return true;
    case Qt::Key_Slash:      *vk = keypad ? VK_DIVIDE : VK_OEM_2; return true;
    case Qt::Key_Period:     *vk = keypad ? VK_DECIMAL : VK_OEM_PERIOD; return true;
    case Qt::Key_Comma:      *vk = VK_OEM_COMMA; return true;
    case Qt::Key_Semicolon:  *vk = VK_OEM_1; return true;
    case Qt::Key_Equal:      *vk = VK_OEM_PLUS; return true;
    case Qt::Key_BracketLeft:  *vk = VK_OEM_4; return true;
    case Qt::Key_BracketRight: *vk = VK_OEM_6; return true;
    case Qt::Key_Backslash:  *vk = VK_OEM_5; return true;
    case Qt::Key_Apostrophe: *vk = VK_OEM_7; return true;
    case Qt::Key_QuoteLeft:  *vk = VK_OEM_3; return true;
    case Qt::Key_MediaPlay:
    case Qt::Key_MediaTogglePlayPause: *vk = VK_MEDIA_PLAY_PAUSE; return true;
    case Qt::Key_MediaNext:  *vk = VK_MEDIA_NEXT_TRACK; return true;
    case Qt::Key_MediaPrevious: *vk = VK_MEDIA_PREV_TRACK; return true;
    default: return false;
    }
}

HWND Hwnd(QWidget* w) { return reinterpret_cast<HWND>(w->winId()); }

} // namespace

HotkeyManager::HotkeyManager(QWidget* window) : window_(window) {}

HotkeyManager::~HotkeyManager() { clearAll(); }

bool HotkeyManager::set(int id, const QKeySequence& seq, QString* error)
{
    clear(id);
    if (seq.isEmpty()) return true;

    UINT vk = 0, mods = 0;
    if (!ToWin32(seq[0], &vk, &mods))
    {
        if (error) *error = QStringLiteral("That key can't be used as a global hotkey.");
        return false;
    }
    if (!RegisterHotKey(Hwnd(window_), id, mods, vk))
    {
        if (error) *error = QStringLiteral("%1 is already used by another app.").arg(HotkeyLabel(seq));
        return false;
    }
    active_[id] = true;
    return true;
}

void HotkeyManager::clear(int id)
{
    if (!active_.contains(id)) return;
    UnregisterHotKey(Hwnd(window_), id);
    active_.remove(id);
}

void HotkeyManager::clearAll()
{
    for (int id : active_.keys()) UnregisterHotKey(Hwnd(window_), id);
    active_.clear();
}

int HotkeyManager::idFromNativeMessage(void* message)
{
    const MSG* msg = static_cast<const MSG*>(message);
    return (msg && msg->message == WM_HOTKEY) ? static_cast<int>(msg->wParam) : -1;
}

#else   // Non-Windows builds (compile checks only): hotkeys do nothing.

HotkeyManager::HotkeyManager(QWidget* window) : window_(window) {}
HotkeyManager::~HotkeyManager() {}
bool HotkeyManager::set(int id, const QKeySequence& seq, QString*) { if (!seq.isEmpty()) active_[id] = true; return true; }
void HotkeyManager::clear(int id) { active_.remove(id); }
void HotkeyManager::clearAll() { active_.clear(); }
int  HotkeyManager::idFromNativeMessage(void*) { return -1; }

#endif

QString HotkeyLabel(const QKeySequence& seq)
{
    if (seq.isEmpty()) return {};
    QString s = seq.toString(QKeySequence::NativeText);
    s.replace(QStringLiteral("Num+"), QStringLiteral("Num "));
    return s;
}
