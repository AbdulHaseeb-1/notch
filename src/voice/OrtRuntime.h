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

// One model with a single float input and a single float output (the three
// models of the openWakeWord pipeline).
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

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace ortrt
