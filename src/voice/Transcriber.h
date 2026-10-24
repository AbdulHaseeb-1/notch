#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <functional>
#include <memory>

// A speech-to-text engine. Noch's one engine is Deepgram's cloud service;
// the interface exists so tests can stand in a scripted one.
//
// prepare() and transcribe() run on VoiceInput's worker thread and may take a
// while; problem() is a cheap check and is safe to call from any thread.
class Transcriber
{
public:
    virtual ~Transcriber() = default;
    virtual QString name() const = 0;
    // Why it cannot be used ("" when it looks usable), e.g. no API key is set.
    virtual QString problem() const = 0;
    // Gets the engine ready. "" on success, otherwise why not.
    virtual QString prepare() = 0;
    // 16 kHz mono 16-bit PCM in; the words out ("" for no words). A failure sets `error`.
    virtual QString transcribe(const QByteArray &pcm16, QString *error) = 0;
    // The model files the engine needs, for the settings to show their state:
    // a list of {name, path, found, bytes} maps. "" list when there are none.
    virtual QVariantList modelFiles() const { return {}; }
    // An engine that talks to a network service does not need the worker thread: it
    // runs on the caller's thread and reports through the callback ("" text and "" error
    // = no words heard). When true, VoiceInput uses transcribeAsync() instead of
    // prepare()/transcribe().
    virtual bool isAsync() const { return false; }
    virtual void transcribeAsync(const QByteArray &pcm16,
                                 std::function<void(const QString &text, const QString &error)> done)
    {
        Q_UNUSED(pcm16);
        done(QString(), QStringLiteral("This engine cannot run asynchronously."));
    }
};

// Speech to text by Deepgram's service (https://developers.deepgram.com/docs/pre-recorded-audio).
// The clip is sent to Deepgram with the API key; nothing is kept on this computer.
struct DeepgramListenSettings
{
    QString apiKey;
    QString model = QStringLiteral("nova-3");
    QString language = QStringLiteral("en");
    QStringList keyterms;
    // Tests: where requests go instead of api.deepgram.com.
    QString apiBase;
};
std::unique_ptr<Transcriber> makeDeepgramTranscriber(const DeepgramListenSettings &settings);
