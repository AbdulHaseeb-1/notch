#include "PushToTalkKey.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QWindow>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>

namespace {
constexpr int kHotkeyId = 0x4E54; // "NT"

struct Candidate {
    UINT mods;
    UINT vk;
    const wchar_t *text;
};

// Held to talk, so a combination you can keep down comfortably. Ctrl+Alt+V
// first; if another app owns it, a three-key variant.
constexpr Candidate kCandidates[] = {
    { MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'V', L"Ctrl+Alt+V" },
    { MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'V', L"Ctrl+Alt+Shift+V" },
};
constexpr int kCandidateCount = int(sizeof(kCandidates) / sizeof(kCandidates[0]));
} // namespace
#endif

PushToTalkKey::PushToTalkKey(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(30);
    connect(&m_timer, &QTimer::timeout, this, &PushToTalkKey::poll);
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installNativeEventFilter(this);
#ifdef Q_OS_WIN
    m_probe = [this]() { return m_vk != 0 && (GetAsyncKeyState(int(m_vk)) & 0x8000) != 0; };
#endif
}

PushToTalkKey::~PushToTalkKey()
{
    deregister();
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

void PushToTalkKey::setWindow(QWindow *window)
{
    if (m_window == window)
        return;
    deregister();
    m_window = window;
    if (window)
        connect(window, &QObject::destroyed, this, [this]() { deregister(); });
    if (m_enabled && window)
        tryRegister();
}

void PushToTalkKey::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    emit enabledChanged();
    if (on) {
        tryRegister();
    } else {
        release();
        deregister();
    }
}

void PushToTalkKey::tryRegister()
{
#ifdef Q_OS_WIN
    if (m_registered || !m_window)
        return;
    const auto hwnd = reinterpret_cast<HWND>(m_window->winId());
    if (!hwnd)
        return;
    for (int i = 0; i < kCandidateCount; ++i) {
        if (!RegisterHotKey(hwnd, kHotkeyId, kCandidates[i].mods, kCandidates[i].vk))
            continue;
        m_registered = true;
        m_hwnd = reinterpret_cast<quintptr>(hwnd);
        m_vk = kCandidates[i].vk;
        m_shortcut = QString::fromWCharArray(kCandidates[i].text);
        emit registeredChanged();
        return;
    }
    qWarning("noch: no free shortcut for push-to-talk; hold-to-talk by key stays off");
#endif
}

void PushToTalkKey::deregister()
{
    if (!m_registered)
        return;
    m_registered = false;
#ifdef Q_OS_WIN
    const auto hwnd = reinterpret_cast<HWND>(m_hwnd);
    if (hwnd)
        UnregisterHotKey(hwnd, kHotkeyId);
#endif
    m_hwnd = 0;
    m_vk = 0;
    m_shortcut.clear();
    emit registeredChanged();
}

void PushToTalkKey::press()
{
    if (!m_enabled || m_held)
        return;
    m_held = true;
    m_upPolls = 0;
    m_pressedAt = QDateTime::currentMSecsSinceEpoch();
    emit heldChanged();
    emit pressed();
    m_timer.start();
}

void PushToTalkKey::release()
{
    if (!m_held)
        return;
    m_timer.stop();
    m_held = false;
    emit heldChanged();
    emit released();
}

void PushToTalkKey::poll()
{
    if (!m_held) {
        m_timer.stop();
        return;
    }
    // No way to see the key (no probe): treat the press as a tap, which ends at once.
    const bool down = m_probe && m_probe();
    if (down) {
        m_upPolls = 0;
    } else if (++m_upPolls >= 2) {
        release();
        return;
    }
    if (QDateTime::currentMSecsSinceEpoch() - m_pressedAt >= m_maxHoldMs)
        release();   // the key-up was missed; never leave the microphone open
}

bool PushToTalkKey::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result)
#ifdef Q_OS_WIN
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        return false;
    const auto *msg = static_cast<MSG *>(message);
    if (msg && msg->message == WM_HOTKEY && msg->wParam == kHotkeyId) {
        press();
        return true;
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
#endif
    return false;
}
