#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>

// Live CPU, memory, battery, disk and network figures for the notch.
// Sampled once a second from the operating system.
class SystemMonitor final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int cpuPercent READ cpuPercent NOTIFY statsChanged)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY statsChanged)
    Q_PROPERTY(int memoryPercent READ memoryPercent NOTIFY statsChanged)
    Q_PROPERTY(qint64 memoryUsed READ memoryUsed NOTIFY statsChanged)
    Q_PROPERTY(qint64 memoryTotal READ memoryTotal NOTIFY statsChanged)
    Q_PROPERTY(bool hasBattery READ hasBattery NOTIFY statsChanged)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY statsChanged)
    Q_PROPERTY(bool charging READ charging NOTIFY statsChanged)
    Q_PROPERTY(bool pluggedIn READ pluggedIn NOTIFY statsChanged)
    Q_PROPERTY(int batterySeconds READ batterySeconds NOTIFY statsChanged)
    Q_PROPERTY(QString diskName READ diskName NOTIFY statsChanged)
    Q_PROPERTY(qint64 diskUsed READ diskUsed NOTIFY statsChanged)
    Q_PROPERTY(qint64 diskTotal READ diskTotal NOTIFY statsChanged)
    Q_PROPERTY(int diskPercent READ diskPercent NOTIFY statsChanged)
    Q_PROPERTY(bool netReady READ netReady NOTIFY statsChanged)
    Q_PROPERTY(qreal downBps READ downBps NOTIFY statsChanged)
    Q_PROPERTY(qreal upBps READ upBps NOTIFY statsChanged)
    Q_PROPERTY(qint64 uptimeSeconds READ uptimeSeconds NOTIFY statsChanged)

public:
    explicit SystemMonitor(QObject *parent = nullptr);

    int cpuPercent() const { return m_cpu; }
    QVariantList cpuHistory() const { return m_cpuHistory; }
    int memoryPercent() const { return m_memoryPercent; }
    qint64 memoryUsed() const { return m_memoryUsed; }
    qint64 memoryTotal() const { return m_memoryTotal; }
    bool hasBattery() const { return m_hasBattery; }
    int batteryPercent() const { return m_battery; }
    bool charging() const { return m_charging; }
    bool pluggedIn() const { return m_pluggedIn; }
    int batterySeconds() const { return m_batterySeconds; }
    QString diskName() const { return m_diskName; }
    qint64 diskUsed() const { return m_diskUsed; }
    qint64 diskTotal() const { return m_diskTotal; }
    int diskPercent() const;
    bool netReady() const { return m_netReady; }
    qreal downBps() const { return m_downBps; }
    qreal upBps() const { return m_upBps; }
    qint64 uptimeSeconds() const { return m_uptime; }

    Q_INVOKABLE void openTaskManager();

signals:
    void statsChanged();

private:
    void sample();

    QTimer m_timer;

    int m_cpu = -1;
    double m_cpuSmooth = -1;
    QVariantList m_cpuHistory;
    bool m_hasCpu = false;
    quint64 m_prevIdle = 0;
    quint64 m_prevKernel = 0;
    quint64 m_prevUser = 0;

    int m_memoryPercent = 0;
    qint64 m_memoryUsed = 0;
    qint64 m_memoryTotal = 0;

    bool m_hasBattery = false;
    int m_battery = -1;
    bool m_charging = false;
    bool m_pluggedIn = false;
    int m_batterySeconds = -1;

    QString m_diskName;
    qint64 m_diskUsed = 0;
    qint64 m_diskTotal = 0;

    bool m_hasNet = false;
    bool m_netReady = false;
    quint32 m_prevIn = 0;
    quint32 m_prevOut = 0;
    qint64 m_prevNetMs = 0;
    qreal m_downBps = 0;
    qreal m_upBps = 0;

    qint64 m_uptime = 0;
};
