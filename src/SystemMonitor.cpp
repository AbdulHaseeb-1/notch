#include "SystemMonitor.h"

#include <QByteArray>
#include <QDateTime>
#include <QProcess>

#include <string>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <iphlpapi.h>
#endif

namespace {

#ifdef Q_OS_WIN
quint64 fileTime(const FILETIME &ft)
{
    ULARGE_INTEGER value;
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}
#endif

} // namespace

SystemMonitor::SystemMonitor(QObject *parent)
    : QObject(parent)
{
    for (int i = 0; i < 28; ++i)
        m_cpuHistory.append(0);

    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &SystemMonitor::sample);
    m_timer.start();
    sample();
}

void SystemMonitor::openTaskManager()
{
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("taskmgr.exe"), {});
#endif
}

int SystemMonitor::diskPercent() const
{
    if (m_diskTotal <= 0)
        return 0;
    return qBound(0, qRound(100.0 * double(m_diskUsed) / double(m_diskTotal)), 100);
}

void SystemMonitor::sample()
{
#ifdef Q_OS_WIN
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const quint64 idleNow = fileTime(idle);
        const quint64 kernelNow = fileTime(kernel);
        const quint64 userNow = fileTime(user);
        if (m_hasCpu) {
            const quint64 idleDelta = idleNow - m_prevIdle;
            const quint64 totalDelta = (kernelNow - m_prevKernel) + (userNow - m_prevUser);
            const double instant = totalDelta == 0
                ? 0
                : 100.0 * double(totalDelta - qMin(idleDelta, totalDelta)) / double(totalDelta);
            m_cpuSmooth = m_cpuSmooth < 0 ? instant : (m_cpuSmooth * 0.4 + instant * 0.6);
            m_cpu = qBound(0, qRound(m_cpuSmooth), 100);
            if (m_cpuHistory.size() >= 28)
                m_cpuHistory.removeFirst();
            m_cpuHistory.append(m_cpu);
        }
        m_prevIdle = idleNow;
        m_prevKernel = kernelNow;
        m_prevUser = userNow;
        m_hasCpu = true;
    }

    MEMORYSTATUSEX memory;
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        m_memoryTotal = qint64(memory.ullTotalPhys);
        m_memoryUsed = qint64(memory.ullTotalPhys - memory.ullAvailPhys);
        m_memoryPercent = int(memory.dwMemoryLoad);
    }

    SYSTEM_POWER_STATUS power;
    if (GetSystemPowerStatus(&power)) {
        const bool missing = power.BatteryFlag == 128 || power.BatteryLifePercent == 255;
        m_hasBattery = !missing;
        m_battery = m_hasBattery ? int(power.BatteryLifePercent) : -1;
        m_pluggedIn = power.ACLineStatus == 1;
        m_charging = m_hasBattery && (power.BatteryFlag & 8);
        m_batterySeconds = power.BatteryLifeTime == (DWORD)-1 ? -1 : int(power.BatteryLifeTime);
    }

    wchar_t windowsDir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windowsDir, MAX_PATH) > 0 && windowsDir[1] == L':') {
        m_diskName = QString(QChar(windowsDir[0])) + QLatin1Char(':');
        const std::wstring root = std::wstring(1, windowsDir[0]) + L":\\";
        ULARGE_INTEGER available, total, totalFree;
        if (GetDiskFreeSpaceExW(root.c_str(), &available, &total, &totalFree)) {
            m_diskTotal = qint64(total.QuadPart);
            m_diskUsed = qint64(total.QuadPart - totalFree.QuadPart);
        }
    }

    MIB_IFTABLE probe{};
    DWORD tableSize = sizeof(probe);
    const DWORD first = GetIfTable(&probe, &tableSize, FALSE);
    if (first == NO_ERROR || first == ERROR_INSUFFICIENT_BUFFER) {
        QByteArray buffer(int(qMax<DWORD>(tableSize, sizeof(probe))), 0);
        auto *table = first == NO_ERROR ? &probe : reinterpret_cast<MIB_IFTABLE *>(buffer.data());
        if (first == NO_ERROR || GetIfTable(table, &tableSize, FALSE) == NO_ERROR) {
            quint64 bytesIn = 0;
            quint64 bytesOut = 0;
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const MIB_IFROW &row = table->table[i];
                if (row.dwType == 24)
                    continue;
                if (row.dwOperStatus != MIB_IF_OPER_STATUS_OPERATIONAL)
                    continue;
                bytesIn += row.dwInOctets;
                bytesOut += row.dwOutOctets;
            }
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (m_hasNet) {
                const double seconds = qMax(0.2, (now - m_prevNetMs) / 1000.0);
                // Each counter is a 32-bit byte count; it wraps every ~4 GiB.
                // Compute the delta modulo 2^32 so the wrap shows up as the
                // real delta instead of a dropped sample.
                const auto delta = [](quint32 now32, quint32 prev32) {
                    return double(quint32(now32 - prev32));
                };
                m_downBps = delta(bytesIn, m_prevIn) / seconds;
                m_upBps = delta(bytesOut, m_prevOut) / seconds;
                m_netReady = true;
            }
            m_prevIn = bytesIn;
            m_prevOut = bytesOut;
            m_prevNetMs = now;
            m_hasNet = true;
        }
    }

    m_uptime = qint64(GetTickCount64() / 1000);
#else
    Q_UNUSED(this);
#endif
    emit statsChanged();
}
