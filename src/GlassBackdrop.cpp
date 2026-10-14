#include "GlassBackdrop.h"

#include "Appearance.h"

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

GlassBackdrop::GlassBackdrop(const Appearance *appearance, QObject *parent)
    : QObject(parent)
    , m_appearance(appearance)
{
    if (m_appearance) {
        connect(m_appearance, &Appearance::lightChanged, this, &GlassBackdrop::applyBlur);
    }
}

void GlassBackdrop::setWindow(QWindow *window)
{
    m_window = window;
    applyBlur();
}

void GlassBackdrop::applyBlur()
{
#ifdef Q_OS_WIN
    if (!m_window)
        return;

    static const auto setAttribute = reinterpret_cast<SetWindowCompositionAttributeFn>(
        reinterpret_cast<void *>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute")));
    if (!setAttribute)
        return;

    // Tint at about 50% opacity, in AABBGGRR order: #181411 for dark glass and
    // #F7F8FA for light glass. The QML glass layer adds a little more on top.
    const bool light = m_appearance && m_appearance->light();
    const unsigned int tint = light ? 0x80FAF8F7u : 0x80181411u;
    AccentPolicy accent{AccentEnableAcrylicBlurBehind, 2, static_cast<int>(tint), 0};
    CompositionAttribData data{kWcaAccentPolicy, &accent, sizeof(accent)};

    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    const bool acrylic = setAttribute(hwnd, &data) != FALSE;
    const bool active = acrylic && transparencyEnabled();
    if (active != m_blurActive) {
        m_blurActive = active;
        emit blurActiveChanged();
    }
#endif
}
