#pragma once

// Minimal hand-written ABI definitions for the Windows Runtime bits Noch
// needs (SMTC now-playing, Radio for Wi-Fi/Bluetooth). The project builds with
// MinGW, which has no C++/WinRT, so the interfaces are declared here with
// exactly the vtable layouts the Windows SDK generates (checked against the
// SDK's cppwinrt headers), and the WinRT entry points are resolved from
// combase.dll at runtime so no import library is required either way.
//
// Everything here is Windows-only; callers guard with Q_OS_WIN.

#ifdef Q_OS_WIN

#  include <QByteArray>
#  include <QImage>
#  include <QMutex>
#  include <QMutexLocker>
#  include <QString>

#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <objbase.h>

#  include <atomic>
#  include <cstdint>
#  include <cstring>

namespace nochwin {

using HStringHandle = void *; // HSTRING

// ------------------------------------------------------------------ GUIDs
struct Guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];

    bool operator==(const Guid &other) const { return std::memcmp(this, &other, sizeof(Guid)) == 0; }
};

inline constexpr Guid kIidAsyncInfo{0x00000036, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// Windows.Media.Control
inline constexpr Guid kIidSessionStatics{0x2050C4EE, 0x11A0, 0x57DE,
                                         {0xAE, 0xD7, 0xC9, 0x7C, 0x70, 0x33, 0x82, 0x45}};
inline constexpr Guid kIidSessionManager{0xCACE8EAC, 0xE86E, 0x504A,
                                         {0xAB, 0x31, 0x5F, 0xF8, 0xFF, 0x1B, 0xCE, 0x49}};
inline constexpr Guid kIidSession{0x7148C835, 0x9B14, 0x5AE2,
                                  {0xAB, 0x85, 0xDC, 0x9B, 0x1C, 0x14, 0xE1, 0xA8}};
inline constexpr Guid kIidMediaProperties{0x68856CF6, 0xADB4, 0x54B2,
                                          {0xAC, 0x16, 0x05, 0x83, 0x79, 0x07, 0xAC, 0xB6}};
inline constexpr Guid kIidPlaybackInfo{0x94B4B6CF, 0xE8BA, 0x51AD,
                                       {0x87, 0xA7, 0xC1, 0x0A, 0xDE, 0x10, 0x61, 0x27}};
inline constexpr Guid kIidPlaybackControls{0x6501A3E6, 0xBC7A, 0x503A,
                                           {0xBB, 0x1B, 0x68, 0xF1, 0x58, 0xF3, 0xFB, 0x03}};
inline constexpr Guid kIidTimelineProperties{0xEDE34136, 0x6F25, 0x588D,
                                             {0x8E, 0xCF, 0xEA, 0x5B, 0x67, 0x35, 0xAA, 0xA5}};

// Windows.Storage.Streams
inline constexpr Guid kIidBuffer{0x905A0FE0, 0xBC53, 0x11DF,
                                 {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};
inline constexpr Guid kIidBufferFactory{0x71AF914D, 0xC10F, 0x484B,
                                        {0xBC, 0x50, 0x14, 0xBC, 0x62, 0x3B, 0x3A, 0x27}};
inline constexpr Guid kIidBufferByteAccess{0x905A0FEF, 0xBC53, 0x11DF,
                                           {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};
inline constexpr Guid kIidRandomAccessStreamReference{0x33EE3134, 0x1DD6, 0x4E3A,
                                                      {0x80, 0x67, 0xD1, 0xC1, 0x62, 0xE8, 0x64, 0x2B}};
inline constexpr Guid kIidRandomAccessStream{0x905A0FE1, 0xBC53, 0x11DF,
                                             {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};
inline constexpr Guid kIidInputStream{0x905A0FE2, 0xBC53, 0x11DF,
                                      {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};

// Windows.Devices.Radios
inline constexpr Guid kIidRadioStatics{0x5FB6A12E, 0x67CB, 0x46AE,
                                       {0xAA, 0xE9, 0x65, 0x91, 0x9F, 0x86, 0xEF, 0xF4}};
inline constexpr Guid kIidRadio{0x252118DF, 0xB33E, 0x416A,
                                {0x87, 0x5F, 0x1C, 0xF3, 0x8A, 0xE2, 0xD8, 0x3E}};

// ------------------------------------------------------------------ COM types
// MinGW's inspectable.h is not pulled in by windows.h, so the WinRT root
// interface is declared here with the exact three-method tail the ABI has.
struct IInspectableAbi : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetIids(uint32_t *count, GUID **iids) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(void **name) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int32_t *level) = 0;
};

struct IAsyncInfoAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE get_Id(uint32_t *id) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(int32_t *status) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT *error) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

template <typename TResult>
struct IAsyncOperationAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void **handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(TResult *result) = 0;
};

template <typename TResult>
struct IAsyncOperationWithProgressAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE put_Progress(void *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Progress(void **handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void **handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(TResult *result) = 0;
};

template <typename T>
struct IVectorViewAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE GetAt(uint32_t index, T *item) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(uint32_t *size) = 0;
    virtual HRESULT STDMETHODCALLTYPE IndexOf(T value, uint32_t *index, bool *found) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(uint32_t startIndex, uint32_t capacity, T *items,
                                              uint32_t *actual) = 0;
};

struct IBufferAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE get_Capacity(uint32_t *capacity) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Length(uint32_t *length) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Length(uint32_t length) = 0;
};

struct IBufferFactoryAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE Create(uint32_t capacity, void **buffer) = 0;
};

struct IBufferByteAccessAbi : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Buffer(uint8_t **value) = 0;
};

struct IRandomAccessStreamReferenceAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE OpenReadAsync(void **operation) = 0;
};

struct IRandomAccessStreamAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE get_Size(uint64_t *size) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Size(uint64_t size) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetInputStreamAt(uint64_t position, void **stream) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetOutputStreamAt(uint64_t position, void **stream) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Position(uint64_t *position) = 0;
    virtual HRESULT STDMETHODCALLTYPE Seek(uint64_t position) = 0;
    virtual HRESULT STDMETHODCALLTYPE CloneStream(void **stream) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CanRead(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CanWrite(bool *value) = 0;
};

struct IInputStreamAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE ReadAsync(void *buffer, uint32_t count, uint32_t options,
                                                void **operation) = 0;
};

// Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties
struct IMediaPropertiesAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE get_Title(HStringHandle *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Subtitle(HStringHandle *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(HStringHandle *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Artist(HStringHandle *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTitle(HStringHandle *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TrackNumber(int32_t *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Genres(void **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTrackCount(int32_t *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackType(void **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Thumbnail(void **value) = 0;
};

struct IPlaybackControlsAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE get_IsPlayEnabled(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPauseEnabled(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsStopEnabled(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRecordEnabled(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsFastForwardEnabled(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRewindEnabled(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsNextEnabled(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPreviousEnabled(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelUpEnabled(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelDownEnabled(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPlayPauseToggleEnabled(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsShuffleEnabled(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRepeatEnabled(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPlaybackRateEnabled(bool *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPlaybackPositionEnabled(bool *value) = 0;
};

struct IPlaybackInfoAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE get_Controls(void **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(int32_t *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackType(void **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AutoRepeatMode(void **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackRate(void **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsShuffleActive(void **value) = 0;
};

struct ITimelinePropertiesAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE get_StartTime(int64_t *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_EndTime(int64_t *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MinSeekTime(int64_t *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxSeekTime(int64_t *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Position(int64_t *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_LastUpdatedTime(int64_t *value) = 0;
};

// GlobalSystemMediaTransportControlsSession
struct ISessionAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE get_SourceAppUserModelId(HStringHandle *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetMediaPropertiesAsync(void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTimelineProperties(void **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPlaybackInfo(void **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryPlayAsync(void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryPauseAsync(void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryStopAsync(void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryRecordAsync(void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryFastForwardAsync(void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryRewindAsync(void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TrySkipNextAsync(void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TrySkipPreviousAsync(void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeChannelUpAsync(void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeChannelDownAsync(void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryTogglePlayPauseAsync(void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeAutoRepeatModeAsync(int32_t mode, void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangePlaybackRateAsync(double rate, void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeShuffleActiveAsync(bool active, void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangePlaybackPositionAsync(int64_t ticks,
                                                                     void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_TimelinePropertiesChanged(void *handler,
                                                                    int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TimelinePropertiesChanged(int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PlaybackInfoChanged(void *handler, int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PlaybackInfoChanged(int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_MediaPropertiesChanged(void *handler, int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_MediaPropertiesChanged(int64_t token) = 0;
};

// GlobalSystemMediaTransportControlsSessionManager
struct ISessionManagerAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSession(void **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSessions(void **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_CurrentSessionChanged(void *handler, int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_CurrentSessionChanged(int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_SessionsChanged(void *handler, int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_SessionsChanged(int64_t token) = 0;
};

struct ISessionManagerStaticsAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE RequestAsync(void **operation) = 0;
};

// Windows.Devices.Radios
struct IRadioAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE SetStateAsync(int32_t state, void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_StateChanged(void *handler, int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_StateChanged(int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_State(int32_t *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Name(HStringHandle *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Kind(int32_t *value) = 0;
};

struct IRadioStaticsAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE GetRadiosAsync(void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceSelector(HStringHandle *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE FromIdAsync(HStringHandle id, void **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestAccessAsync(void **operation) = 0;
};

// ------------------------------------------------------------------ enums
enum AsyncStatus { AsyncStarted = 0, AsyncCompleted = 1, AsyncCanceled = 2, AsyncError = 3 };
enum PlaybackStatus {
    PlaybackClosed = 0,
    PlaybackOpened = 1,
    PlaybackChanging = 2,
    PlaybackStopped = 3,
    PlaybackPlaying = 4,
    PlaybackPaused = 5,
};
enum RadioKindValue { RadioOther = 0, RadioWiFi = 1, RadioMobileBroadband = 2, RadioBluetooth = 3, RadioFM = 4 };
enum RadioStateValue { RadioUnknown = 0, RadioOn = 1, RadioOff = 2, RadioDisabled = 3 };
enum RadioAccessValue { RadioAccessUnspecified = 0, RadioAccessAllowed = 1, RadioAccessDeniedByUser = 2, RadioAccessDeniedBySystem = 3 };

// ------------------------------------------------------------------ loader
using RoInitializeFn = HRESULT(WINAPI *)(int initType);
using RoUninitializeFn = void(WINAPI *)();
using RoGetActivationFactoryFn = HRESULT(WINAPI *)(HStringHandle className, const Guid &iid, void **factory);
using WindowsCreateStringFn = HRESULT(WINAPI *)(const wchar_t *source, uint32_t length, HStringHandle *value);
using WindowsDeleteStringFn = HRESULT(WINAPI *)(HStringHandle value);
using WindowsGetStringRawBufferFn = const wchar_t *(WINAPI *)(HStringHandle value, uint32_t *length);

// combase.dll hosts the WinRT entry points and the HSTRING helpers. Loading
// them by hand keeps the build toolchain-neutral.
struct WinRtApi {
    HMODULE module = nullptr;
    RoInitializeFn roInitialize = nullptr;
    RoUninitializeFn roUninitialize = nullptr;
    RoGetActivationFactoryFn roGetActivationFactory = nullptr;
    WindowsCreateStringFn createString = nullptr;
    WindowsDeleteStringFn deleteString = nullptr;
    WindowsGetStringRawBufferFn stringRawBuffer = nullptr;

    bool valid() const { return module && roInitialize && roGetActivationFactory && createString
                                && deleteString && stringRawBuffer; }
};

inline WinRtApi &winRtApi()
{
    static WinRtApi api = [] {
        WinRtApi a;
        HMODULE mod = LoadLibraryW(L"combase.dll");
        if (!mod)
            return a;
        a.module = mod;
        a.roInitialize = reinterpret_cast<RoInitializeFn>(GetProcAddress(mod, "RoInitialize"));
        a.roUninitialize = reinterpret_cast<RoUninitializeFn>(GetProcAddress(mod, "RoUninitialize"));
        a.roGetActivationFactory = reinterpret_cast<RoGetActivationFactoryFn>(GetProcAddress(mod, "RoGetActivationFactory"));
        a.createString = reinterpret_cast<WindowsCreateStringFn>(GetProcAddress(mod, "WindowsCreateString"));
        a.deleteString = reinterpret_cast<WindowsDeleteStringFn>(GetProcAddress(mod, "WindowsDeleteString"));
        a.stringRawBuffer = reinterpret_cast<WindowsGetStringRawBufferFn>(GetProcAddress(mod, "WindowsGetStringRawBuffer"));
        return a;
    }();
    return api;
}

// ------------------------------------------------------------------ helpers
template <typename T>
struct ComPtr {
    T *p = nullptr;

    ComPtr() = default;
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    ComPtr(ComPtr &&other) noexcept
        : p(other.p)
    {
        other.p = nullptr;
    }
    ComPtr &operator=(ComPtr &&other) noexcept
    {
        if (this != &other) {
            reset();
            p = other.p;
            other.p = nullptr;
        }
        return *this;
    }

    ~ComPtr() { reset(); }

    T *operator->() const { return p; }
    T *get() const { return p; }
    explicit operator bool() const { return p != nullptr; }

    void reset()
    {
        if (p) {
            p->Release();
            p = nullptr;
        }
    }

    T **put()
    {
        reset();
        return &p;
    }

    void **putVoid() { return reinterpret_cast<void **>(put()); }

    T *detach()
    {
        T *out = p;
        p = nullptr;
        return out;
    }
};

inline HRESULT qi(void *object, const Guid &iid, void **out)
{
    if (!object)
        return E_POINTER;
    return static_cast<IUnknown *>(object)->QueryInterface(
        reinterpret_cast<const IID &>(iid), out);
}

// Owns an HSTRING.
struct HString {
    HStringHandle value = nullptr;

    HString() = default;
    HString(const HString &) = delete;
    HString &operator=(const HString &) = delete;

    ~HString()
    {
        if (value)
            winRtApi().deleteString(value);
    }

    bool set(const QString &text)
    {
        if (!winRtApi().valid())
            return false;
        if (value) {
            winRtApi().deleteString(value);
            value = nullptr;
        }
        const std::wstring wide = text.toStdWString();
        return SUCCEEDED(winRtApi().createString(
            wide.c_str(), static_cast<uint32_t>(wide.size()), &value));
    }
};

inline QString fromHString(HStringHandle handle)
{
    if (!handle || !winRtApi().valid())
        return {};
    uint32_t length = 0;
    const wchar_t *raw = winRtApi().stringRawBuffer(handle, &length);
    if (!raw || length == 0)
        return {};
    return QString::fromWCharArray(raw, static_cast<int>(length));
}

// Reads and owns the contents of an HSTRING out-parameter.
inline QString takeHString(HStringHandle *slot)
{
    if (!slot || !*slot)
        return {};
    const QString text = fromHString(*slot);
    if (winRtApi().valid()) // defensive: never call through a null entry point
        winRtApi().deleteString(*slot);
    *slot = nullptr;
    return text;
}

// Blocks until the WinRT operation finishes. Only call from a thread with no
// message pump to serve (a worker thread): it polls, which is the one way to
// wait on IAsyncInfo from plain C.
//
// `cancel` lets a worker that is shutting down abort the wait instead of
// parking for the whole timeout; without it the poll loop would add up to
// `timeoutMs` to the teardown window of the owning thread.
inline HRESULT waitForOperation(IUnknown *operation, DWORD timeoutMs = 15000,
                                std::atomic<bool> *cancel = nullptr)
{
    if (!operation)
        return E_POINTER;

    ComPtr<IAsyncInfoAbi> info;
    HRESULT hr = qi(operation, kIidAsyncInfo, reinterpret_cast<void **>(&info.p));
    if (FAILED(hr))
        return hr;

    const DWORD start = GetTickCount();
    for (;;) {
        int32_t status = 0;
        hr = info->get_Status(&status);
        if (FAILED(hr))
            return hr;
        if (status == AsyncCompleted)
            return S_OK;
        if (status == AsyncCanceled)
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        if (status == AsyncError) {
            HRESULT error = E_FAIL;
            info->get_ErrorCode(&error);
            return error;
        }
        if (GetTickCount() - start > timeoutMs)
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        if (cancel && cancel->load(std::memory_order_relaxed))
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        Sleep(2);
    }
}

// Waits for a completed IAsyncOperation<T> and takes its result.
template <typename T, typename OpAbi>
HRESULT operationResult(void *operation, T *out, std::atomic<bool> *cancel = nullptr)
{
    auto *op = static_cast<IUnknown *>(operation);
    HRESULT hr = waitForOperation(op, 15000, cancel);
    if (FAILED(hr))
        return hr;
    return reinterpret_cast<OpAbi *>(op)->GetResults(out);
}

inline HRESULT getActivationFactory(const wchar_t *className, const Guid &iid, void **factory)
{
    if (!winRtApi().valid())
        return E_NOTIMPL;
    HString name;
    const std::wstring wide = className;
    if (!winRtApi().createString(wide.c_str(), static_cast<uint32_t>(wide.size()), &name.value))
        return E_FAIL;
    return winRtApi().roGetActivationFactory(name.value, iid, factory);
}

// Opens an IRandomAccessStreamReference (a thumbnail) and reads it whole.
inline QByteArray readStreamReference(IInspectableAbi *reference, std::atomic<bool> *cancel = nullptr)
{
    QByteArray data;
    if (!reference)
        return data;

    auto *ref = reinterpret_cast<IRandomAccessStreamReferenceAbi *>(reference);
    ComPtr<IUnknown> openOp;
    if (FAILED(ref->OpenReadAsync(openOp.putVoid())))
        return data;

    ComPtr<IRandomAccessStreamAbi> stream;
    if (FAILED((operationResult<IRandomAccessStreamAbi *,
                                IAsyncOperationAbi<IRandomAccessStreamAbi *>>(
            openOp.get(), stream.put(), cancel))))
        return data;

    uint64_t size = 0;
    if (FAILED(stream->get_Size(&size)) || size == 0 || size > 32 * 1024 * 1024)
        return data;

    ComPtr<IInputStreamAbi> input;
    if (FAILED(stream->GetInputStreamAt(0, input.putVoid())))
        return data;

    ComPtr<IBufferFactoryAbi> bufferFactory;
    if (FAILED(getActivationFactory(L"Windows.Storage.Streams.Buffer", kIidBufferFactory,
                                    bufferFactory.putVoid())))
        return data;

    data.reserve(static_cast<int>(size));
    uint64_t read = 0;
    while (read < size) {
        const uint32_t chunk = static_cast<uint32_t>(size - read);

        // A fresh buffer per read: ReadAsync fills it from the start.
        ComPtr<IBufferAbi> buffer;
        if (FAILED(bufferFactory->Create(chunk, buffer.putVoid())))
            return {};
        ComPtr<IUnknown> readOp;
        if (FAILED(input->ReadAsync(buffer.get(), chunk, 0 /*InputStreamOptions::None*/,
                                    readOp.putVoid())))
            return {};
        if (FAILED(waitForOperation(readOp.get(), 15000, cancel)))
            return {};

        uint32_t got = 0;
        if (FAILED(buffer->get_Length(&got)) || got == 0)
            break;

        ComPtr<IBufferByteAccessAbi> access;
        if (FAILED(qi(buffer.get(), kIidBufferByteAccess, reinterpret_cast<void **>(&access.p))))
            return {};
        uint8_t *bytes = nullptr;
        if (FAILED(access->Buffer(&bytes)) || !bytes)
            return {};

        data.append(reinterpret_cast<const char *>(bytes), static_cast<int>(got));
        read += got;
    }
    return data;
}

} // namespace nochwin

#endif // Q_OS_WIN
