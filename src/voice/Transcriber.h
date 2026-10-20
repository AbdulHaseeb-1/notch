#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantList>

#include <memory>

// A speech-to-text engine. Noch ships one, Moonshine Tiny, which runs on this
// computer; the interface exists so tests can stand in a scripted one.
//
// prepare() and transcribe() run on VoiceInput's worker thread and may take a
// while; problem() is a cheap check and is safe to call from any thread.
class Transcriber
{
public:
    virtual ~Transcriber() = default;
    virtual QString name() const = 0;
    // Why it cannot be used ("" when it looks usable), e.g. the model files are missing.
    virtual QString problem() const = 0;
    // Loads the model. "" on success, otherwise why not.
    virtual QString prepare() = 0;
    // 16 kHz mono 16-bit PCM in; the words out ("" for no words). A failure sets `error`.
    virtual QString transcribe(const QByteArray &pcm16, QString *error) = 0;
    // The model files the engine needs, for the settings to show their state:
    // a list of {name, path, found, bytes} maps. "" list when there are none.
    virtual QVariantList modelFiles() const { return {}; }
};

// Moonshine Tiny on this computer (see Moonshine.h). `folder` is where the user can
// put the model files; the folder next to noch.exe is searched too.
std::unique_ptr<Transcriber> makeMoonshineTranscriber(const QString &folder);
QString defaultMoonshineFolder();
