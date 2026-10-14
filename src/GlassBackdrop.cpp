#include "GlassBackdrop.h"

#include <QWindow>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <QSettings>
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

// Settings > Personalization > Colors > "Transparency effects". When it is off
// the acrylic call still succeeds but nothing is blurred.
bool transparencyEnabled()
{
    const QSettings personalize(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    return personalize.value(QStringLiteral("EnableTransparency"), 1).toInt() != 0;
}
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
    AccentPolicy accent{AccentEnableAcrylicBlurBehind, 2, static_cast<int>(0x80181411u), 0};
    CompositionAttribData data{kWcaAccentPolicy, &accent, sizeof(accent)};

    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const bool acrylic = setAttribute(hwnd, &data) != FALSE;
    m_blurActive = acrylic && transparencyEnabled();
    emit blurActiveChanged();
#else
    Q_UNUSED(window);
#endif
}
