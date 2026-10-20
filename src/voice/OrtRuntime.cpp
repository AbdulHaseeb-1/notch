#include "OrtRuntime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>

// MinGW defines _WIN32, so ONNX Runtime's C API expects the Windows SDK's
// <specstrings.h> to provide this SAL annotation. MinGW's version does not.
#if defined(__MINGW32__) && !defined(_Frees_ptr_opt_)
#define _Frees_ptr_opt_
#endif
#include "onnxruntime_c_api.h"

namespace ortrt {

namespace {

struct Global {
    QMutex mutex;
    QLibrary lib;
    const OrtApi *api = nullptr;
    OrtEnv *env = nullptr;
    bool tried = false;
    bool ok = false;
    QString error;
    QString version;
};

Global &g()
{
    static Global instance;
    return instance;
}

// Frees an OrtStatus and returns its message ("" for success).
QString take(const OrtApi *api, OrtStatus *status)
{
    if (!status)
        return QString();
    const QString message = QString::fromUtf8(api->GetErrorMessage(status));
    api->ReleaseStatus(status);
    return message.isEmpty() ? QStringLiteral("unknown error") : message;
}

QStringList candidates(const QStringList &dirs)
{
    QStringList out;
    const QString forced = qEnvironmentVariable("NOCH_ORT_LIBRARY");
    if (!forced.isEmpty()) {
        out << forced;
        return out;
    }
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("onnxruntime.dll");
#elif defined(Q_OS_MACOS)
    const QString name = QStringLiteral("libonnxruntime.dylib");
#else
    const QString name = QStringLiteral("libonnxruntime.so");
#endif
    for (const QString &dir : dirs) {
        if (dir.isEmpty())
            continue;
        const QString path = QDir(dir).filePath(name);
        if (QFileInfo::exists(path))
            out << path;
    }
    out << name;   // the loader's own search path last
    return out;
}

} // namespace

bool load(const QStringList &dirs, QString *error)
{
    Global &s = g();
    QMutexLocker lock(&s.mutex);
    if (s.tried) {
        if (!s.ok && error)
            *error = s.error;
        return s.ok;
    }
    s.tried = true;

    QString last;
    for (const QString &path : candidates(dirs)) {
        s.lib.setFileName(path);
        if (s.lib.load())
            break;
        last = s.lib.errorString();
    }
    if (!s.lib.isLoaded()) {
        s.error = QObject::tr("The ONNX Runtime library (onnxruntime.dll) was not found next to Noch, or could not be started "
                              "(it needs the Microsoft Visual C++ Redistributable).");
        if (!last.isEmpty())
            s.error += QLatin1Char(' ') + last;
        if (error)
            *error = s.error;
        return false;
    }

    using GetBase = const OrtApiBase *(ORT_API_CALL *)();
    auto getBase = reinterpret_cast<GetBase>(s.lib.resolve("OrtGetApiBase"));
    const OrtApiBase *base = getBase ? getBase() : nullptr;
    // 17 is old enough for any runtime still in use and has every call used here.
    s.api = base ? base->GetApi(17) : nullptr;
    if (!s.api) {
        s.error = QObject::tr("This onnxruntime library is too old (needs 1.17 or newer).");
        s.lib.unload();
        if (error)
            *error = s.error;
        return false;
    }
    s.version = QString::fromUtf8(base->GetVersionString());
    if (const QString e = take(s.api, s.api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "noch", &s.env)); !e.isEmpty()) {
        s.error = QObject::tr("ONNX Runtime did not start: %1").arg(e);
        s.api = nullptr;
        if (error)
            *error = s.error;
        return false;
    }
    s.ok = true;
    return true;
}

bool loaded()
{
    Global &s = g();
    QMutexLocker lock(&s.mutex);
    return s.ok;
}

QString version()
{
    Global &s = g();
    QMutexLocker lock(&s.mutex);
    return s.version;
}

// ---- Session ---------------------------------------------------------------------------------

struct Session::Impl {
    OrtSession *session = nullptr;
    OrtMemoryInfo *memory = nullptr;
    std::string inName;
    std::string outName;
};

Session::Session() : d(new Impl) {}

Session::~Session()
{
    const OrtApi *api = g().api;
    if (api && d) {
        if (d->session)
            api->ReleaseSession(d->session);
        if (d->memory)
            api->ReleaseMemoryInfo(d->memory);
    }
}

bool Session::isOpen() const
{
    return d && d->session;
}

bool Session::open(const QByteArray &model, QString *error)
{
    const OrtApi *api = g().api;
    if (!api || !g().ok) {
        if (error)
            *error = QObject::tr("ONNX Runtime is not loaded.");
        return false;
    }
    QString failure;
    OrtSessionOptions *opts = nullptr;
    auto fail = [&](const QString &why) {
        if (opts)
            api->ReleaseSessionOptions(opts);
        if (error)
            *error = why;
        return false;
    };
    if (const QString e = take(api, api->CreateSessionOptions(&opts)); !e.isEmpty())
        return fail(e);
    // One thread and no spinning: three tiny models on a 12.5 Hz cadence must not
    // keep cores busy while the machine is otherwise idle.
    (void)take(api, api->SetIntraOpNumThreads(opts, 1));
    (void)take(api, api->SetInterOpNumThreads(opts, 1));
    (void)take(api, api->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL));
    (void)take(api, api->AddSessionConfigEntry(opts, "session.intra_op.allow_spinning", "0"));
    if (const QString e = take(api, api->CreateSessionFromArray(g().env, model.constData(),
                                                                size_t(model.size()), opts, &d->session));
        !e.isEmpty())
        return fail(e);
    api->ReleaseSessionOptions(opts);
    opts = nullptr;

    OrtAllocator *alloc = nullptr;
    if (const QString e = take(api, api->GetAllocatorWithDefaultOptions(&alloc)); !e.isEmpty())
        return fail(e);
    char *in = nullptr;
    char *out = nullptr;
    if (const QString e = take(api, api->SessionGetInputName(d->session, 0, alloc, &in)); !e.isEmpty())
        return fail(e);
    d->inName = in;
    (void)take(api, api->AllocatorFree(alloc, in));
    if (const QString e = take(api, api->SessionGetOutputName(d->session, 0, alloc, &out)); !e.isEmpty())
        return fail(e);
    d->outName = out;
    (void)take(api, api->AllocatorFree(alloc, out));
    if (const QString e = take(api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &d->memory));
        !e.isEmpty())
        return fail(e);
    return true;
}

bool Session::run(const float *data, const std::vector<int64_t> &shape, std::vector<float> *out,
                  std::vector<int64_t> *outShape, QString *error)
{
    const OrtApi *api = g().api;
    if (!api || !isOpen())
        return false;
    size_t count = 1;
    for (int64_t dim : shape)
        count *= size_t(dim);

    OrtValue *input = nullptr;
    OrtValue *output = nullptr;
    auto done = [&](bool ok, const QString &why) {
        if (output)
            api->ReleaseValue(output);
        if (input)
            api->ReleaseValue(input);
        if (!ok && error)
            *error = why;
        return ok;
    };

    if (const QString e = take(api, api->CreateTensorWithDataAsOrtValue(
                                        d->memory, const_cast<float *>(data), count * sizeof(float),
                                        shape.data(), shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input));
        !e.isEmpty())
        return done(false, e);
    const char *inNames[] = { d->inName.c_str() };
    const char *outNames[] = { d->outName.c_str() };
    if (const QString e = take(api, api->Run(d->session, nullptr, inNames, &input, 1, outNames, 1, &output));
        !e.isEmpty())
        return done(false, e);

    OrtTensorTypeAndShapeInfo *info = nullptr;
    if (const QString e = take(api, api->GetTensorTypeAndShape(output, &info)); !e.isEmpty())
        return done(false, e);
    size_t rank = 0;
    (void)take(api, api->GetDimensionsCount(info, &rank));
    outShape->assign(rank, 0);
    (void)take(api, api->GetDimensions(info, outShape->data(), rank));
    size_t elements = 0;
    (void)take(api, api->GetTensorShapeElementCount(info, &elements));
    api->ReleaseTensorTypeAndShapeInfo(info);

    float *values = nullptr;
    if (const QString e = take(api, api->GetTensorMutableData(output, reinterpret_cast<void **>(&values)));
        !e.isEmpty() || !values)
        return done(false, QStringLiteral("no output"));
    out->assign(values, values + elements);
    return done(true, QString());
}

bool Session::runNamed(const std::vector<TensorIn> &inputs, const std::vector<std::string> &outputs,
                       std::vector<TensorOut> *results, QString *error)
{
    const OrtApi *api = g().api;
    if (!api || !isOpen())
        return false;
    static const float kEmpty = 0.0f;
    std::vector<OrtValue *> in(inputs.size(), nullptr);
    std::vector<OrtValue *> out(outputs.size(), nullptr);
    auto done = [&](bool ok, const QString &why) {
        for (OrtValue *v : out)
            if (v)
                api->ReleaseValue(v);
        for (OrtValue *v : in)
            if (v)
                api->ReleaseValue(v);
        if (!ok && error)
            *error = why;
        return ok;
    };

    std::vector<const char *> inNames, outNames;
    for (size_t i = 0; i < inputs.size(); ++i) {
        const TensorIn &t = inputs[i];
        size_t count = 1;
        for (int64_t dim : t.shape)
            count *= size_t(dim);
        size_t bytes = count;
        ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL;
        switch (t.type) {
        case DType::Float32: bytes = count * sizeof(float); type = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT; break;
        case DType::Int64: bytes = count * sizeof(int64_t); type = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64; break;
        case DType::Bool: break;
        }
        void *data = const_cast<void *>(count == 0 || !t.data ? static_cast<const void *>(&kEmpty) : t.data);
        if (const QString e = take(api, api->CreateTensorWithDataAsOrtValue(d->memory, data, bytes, t.shape.data(),
                                                                            t.shape.size(), type, &in[i]));
            !e.isEmpty())
            return done(false, QStringLiteral("%1: %2").arg(QString::fromStdString(t.name), e));
        inNames.push_back(t.name.c_str());
    }
    for (const std::string &name : outputs)
        outNames.push_back(name.c_str());

    if (const QString e = take(api, api->Run(d->session, nullptr, inNames.data(), in.data(), in.size(),
                                             outNames.data(), outNames.size(), out.data()));
        !e.isEmpty())
        return done(false, e);

    results->assign(outputs.size(), TensorOut());
    for (size_t i = 0; i < outputs.size(); ++i) {
        OrtTensorTypeAndShapeInfo *info = nullptr;
        if (const QString e = take(api, api->GetTensorTypeAndShape(out[i], &info)); !e.isEmpty())
            return done(false, e);
        size_t rank = 0;
        size_t elements = 0;
        (void)take(api, api->GetDimensionsCount(info, &rank));
        (*results)[i].shape.assign(rank, 0);
        (void)take(api, api->GetDimensions(info, (*results)[i].shape.data(), rank));
        (void)take(api, api->GetTensorShapeElementCount(info, &elements));
        api->ReleaseTensorTypeAndShapeInfo(info);
        float *values = nullptr;
        if (const QString e = take(api, api->GetTensorMutableData(out[i], reinterpret_cast<void **>(&values)));
            !e.isEmpty())
            return done(false, e);
        if (values)
            (*results)[i].data.assign(values, values + elements);
    }
    return done(true, QString());
}

} // namespace ortrt
