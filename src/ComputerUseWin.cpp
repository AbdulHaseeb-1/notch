// Windows input backend for Buddy's computer use: SendInput for mouse and
// keyboard (absolute, virtual-desktop coordinates in physical pixels),
// GetCursorPos, password-field detection on the focused control, and
// low-level hooks that tell real user input from our own injected input.
#include "ComputerUse.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QVector>

#include <cstdlib>

namespace {

class WinBackend;
WinBackend *g_watcher = nullptr;

WORD vkFor(const QString &k)
{
    if (k == QLatin1String("ctrl")) return VK_CONTROL;
    if (k == QLatin1String("alt")) return VK_MENU;
    if (k == QLatin1String("shift")) return VK_SHIFT;
    if (k == QLatin1String("super")) return VK_LWIN;
    if (k == QLatin1String("return")) return VK_RETURN;
    if (k == QLatin1String("escape")) return VK_ESCAPE;
    if (k == QLatin1String("tab")) return VK_TAB;
    if (k == QLatin1String("space")) return VK_SPACE;
    if (k == QLatin1String("backspace")) return VK_BACK;
    if (k == QLatin1String("delete")) return VK_DELETE;
    if (k == QLatin1String("insert")) return VK_INSERT;
    if (k == QLatin1String("up")) return VK_UP;
    if (k == QLatin1String("down")) return VK_DOWN;
    if (k == QLatin1String("left")) return VK_LEFT;
    if (k == QLatin1String("right")) return VK_RIGHT;
    if (k == QLatin1String("home")) return VK_HOME;
    if (k == QLatin1String("end")) return VK_END;
    if (k == QLatin1String("page_up")) return VK_PRIOR;
    if (k == QLatin1String("page_down")) return VK_NEXT;
    if (k == QLatin1String("caps_lock")) return VK_CAPITAL;
    if (k == QLatin1String("print")) return VK_SNAPSHOT;
    if (k.size() >= 2 && k.at(0) == QLatin1Char('f')) {
        bool ok = false;
        const int n = k.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 24)
            return WORD(VK_F1 + n - 1);
    }
    if (k.size() == 1) {
        const QChar c = k.at(0);
        if (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
            return WORD('A' + (c.unicode() - 'a'));
        if (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            return WORD(c.unicode());
        const SHORT vk = VkKeyScanW(c.unicode());
        if (vk != -1)
            return WORD(vk & 0xFF);
    }
    return 0;
}

bool isExtended(WORD vk)
{
    switch (vk) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT: case VK_LWIN: case VK_SNAPSHOT:
        return true;
    default:
        return false;
    }
}

class WinBackend final : public ComputerBackend
{
public:
    ~WinBackend() override { setWatching(false); }

    QString name() const override { return QStringLiteral("windows"); }

    QPoint cursorPos() const override
    {
        POINT p{};
        GetCursorPos(&p);
        const QPoint phys(p.x, p.y);
        return m_mapper ? m_mapper->physicalToLogical(phys) : phys;
    }

    void moveTo(const QPoint &logical) override
    {
        const QPoint phys = m_mapper ? m_mapper->logicalToPhysical(logical) : logical;
        // Absolute move on the virtual desktop (0..65535), injected so the
        // hook can tell it from the user's own mouse.
        const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int vw = qMax(1, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1);
        const int vh = qMax(1, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1);
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dx = LONG(qRound((phys.x() - vx) * 65535.0 / vw));
        in.mi.dy = LONG(qRound((phys.y() - vy) * 65535.0 / vh));
        in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        SendInput(1, &in, sizeof(INPUT));
        m_lastInjected = phys;
    }

    void button(Qt::MouseButton b, bool down) override
    {
        INPUT in{};
        in.type = INPUT_MOUSE;
        if (b == Qt::RightButton)
            in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        else if (b == Qt::MiddleButton)
            in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        else
            in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        SendInput(1, &in, sizeof(INPUT));
    }

    void scroll(int dx, int dy) override
    {
        if (dy != 0) {
            INPUT in{};
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = MOUSEEVENTF_WHEEL;
            in.mi.mouseData = DWORD(dy * WHEEL_DELTA);
            SendInput(1, &in, sizeof(INPUT));
        }
        if (dx != 0) {
            INPUT in{};
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = MOUSEEVENTF_HWHEEL;
            in.mi.mouseData = DWORD(dx * WHEEL_DELTA);
            SendInput(1, &in, sizeof(INPUT));
        }
    }

    bool typeText(const QString &text) override
    {
        QVector<INPUT> ev;
        ev.reserve(text.size() * 2);
        for (const QChar ch : text) {
            if (ch == QLatin1Char('\r'))
                continue;
            INPUT d{};
            d.type = INPUT_KEYBOARD;
            if (ch == QLatin1Char('\n')) {
                d.ki.wVk = VK_RETURN;
            } else {
                d.ki.wScan = ch.unicode();
                d.ki.dwFlags = KEYEVENTF_UNICODE;
            }
            INPUT u = d;
            u.ki.dwFlags |= KEYEVENTF_KEYUP;
            ev << d << u;
        }
        return ev.isEmpty() || SendInput(UINT(ev.size()), ev.data(), sizeof(INPUT)) == UINT(ev.size());
    }

    bool keyCombo(const cu::KeyCombo &combo) override
    {
        QVector<WORD> vks;
        for (const QString &m : combo.modifiers)
            vks << vkFor(m);
        vks << vkFor(combo.key);
        if (vks.contains(WORD(0)))
            return false;
        QVector<INPUT> ev;
        for (WORD vk : vks) {
            INPUT in{};
            in.type = INPUT_KEYBOARD;
            in.ki.wVk = vk;
            in.ki.dwFlags = isExtended(vk) ? KEYEVENTF_EXTENDEDKEY : 0;
            ev << in;
        }
        for (int i = vks.size() - 1; i >= 0; --i) {
            INPUT in{};
            in.type = INPUT_KEYBOARD;
            in.ki.wVk = vks.at(i);
            in.ki.dwFlags = KEYEVENTF_KEYUP | (isExtended(vks.at(i)) ? KEYEVENTF_EXTENDEDKEY : 0);
            ev << in;
        }
        return SendInput(UINT(ev.size()), ev.data(), sizeof(INPUT)) == UINT(ev.size());
    }

    bool keyState(const cu::KeyCombo &combo, bool down) override
    {
        QVector<WORD> vks;
        for (const QString &m : combo.modifiers)
            vks << vkFor(m);
        vks << vkFor(combo.key);
        if (vks.contains(WORD(0)))
            return false;
        QVector<INPUT> ev;
        // Down in order, up in reverse, like a person pressing and letting go.
        for (int n = 0; n < vks.size(); ++n) {
            const WORD vk = down ? vks.at(n) : vks.at(vks.size() - 1 - n);
            INPUT in{};
            in.type = INPUT_KEYBOARD;
            in.ki.wVk = vk;
            in.ki.dwFlags = (isExtended(vk) ? KEYEVENTF_EXTENDEDKEY : 0) | (down ? 0 : KEYEVENTF_KEYUP);
            ev << in;
        }
        return SendInput(UINT(ev.size()), ev.data(), sizeof(INPUT)) == UINT(ev.size());
    }

    QImage grab(const ScreenInfo &screen) override
    {
        for (QScreen *s : QGuiApplication::screens())
            if (s->name() == screen.name)
                return s->grabWindow(0).toImage();
        QScreen *p = QGuiApplication::primaryScreen();
        return p ? p->grabWindow(0).toImage() : QImage();
    }

    bool focusedIsPassword() const override
    {
        GUITHREADINFO gti{};
        gti.cbSize = sizeof(gti);
        if (!GetGUIThreadInfo(0, &gti) || !gti.hwndFocus)
            return false;
        // Classic Win32 edit controls flag password boxes with ES_PASSWORD
        // (also what most UI toolkits wrap). UI Automation IsPassword would
        // also cover browsers/WinUI; see docs/agent-safety.md.
        const LONG style = GetWindowLongW(gti.hwndFocus, GWL_STYLE);
        if (style & ES_PASSWORD)
            return true;
        wchar_t cls[64] = {};
        GetClassNameW(gti.hwndFocus, cls, 63);
        const QString c = QString::fromWCharArray(cls).toLower();
        if (c.contains(QLatin1String("edit")) && SendMessageW(gti.hwndFocus, EM_GETPASSWORDCHAR, 0, 0) != 0)
            return true;
        // Credential UI (Windows Security sign-in prompts).
        wchar_t title[128] = {};
        GetWindowTextW(GetForegroundWindow(), title, 127);
        const QString t = QString::fromWCharArray(title).toLower();
        return t.contains(QLatin1String("windows security")) || t.contains(QLatin1String("credential"));
    }

    void setWatching(bool on) override
    {
        if (on == (m_mouseHook != nullptr))
            return;
        if (on) {
            g_watcher = this;
            m_escDownAt = 0;
            m_userTravel = 0;
            m_lastReal = QPoint(-100000, -100000);
            m_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, &WinBackend::mouseProc, GetModuleHandleW(nullptr), 0);
            m_keyHook = SetWindowsHookExW(WH_KEYBOARD_LL, &WinBackend::keyProc, GetModuleHandleW(nullptr), 0);
        } else {
            if (m_mouseHook)
                UnhookWindowsHookEx(m_mouseHook);
            if (m_keyHook)
                UnhookWindowsHookEx(m_keyHook);
            m_mouseHook = nullptr;
            m_keyHook = nullptr;
            if (g_watcher == this)
                g_watcher = nullptr;
        }
    }

private:
    static LRESULT CALLBACK mouseProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION && g_watcher) {
            const auto *m = reinterpret_cast<const MSLLHOOKSTRUCT *>(lParam);
            const bool injected = (m->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED)) != 0;
            if (!injected) {
                bool real = wParam != WM_MOUSEMOVE;
                if (!real) {   // a desk bump is not a take-over: need ~12 px of travel
                    WinBackend *w = g_watcher;
                    if (w->m_lastReal.x() > -100000)
                        w->m_userTravel += std::abs(m->pt.x - w->m_lastReal.x()) + std::abs(m->pt.y - w->m_lastReal.y());
                    w->m_lastReal = QPoint(m->pt.x, m->pt.y);
                    real = w->m_userTravel > 12;
                }
                if (real && g_watcher->onUserInput)
                    g_watcher->onUserInput(false);
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    static LRESULT CALLBACK keyProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION && g_watcher) {
            const auto *k = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
            const bool injected = (k->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) != 0;
            const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
            if (!injected && g_watcher->onUserInput) {
                if (k->vkCode == VK_ESCAPE) {
                    const bool ctrl = GetAsyncKeyState(VK_CONTROL) & 0x8000;
                    const bool alt = GetAsyncKeyState(VK_MENU) & 0x8000;
                    if (down && ctrl && alt) {
                        g_watcher->onUserInput(true);            // Ctrl+Alt+Esc: abort
                    } else if (down) {
                        if (g_watcher->m_escDownAt == 0)
                            g_watcher->m_escDownAt = k->time;
                        else if (k->time - g_watcher->m_escDownAt >= 600)
                            g_watcher->onUserInput(true);        // Esc held: abort (auto-repeat)
                        else
                            g_watcher->onUserInput(false);
                    } else {
                        g_watcher->m_escDownAt = 0;
                    }
                } else if (down) {
                    g_watcher->onUserInput(false);
                }
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    HHOOK m_mouseHook = nullptr;
    HHOOK m_keyHook = nullptr;
    QPoint m_lastInjected{ -100000, -100000 };
    QPoint m_lastReal{ -100000, -100000 };
    int m_userTravel = 0;
    DWORD m_escDownAt = 0;
};

} // namespace

std::unique_ptr<ComputerBackend> makeWindowsBackend()
{
    return std::make_unique<WinBackend>();
}
#endif
