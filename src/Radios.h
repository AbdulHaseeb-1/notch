#pragma once

#include <QList>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QWaitCondition>
#include <atomic>

class RadiosWorker;

// Wi-Fi and Bluetooth on/off, the way the quick settings flyout does it: via
// the Windows Radio API. Polling keeps the chips truthful even when the user
// flips a radio elsewhere (a few seconds apart while a toggle is pending,
// roughly every ten seconds otherwise).
class Radios final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool wifiAvailable READ wifiAvailable NOTIFY changed)
    Q_PROPERTY(bool wifiOn READ wifiOn NOTIFY changed)
    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable NOTIFY changed)
    Q_PROPERTY(bool bluetoothOn READ bluetoothOn NOTIFY changed)

public:
    explicit Radios(QObject *parent = nullptr);
    ~Radios() override;

    bool wifiAvailable() const { return m_wifiAvailable; }
    bool wifiOn() const { return m_wifiOn; }
    bool bluetoothAvailable() const { return m_bluetoothAvailable; }
    bool bluetoothOn() const { return m_bluetoothOn; }

    Q_INVOKABLE void toggleWifi();
    Q_INVOKABLE void toggleBluetooth();

signals:
    void changed();
    // The system refused to flip a radio (policy or permission).
    void denied();

private:
    void applyState(bool wifiAvailable, bool wifiOn, bool bluetoothAvailable,
                    bool bluetoothOn);

    RadiosWorker *m_worker = nullptr;
    bool m_wifiAvailable = false;
    bool m_wifiOn = false;
    bool m_bluetoothAvailable = false;
    bool m_bluetoothOn = false;
};

// The polling thread; see SmtcWorker for the same shape.
class RadiosWorker : public QThread
{
    Q_OBJECT

public:
    enum Target { WifiTarget = 0, BluetoothTarget = 1 };

    ~RadiosWorker() override { stop(); }

    void postToggle(int target, bool on);
    void stop();

signals:
    void stateArrived(bool wifiAvailable, bool wifiOn, bool bluetoothAvailable,
                      bool bluetoothOn);
    void deniedArrived(int target);

protected:
    void run() override;

private:
    struct Request {
        int target = WifiTarget;
        bool on = false;
        int attempts = 0; // cycles spent without the radio kind showing up
    };

    QMutex m_mutex;
    QWaitCondition m_wake;
    QList<Request> m_requests;
    // Atomic, not mutex-guarded: it is also read inside the polling loop's
    // WinRT waits, which must abort the moment shutdown is requested.
    std::atomic<bool> m_stop{false};
};
