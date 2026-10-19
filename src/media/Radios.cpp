#include "Radios.h"

#include <QMutexLocker>

namespace {
// Idle cadence: plenty for a chip that only has to stay truthful, and much
// less work than a constant fast poll. While a toggle is queued the loop
// wakes quickly, so the click still feels immediate.
constexpr int kIdlePollMs = 10000;
constexpr int kBusyPollMs = 300;
// Give up on a queued toggle when its radio kind keeps not showing up
// (e.g. no Bluetooth adapter) instead of keeping it queued forever.
constexpr int kMaxRadioWaits = 3;
}

#ifdef Q_OS_WIN

#  include "WinRtAbi.h"

using namespace nochwin;

#endif // Q_OS_WIN

Radios::Radios(QObject *parent)
    : QObject(parent)
{
    m_worker = new RadiosWorker();
    connect(m_worker, &RadiosWorker::stateArrived, this, &Radios::applyState,
            Qt::QueuedConnection);
    connect(m_worker, &RadiosWorker::deniedArrived, this, [this](int) { emit denied(); },
            Qt::QueuedConnection);
    m_worker->start();
}

Radios::~Radios()
{
    if (m_worker) {
        m_worker->stop();
        if (m_worker->isFinished()) {
            delete m_worker;
        } else {
            // Never delete a QThread that is still running: run() would keep
            // using the destroyed mutex/condition. The loop sees m_stop and
            // exits on its own; the process reclaims the object at exit.
        }
        m_worker = nullptr;
    }
}

void Radios::applyState(bool wifiAvailable, bool wifiOn, bool bluetoothAvailable,
                        bool bluetoothOn)
{
    if (m_wifiAvailable == wifiAvailable && m_wifiOn == wifiOn
        && m_bluetoothAvailable == bluetoothAvailable && m_bluetoothOn == bluetoothOn)
        return;
    m_wifiAvailable = wifiAvailable;
    m_wifiOn = wifiOn;
    m_bluetoothAvailable = bluetoothAvailable;
    m_bluetoothOn = bluetoothOn;
    emit changed();
}

void Radios::toggleWifi()
{
    if (m_worker)
        m_worker->postToggle(RadiosWorker::WifiTarget, !m_wifiOn);
}

void Radios::toggleBluetooth()
{
    if (m_worker)
        m_worker->postToggle(RadiosWorker::BluetoothTarget, !m_bluetoothOn);
}

// ------------------------------------------------------------------ worker
void RadiosWorker::postToggle(int target, bool on)
{
    QMutexLocker lock(&m_mutex);
    m_requests.append({target, on, 0});
    m_wake.wakeOne();
}

void RadiosWorker::stop()
{
    {
        QMutexLocker lock(&m_mutex);
        m_stop = true;
        m_wake.wakeOne();
    }
    // The run loop checks m_stop inside every WinRT wait, so this only lasts
    // long if a COM object refuses to wake up; 15s is the same bound those
    // waits use.
    wait(15000);
}

void RadiosWorker::run()
{
#ifdef Q_OS_WIN
    if (!winRtApi().valid())
        return;
    if (FAILED(winRtApi().roInitialize(1 /*MULTITHREADED*/)))
        return;
    // Pair RoUninitialize with a successful RoInitialize on every exit path.
    const bool winRTInited = true;

    ComPtr<IRadioStaticsAbi> statics;
    getActivationFactory(L"Windows.Devices.Radios.Radio", kIidRadioStatics, statics.putVoid());

    for (;;) {
        // Peek at the pending requests but keep them around: a request for a
        // radio kind that is not enumerated this cycle stays queued (and is
        // retried next cycle), so a slow adapter cannot silently eat the click.
        const QList<Request> pending = [this]() {
            QMutexLocker lock(&m_mutex);
            return m_requests;
        }();
        if (m_stop)
            break;

        bool wifiAvailable = false;
        bool wifiOn = false;
        bool bluetoothAvailable = false;
        bool bluetoothOn = false;
        // Per pending request: a matching radio was found this cycle / refused.
        QList<char> found(pending.size(), 0);
        QList<char> denied(pending.size(), 0);

        if (statics) {
            ComPtr<IUnknown> radiosOp;
            if (SUCCEEDED(statics->GetRadiosAsync(radiosOp.putVoid()))) {
                ComPtr<IVectorViewAbi<IRadioAbi *>> radios;
                if (SUCCEEDED((operationResult<
                                  IVectorViewAbi<IRadioAbi *> *,
                                  IAsyncOperationAbi<IVectorViewAbi<IRadioAbi *> *>>(
                        radiosOp.get(), radios.put(), &m_stop)))
                    && radios) {
                    uint32_t count = 0;
                    radios->get_Size(&count);
                    for (uint32_t i = 0; i < count; ++i) {
                        ComPtr<IRadioAbi> radio;
                        if (FAILED(radios->GetAt(i, radio.put())))
                            continue;

                        int32_t kind = -1;
                        radio->get_Kind(&kind);

                        bool *available = nullptr;
                        bool *on = nullptr;
                        int target = -1;
                        if (kind == RadioWiFi) {
                            available = &wifiAvailable;
                            on = &wifiOn;
                            target = WifiTarget;
                        } else if (kind == RadioBluetooth) {
                            available = &bluetoothAvailable;
                            on = &bluetoothOn;
                            target = BluetoothTarget;
                        } else {
                            continue;
                        }
                        *available = true;

                        // Apply any queued toggle before reading the state back.
                        for (int j = 0; j < pending.size(); ++j) {
                            if (pending.at(j).target != target)
                                continue;
                            found[j] = 1;
                            ComPtr<IUnknown> op;
                            if (FAILED(radio->SetStateAsync(
                                    pending.at(j).on ? RadioOn : RadioOff, op.putVoid()))) {
                                denied[j] = 1;
                                continue;
                            }
                            if (FAILED(waitForOperation(op.get(), 15000, &m_stop))) {
                                denied[j] = 1;
                                continue;
                            }
                            int32_t status = RadioAccessUnspecified;
                            reinterpret_cast<IAsyncOperationAbi<int32_t> *>(
                                static_cast<IUnknown *>(op.get()))
                                ->GetResults(&status);
                            if (status != RadioAccessAllowed)
                                denied[j] = 1;
                        }

                        int32_t state = RadioUnknown;
                        radio->get_State(&state);
                        *on = state == RadioOn;
                    }
                }
            }
        }

        // Report the outcome once per user action, not once per failing radio.
        for (int j = 0; j < pending.size(); ++j) {
            if (denied.at(j))
                emit deniedArrived(pending.at(j).target);
        }

        // Requests whose radio kind showed up this cycle were applied (or
        // refused) - drop them. Ones still waiting for their adapter get a
        // few retries, then give up loudly so the user hears it once.
        {
            QMutexLocker lock(&m_mutex);
            QList<Request> kept;
            for (const Request &request : m_requests) {
                bool handled = false;
                for (int j = 0; j < pending.size(); ++j) {
                    if (pending.at(j).target == request.target
                        && pending.at(j).on == request.on && found.at(j)) {
                        handled = true;
                        break;
                    }
                }
                if (handled)
                    continue;
                Request next = request;
                next.attempts += 1;
                if (next.attempts >= kMaxRadioWaits) {
                    emit deniedArrived(next.target);
                    continue;
                }
                kept.append(next);
            }
            m_requests = kept;
        }

        emit stateArrived(wifiAvailable, wifiOn, bluetoothAvailable, bluetoothOn);

        // Sleep between polls: busy while a queued toggle waits for its
        // adapter, slow otherwise (the loop no longer uses a fixed 3 s poll).
        {
            QMutexLocker lock(&m_mutex);
            if (!m_stop)
                m_wake.wait(&m_mutex, m_requests.isEmpty() ? kIdlePollMs : kBusyPollMs);
        }
    }

    if (winRTInited)
        winRtApi().roUninitialize();
#else
    QMutexLocker lock(&m_mutex);
    while (!m_stop)
        m_wake.wait(&m_mutex, kIdlePollMs);
#endif
}
