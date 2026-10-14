#include "GlassBackdrop.h"

#include <QWindow>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
// Undocumented but long-standing user32 entry point used by Windows Terminal
// and others to request acrylic. Resolved at run time so a missing export only
// costs the blur, not the app.
enum AccentState { AccentEnableAcrylicBlurBehind = 4 };
constexpr int kWcaAccentPolicy = 19;

struct AccentPolicy {
    int state;
    int flags;
    int gradientColor; // AABBGGRR
    int animationId;
};

struct CompositionAttribData {
    int attribute;
    void *data;
    SIZE_T size;
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI *)(HWND, CompositionAttribData *);
#endif

} // namespace

GlassBackdrop::GlassBackdrop(QObject *parent)
    : QObject(parent)
{
}

void GlassBackdrop::setWindow(QWindow *window)
{
#ifdef Q_OS_WIN
    if (!window || m_blurActive)
        return;

    static const auto setAttribute = reinterpret_cast<SetWindowCompositionAttributeFn>(
        reinterpret_cast<void *>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute")));
    if (!setAttribute)
        return;

    // A light, cool tint (#181411 in AABBGGRR order) at about 50% opacity. The
    // QML glass layer adds a little more on top, so text stays readable.
    AccentPolicy accent{AccentEnableAcrylicBlurBehind, 2, 0x80181411, 0};
    CompositionAttribData data{kWcaAccentPolicy, &accent, sizeof(accent)};

    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    m_blurActive = setAttribute(hwnd, &data) != FALSE;
    emit blurActiveChanged();
#else
    Q_UNUSED(window);
#endif
}
