#include "GlobalHotkey.h"

#include <QCoreApplication>
#include <QDebug>
#include <QWindow>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>

namespace {
constexpr int kHotkeyId = 0x4E43; // "NC"

struct Candidate {
    UINT mods;
    UINT vk;
    const wchar_t *text;
};

// Ctrl+Alt+Space is the combination the palette advertises. It is also the
// default summon for some other apps (Claude Desktop, notably), so when the
// system says it is taken, quieter combinations are tried in order and the
// QML side reports which one is live.
constexpr Candidate kCandidates[] = {
    { MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_SPACE, L"Ctrl+Alt+Space" },
    { MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, VK_SPACE, L"Alt+Shift+Space" },
    { MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'P', L"Ctrl+Alt+P" },
};
constexpr int kCandidateCount = int(sizeof(kCandidates) / sizeof(kCandidates[0]));
} // namespace
#endif

GlobalHotkey::GlobalHotkey(QObject *parent)
    : QObject(parent)
{
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkey::~GlobalHotkey()
{
    unregister();
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

void GlobalHotkey::setWindow(QWindow *window)
{
    if (m_window == window)
        return;
    deregisterWindow();
    m_window = window;
    if (window)
        // The engine (and its root window) is destroyed before we are, so
        // track that instead of ever dereferencing a dead pointer.
        connect(window, &QObject::destroyed, this, [this]() { deregisterWindow(); });
    if (m_enabled && window)
        tryRegister();
}

void GlobalHotkey::deregisterWindow()
{
    // A window being replaced or destroyed takes its HWND with it; use the
    // handle cached at register time so this stays valid even when called
    // from the destructor, after the QWindow is already gone.
    if (m_registered) {
        m_registered = false;
#ifdef Q_OS_WIN
        const auto hwnd = reinterpret_cast<HWND>(m_hwnd);
        if (hwnd)
            UnregisterHotKey(hwnd, kHotkeyId);
#endif
        m_hwnd = 0;
        emit registeredChanged();
    }
}

void GlobalHotkey::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
    if (enabled)
        tryRegister();
    else
        deregisterWindow();
}

void GlobalHotkey::tryRegister()
{
#ifdef Q_OS_WIN
    if (m_registered || !m_window)
        return;
    const auto hwnd = reinterpret_cast<HWND>(m_window->winId());
    if (!hwnd)
        return;
    for (int i = 0; i < kCandidateCount; ++i) {
        const Candidate &candidate = kCandidates[i];
        if (!RegisterHotKey(hwnd, kHotkeyId, candidate.mods, candidate.vk))
            continue;
        m_registered = true;
        m_preferred = i == 0;
        m_hwnd = reinterpret_cast<quintptr>(hwnd);
        m_shortcut = QString::fromWCharArray(candidate.text);
        emit registeredChanged();
        emit shortcutChanged();
        return;
    }
    qWarning("noch: no free shortcut for the command palette; the hotkey stays off");
#endif
}

void GlobalHotkey::unregister()
{
    // Uses only cached state; safe to call when the window is already gone.
    deregisterWindow();
}

bool GlobalHotkey::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result)

#ifdef Q_OS_WIN
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        return false;
    const auto *msg = static_cast<MSG *>(message);
    if (msg && msg->message == WM_HOTKEY && msg->wParam == kHotkeyId) {
        emit activated();
        return true;
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
#endif
    return false;
}
