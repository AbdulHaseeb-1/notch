#include "NightLight.h"

#include <QDesktopServices>
#include <QUrl>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {

// The CloudStore datum Windows keeps night light's state in. The value inside
// is "Data", a REG_BINARY blob.
const wchar_t *kStateSubKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store"
    L"\\DefaultAccount\\Current\\default$windows.data.bluelightreduction.bluelightreductionstate"
    L"\\windows.data.bluelightreduction.bluelightreductionstate";

// Offsets inside the blob. Byte 18 is 0x13 off / 0x15 on; bytes 10..14 are a
// sequence counter Windows bumps on every write, and byte 23/24 carry a small
// parameter block that exists only while night light is on.
constexpr int kMarkerOffset = 18;
constexpr char kMarkerOff = 0x13;
constexpr char kMarkerOn = 0x15;

// The blob is REG_BINARY and every byte must round-trip untouched, so it is
// read and written through the Win32 registry API directly.
QByteArray readBlob()
{
#ifdef Q_OS_WIN
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kStateSubKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return {};

    QByteArray blob;
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, L"Data", nullptr, &type, nullptr, &size) == ERROR_SUCCESS
        && type == REG_BINARY && size > 0 && size <= 4096) {
        blob.resize(int(size));
        DWORD read = size;
        if (RegQueryValueExW(key, L"Data", nullptr, &type,
                             reinterpret_cast<LPBYTE>(blob.data()), &read) == ERROR_SUCCESS)
            blob.truncate(int(read));
        else
            blob.clear();
    }
    RegCloseKey(key);
    return blob;
#else
    return {};
#endif
}

void writeBlob(const QByteArray &blob)
{
#ifdef Q_OS_WIN
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kStateSubKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, L"Data", 0, REG_BINARY,
                   reinterpret_cast<const BYTE *>(blob.constData()), DWORD(blob.size()));
    RegCloseKey(key);
#else
    Q_UNUSED(blob)
#endif
}

} // namespace

NightLight::NightLight(QObject *parent)
    : QObject(parent)
{
    refresh();
}

void NightLight::refresh()
{
    bool supported = false;
    bool on = false;

    const QByteArray blob = readBlob();
    if (blob.size() >= 26) {
        const auto marker = static_cast<quint8>(blob.at(kMarkerOffset));
        if (marker == static_cast<quint8>(kMarkerOff)) {
            supported = true;
            on = false;
        } else if (marker == static_cast<quint8>(kMarkerOn)) {
            supported = true;
            on = true;
        }
    }

    if (supported != m_supported || on != m_on) {
        m_supported = supported;
        m_on = on;
        emit changed();
    }
}

void NightLight::toggle()
{
    QByteArray blob = readBlob();
    if (blob.size() < 26) {
        refresh();
        emit failed();
        return;
    }

    const auto marker = static_cast<quint8>(blob.at(kMarkerOffset));
    if (marker != static_cast<quint8>(kMarkerOff) && marker != static_cast<quint8>(kMarkerOn)) {
        refresh();
        emit failed();
        return;
    }

    // A fresh write: bump the sequence byte.
    for (int i = 10; i <= 14 && i < blob.size(); ++i) {
        if (static_cast<quint8>(blob.at(i)) != 0xFF) {
            blob[i] = static_cast<char>(static_cast<quint8>(blob.at(i)) + 1);
            break;
        }
    }

    if (marker == static_cast<quint8>(kMarkerOn)) {
        // On -> off: remove the two parameter bytes at 23.. and shrink.
        blob[kMarkerOffset] = kMarkerOff;
        const int size = blob.size();
        for (int j = 23; j < size - 2; ++j)
            blob[j] = blob[j + 2];
        blob.truncate(size - 2);
    } else {
        // Off -> on: insert 0x10 0x00 at 23.., shifting the tail right.
        blob[kMarkerOffset] = kMarkerOn;
        const int size = blob.size();
        blob.resize(size + 2);
        for (int j = size + 1; j > 24; --j)
            blob[j] = blob[j - 2];
        blob[23] = static_cast<char>(0x10);
        blob[24] = static_cast<char>(0x00);
    }

    writeBlob(blob);
    refresh();

    if (!m_supported || m_on == (marker == static_cast<quint8>(kMarkerOn)))
        emit failed();
}

void NightLight::openSettings() const
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:nightlight")));
}
