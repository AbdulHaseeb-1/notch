#include "AudioDevices.h"

#include <QPointer>
#include <QTimer>

#ifdef Q_OS_WIN

#  include "WinRtAbi.h"

#  include <algorithm>

#  include <mmdeviceapi.h>
#  include <endpointvolume.h>
#  include <propvarutil.h>

using nochwin::ComPtr;

namespace {

constexpr int kPollMs = 10000;   // device-list fallback; volume and mute are pushed

// {BCDE0395-E52F-467C-8E3D-C4579291692E}
constexpr GUID kClsidMmDeviceEnumerator{
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
// {A95664D2-9614-4F35-A746-DE8DB63617E6}
constexpr GUID kIidMmDeviceEnumerator{
    0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
// {5CDF2C82-841E-4546-9722-0CF74078229A}
constexpr GUID kIidAudioEndpointVolume{
    0x5CDF2C82, 0x841E, 0x4546, {0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A}};
// IAudioEndpointVolumeCallback: pushed volume / mute changes.
constexpr GUID kIidEndpointVolumeCallback{
    0x657804FA, 0xD6AD, 0x4496, {0x8A, 0x60, 0x35, 0x27, 0x52, 0xAF, 0x4F, 0x89}};

// PolicyConfig: the undocumented (but universally used) way to switch the
// default output device. Stable since Windows 7.
// {f8679f50-850a-41cf-9c72-430f290290c8}
constexpr GUID kIidPolicyConfig{
    0xF8679F50, 0x850A, 0x41CF, {0x9C, 0x72, 0x43, 0x0F, 0x29, 0x02, 0x90, 0xC8}};
// {870af99c-171d-4f9e-af0d-e63df40c2bc9} (Windows 10+), {294935CE-F637-4E7C-A41B-AB255460B862} (older)
constexpr GUID kClsidPolicyConfigModern{
    0x870AF99C, 0x171D, 0x4F9E, {0xAF, 0x0D, 0xE6, 0x3D, 0xF4, 0x0C, 0x2B, 0xC9}};
constexpr GUID kClsidPolicyConfigLegacy{
    0x294935CE, 0xF637, 0x4E7C, {0xA4, 0x1B, 0xAB, 0x25, 0x54, 0x60, 0xB8, 0x62}};

// COM interfaces have no virtual destructor by design: they are released
// through IUnknown::Release(). The SDK base class cannot be changed, so keep
// the ABI shim and silence the warning here.
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
struct IPolicyConfig : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, void **) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, BOOL, void **) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, void *, void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, BOOL, INT64 *, INT64 *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, INT64 *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, BOOL) = 0;
};
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

// PKEY_Device_FriendlyName
constexpr PROPERTYKEY kPkeyFriendlyName = {
    {0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};

bool comReady()
{
    static const bool ok = [] {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        // S_FALSE (already in) and RPC_E_CHANGED_MODE (already in, different
        // apartment) both leave COM usable; we simply never uninitialize.
        return hr == S_OK || hr == S_FALSE || hr == RPC_E_CHANGED_MODE;
    }();
    return ok;
}

ComPtr<IMMDeviceEnumerator> createEnumerator()
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (!comReady())
        return enumerator;
    CoCreateInstance(kClsidMmDeviceEnumerator, nullptr, CLSCTX_ALL, kIidMmDeviceEnumerator,
                     reinterpret_cast<void **>(&enumerator.p));
    return enumerator;
}

ComPtr<IPolicyConfig> createPolicyConfig()
{
    ComPtr<IPolicyConfig> policy;
    if (!comReady())
        return policy;
    if (FAILED(CoCreateInstance(kClsidPolicyConfigModern, nullptr, CLSCTX_ALL, kIidPolicyConfig,
                                reinterpret_cast<void **>(&policy.p))))
        CoCreateInstance(kClsidPolicyConfigLegacy, nullptr, CLSCTX_ALL, kIidPolicyConfig,
                         reinterpret_cast<void **>(&policy.p));
    return policy;
}

QString deviceId(IMMDevice *device)
{
    LPWSTR raw = nullptr;
    if (!device || FAILED(device->GetId(&raw)) || !raw)
        return {};
    const QString id = QString::fromWCharArray(raw);
    CoTaskMemFree(raw);
    return id;
}

QString deviceName(IMMDevice *device)
{
    ComPtr<IPropertyStore> store;
    if (!device || FAILED(device->OpenPropertyStore(STGM_READ, &store.p)))
        return {};
    PROPVARIANT value;
    PropVariantInit(&value);
    QString name;
    if (SUCCEEDED(store->GetValue(kPkeyFriendlyName, &value)) && value.vt == VT_LPWSTR
        && value.pwszVal)
        name = QString::fromWCharArray(value.pwszVal);
    PropVariantClear(&value);
    return name;
}

} // namespace

// Volume and mute come to us instead of being re-read every two seconds: the
// endpoint registry owns the change events. OnNotify marshals back on to the
// GUI thread, since the audio system calls this from its own worker.
// As above, COM lifetime is Release(), not a virtual destructor.
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
class EndpointVolumeCallback final : public IAudioEndpointVolumeCallback
{
public:
    explicit EndpointVolumeCallback(QPointer<AudioDevices> owner)
        : m_owner(owner)
    {
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG left = --m_ref;
        if (left == 0)
            delete this;
        return left;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (riid == IID_IUnknown)
            *out = static_cast<IUnknown *>(this);
        else if (riid == kIidEndpointVolumeCallback)
            *out = static_cast<IAudioEndpointVolumeCallback *>(this);
        else
            return E_NOINTERFACE;
        ++m_ref;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override
    {
        if (auto *owner = m_owner.data())
            QMetaObject::invokeMethod(owner, "poll", Qt::QueuedConnection);
        return S_OK;
    }

private:
    QPointer<AudioDevices> m_owner;
    ULONG m_ref = 1;
};
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

AudioDevices::AudioDevices(QObject *parent)
    : QObject(parent)
{
    connect(&m_poll, &QTimer::timeout, this, &AudioDevices::poll);
    m_poll.start(kPollMs);
    QTimer::singleShot(0, this, &AudioDevices::poll);
}

AudioDevices::~AudioDevices()
{
#ifdef Q_OS_WIN
    detachHook(m_renderHook);
    detachHook(m_captureHook);
#endif
}

void AudioDevices::attachHookFor(IMMDevice *device, VolumeHook &hook)
{
    const QString id = deviceId(device);
    if (id.isEmpty())
        return;
    // Still on the same endpoint: nothing to do.
    if (hook.endpoint && hook.endpointId == id)
        return;
    detachHook(hook);

    ComPtr<IAudioEndpointVolume> volume;
    if (FAILED(device->Activate(kIidAudioEndpointVolume, CLSCTX_ALL, nullptr,
                                reinterpret_cast<void **>(&volume.p))))
        return;
    auto *callback = new EndpointVolumeCallback(this);
    if (FAILED(volume->RegisterControlChangeNotify(callback))) {
        callback->Release();
        return;
    }
    // The registration stays live as long as the volume interface lives, and
    // the callback carries our ref; both are handed over to the hook.
    hook.endpointId = id;
    hook.endpoint = volume.detach();
    hook.callback = callback;
}

void AudioDevices::detachHook(VolumeHook &hook)
{
    auto *volume = reinterpret_cast<IAudioEndpointVolume *>(hook.endpoint);
    auto *callback = reinterpret_cast<IAudioEndpointVolumeCallback *>(hook.callback);
    if (volume) {
        if (callback)
            volume->UnregisterControlChangeNotify(callback);
        volume->Release();
    }
    if (callback)
        callback->Release();
    hook.endpointId.clear();
    hook.endpoint = nullptr;
    hook.callback = nullptr;
}

void AudioDevices::poll()
{
    refresh();
}

void AudioDevices::refresh()
{
    const ComPtr<IMMDeviceEnumerator> enumerator = createEnumerator();
    if (!enumerator)
        return;

    QString defaultId;
    {
        ComPtr<IMMDevice> device;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device.p)))
            defaultId = deviceId(device.get());
    }

    QVariantList list;
    ComPtr<IMMDeviceCollection> devices;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices.p))) {
        UINT count = 0;
        devices->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device;
            if (FAILED(devices->Item(i, &device.p)))
                continue;
            const QString id = deviceId(device.get());
            if (id.isEmpty())
                continue;
            QString name = deviceName(device.get());
            if (name.isEmpty())
                name = tr("Output");
            list.append(QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("name"), name},
                {QStringLiteral("isDefault"), id == defaultId},
            });
        }
    }

    std::stable_sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap left = a.toMap();
        const QVariantMap right = b.toMap();
        if (left.value(QStringLiteral("isDefault")).toBool()
            != right.value(QStringLiteral("isDefault")).toBool())
            return left.value(QStringLiteral("isDefault")).toBool();
        return left.value(QStringLiteral("name")).toString()
                .compare(right.value(QStringLiteral("name")).toString(), Qt::CaseInsensitive)
            < 0;
    });

    if (list != m_outputs) {
        m_outputs = list;
        emit outputsChanged();
    }

    bool hasMic = false;
    bool muted = false;
    {
        ComPtr<IMMDevice> mic;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &mic.p))) {
            ComPtr<IAudioEndpointVolume> volume;
            if (SUCCEEDED(mic->Activate(kIidAudioEndpointVolume, CLSCTX_ALL, nullptr,
                                        reinterpret_cast<void **>(&volume.p)))) {
                BOOL value = FALSE;
                if (SUCCEEDED(volume->GetMute(&value))) {
                    hasMic = true;
                    muted = value != FALSE;
                }
            }
        }
    }
    if (hasMic != m_hasMic || muted != m_micMuted) {
        m_hasMic = hasMic;
        m_micMuted = muted;
        emit micChanged();
    }

    qreal masterVol = m_masterVolume;
    bool masterMute = m_masterMuted;
    {
        ComPtr<IMMDevice> speaker;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &speaker.p))) {
            ComPtr<IAudioEndpointVolume> volume;
            if (SUCCEEDED(speaker->Activate(kIidAudioEndpointVolume, CLSCTX_ALL, nullptr,
                                            reinterpret_cast<void **>(&volume.p)))) {
                float level = 0.8f;
                if (SUCCEEDED(volume->GetMasterVolumeLevelScalar(&level)))
                    masterVol = qBound(0.0, qreal(level), 1.0);
                BOOL m = FALSE;
                if (SUCCEEDED(volume->GetMute(&m)))
                    masterMute = (m != FALSE);
            }
        }
    }
    if (qAbs(masterVol - m_masterVolume) > 0.005 || masterMute != m_masterMuted) {
        m_masterVolume = masterVol;
        m_masterMuted = masterMute;
        emit masterVolumeChanged();
    }

    // Keep the volume/mute notifications wired to whichever endpoints the
    // system is defaulting to right now.
    {
        ComPtr<IMMDevice> speaker;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &speaker.p)))
            attachHookFor(speaker.get(), m_renderHook);
        ComPtr<IMMDevice> mic;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &mic.p)))
            attachHookFor(mic.get(), m_captureHook);
    }
}

bool AudioDevices::setDefault(const QString &id)
{
    if (id.isEmpty())
        return false;

    const ComPtr<IPolicyConfig> policy = createPolicyConfig();
    if (!policy)
        return false;

    const std::wstring wide = id.toStdWString();
    bool ok = false;
    for (const ERole role : {eConsole, eMultimedia, eCommunications})
        ok = SUCCEEDED(policy->SetDefaultEndpoint(wide.c_str(), role)) || ok;

    if (ok)
        refresh();
    return ok;
}

void AudioDevices::toggleMicMute()
{
    const ComPtr<IMMDeviceEnumerator> enumerator = createEnumerator();
    if (!enumerator)
        return;
    ComPtr<IMMDevice> mic;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &mic.p)))
        return;
    ComPtr<IAudioEndpointVolume> volume;
    if (FAILED(mic->Activate(kIidAudioEndpointVolume, CLSCTX_ALL, nullptr,
                             reinterpret_cast<void **>(&volume.p))))
        return;
    BOOL muted = FALSE;
    if (FAILED(volume->GetMute(&muted)))
        return;
    if (FAILED(volume->SetMute(!muted, nullptr)))
        return;
    refresh();
}

void AudioDevices::setMasterVolume(qreal vol)
{
    vol = qBound(0.0, vol, 1.0);
    const ComPtr<IMMDeviceEnumerator> enumerator = createEnumerator();
    if (!enumerator)
        return;
    ComPtr<IMMDevice> speaker;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &speaker.p)))
        return;
    ComPtr<IAudioEndpointVolume> volume;
    if (FAILED(speaker->Activate(kIidAudioEndpointVolume, CLSCTX_ALL, nullptr,
                                 reinterpret_cast<void **>(&volume.p))))
        return;
    if (SUCCEEDED(volume->SetMasterVolumeLevelScalar(float(vol), nullptr))) {
        if (m_masterMuted && vol > 0.005) {
            volume->SetMute(FALSE, nullptr);
            m_masterMuted = false;
        }
        m_masterVolume = vol;
        emit masterVolumeChanged();
    }
}

void AudioDevices::toggleMasterMute()
{
    const ComPtr<IMMDeviceEnumerator> enumerator = createEnumerator();
    if (!enumerator)
        return;
    ComPtr<IMMDevice> speaker;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &speaker.p)))
        return;
    ComPtr<IAudioEndpointVolume> volume;
    if (FAILED(speaker->Activate(kIidAudioEndpointVolume, CLSCTX_ALL, nullptr,
                                 reinterpret_cast<void **>(&volume.p))))
        return;
    BOOL muted = FALSE;
    if (FAILED(volume->GetMute(&muted)))
        return;
    if (SUCCEEDED(volume->SetMute(!muted, nullptr))) {
        m_masterMuted = !muted;
        emit masterVolumeChanged();
    }
}

#else // !Q_OS_WIN

AudioDevices::AudioDevices(QObject *parent)
    : QObject(parent)
{
}

AudioDevices::~AudioDevices() = default;

void AudioDevices::attachHookFor(IMMDevice *, VolumeHook &) {}

void AudioDevices::detachHook(VolumeHook &) {}

void AudioDevices::poll() {}

void AudioDevices::refresh() {}

bool AudioDevices::setDefault(const QString &id)
{
    Q_UNUSED(id)
    return false;
}

void AudioDevices::toggleMicMute() {}

void AudioDevices::setMasterVolume(qreal) {}

void AudioDevices::toggleMasterMute() {}

#endif // Q_OS_WIN
