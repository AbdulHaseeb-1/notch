#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>

// Incremental parser for streamed model replies (Server-Sent Events).
//
//  * Anthropic Messages API  (`stream: true`): message_start,
//    content_block_start / _delta / _stop, message_delta, message_stop, error.
//  * OpenAI Chat Completions (`stream: true`): `data: {choices:[{delta}]}`
//    chunks, tool-call argument fragments by index, `data: [DONE]`.
//
// feed() takes raw network bytes in any chunking. message() rebuilds the
// same JSON the non-streaming endpoint returns, so the rest of the agent loop
// does not care whether a reply was streamed. text() is the visible answer
// text so far, for live display.
class StreamAccumulator
{
public:
    enum class Format { Anthropic, OpenAI };

    explicit StreamAccumulator(Format format = Format::Anthropic) : m_format(format) {}

    void reset(Format format);
    // Returns true when the visible text grew.
    bool feed(const QByteArray &bytes);

    QString text() const { return m_text; }
    QJsonObject message() const;
    bool sawEvents() const { return m_events > 0; }
    bool finished() const { return m_finished; }
    // The model actually finished: a terminal event (message_stop / [DONE])
    // or a stop/finish reason. A stream that closes without one was cut off.
    bool complete() const;
    bool hasError() const { return !m_error.isEmpty(); }
    QString errorMessage() const { return m_error; }
    int events() const { return m_events; }

private:
    void handleEvent(const QString &event, const QByteArray &data);
    void handleAnthropic(const QJsonObject &o);
    void handleOpenAI(const QJsonObject &o);

    Format m_format;
    QByteArray m_buffer;
    QString m_pendingEvent;
    QByteArray m_pendingData;
    int m_events = 0;
    bool m_finished = false;
    QString m_error;
    QString m_text;

    // Anthropic
    QJsonObject m_message;
    QList<QJsonObject> m_blocks;
    QMap<int, QString> m_partialJson;

    // OpenAI
    QString m_id;
    QString m_model;
    QString m_finishReason;
    QJsonObject m_usage;
    struct ToolCall { QString id; QString name; QString args; };
    QMap<int, ToolCall> m_toolCalls;
};
