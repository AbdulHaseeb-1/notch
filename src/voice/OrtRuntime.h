#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// A thin, optional binding to ONNX Runtime's C API, used by the local wake-word
// detector.
//
// onnxruntime.dll is loaded when it is first asked for (QLibrary), instead of
// being linked in: Noch builds and starts without it, and the wake word then
// says what is missing instead of failing to launch. The header in
// third_party/onnxruntime is only used for the struct layout; ORT_API_VERSION 17
// is requested, which every newer runtime still serves.
namespace ortrt {

// Loads the runtime from the first directory in `dirs` that has it (then the
// default search path). Safe to call again; later calls return the first result.
// `NOCH_ORT_LIBRARY` (a full path) overrides the search, for diagnostics and tests.
bool load(const QStringList &dirs, QString *error);
bool loaded();
QString version();

// An input for Session::runNamed. `data` stays owned by the caller. A Bool tensor
// is one byte per element (0 or 1). A tensor with a 0 in its shape (an empty
// key/value cache) may pass any non-null pointer.
enum class DType { Float32, Int64, Bool };
struct TensorIn {
    std::string name;
    DType type = DType::Float32;
    std::vector<int64_t> shape;
    const void *data = nullptr;
};
// A float output, copied out of the runtime.
struct TensorOut {
    std::vector<int64_t> shape;
    std::vector<float> data;
};

// One model. run() is the one-float-in, one-float-out case (the three models of
// the openWakeWord pipeline); runNamed() takes any inputs by name and returns
// the float outputs asked for.
class Session
{
public:
    Session();
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    // `model` is the .onnx file's bytes; nothing is read from disk here.
    bool open(const QByteArray &model, QString *error);
    bool isOpen() const;

    // Runs the model on `data` (row-major, shape `shape`); the result lands in
    // `out` with its shape in `outShape`.
    bool run(const float *data, const std::vector<int64_t> &shape, std::vector<float> *out,
             std::vector<int64_t> *outShape, QString *error);

    bool runNamed(const std::vector<TensorIn> &inputs, const std::vector<std::string> &outputs,
                  std::vector<TensorOut> *results, QString *error);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace ortrt
