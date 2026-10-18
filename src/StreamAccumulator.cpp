#include "StreamAccumulator.h"

#include <QJsonDocument>

void StreamAccumulator::reset(Format format)
{
    *this = StreamAccumulator(format);
}

bool StreamAccumulator::feed(const QByteArray &bytes)
{
    const int before = m_text.size();
    m_buffer += bytes;
    // SSE: lines separated by \n (optionally \r\n); a blank line ends an event.
    for (;;) {
        const int nl = m_buffer.indexOf('\n');
        if (nl < 0)
            break;
        QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty()) {
            if (!m_pendingData.isEmpty() || !m_pendingEvent.isEmpty())
                handleEvent(m_pendingEvent, m_pendingData);
            m_pendingEvent.clear();
            m_pendingData.clear();
            continue;
        }
        if (line.startsWith(':'))
            continue;   // comment / keep-alive
        if (line.startsWith("event:")) {
            m_pendingEvent = QString::fromUtf8(line.mid(6).trimmed());
        } else if (line.startsWith("data:")) {
            QByteArray d = line.mid(5);
            if (d.startsWith(' '))
                d.remove(0, 1);
            if (!m_pendingData.isEmpty())
                m_pendingData += '\n';
            m_pendingData += d;
        }
    }
    return m_text.size() != before;
}

void StreamAccumulator::handleEvent(const QString &event, const QByteArray &data)
{
    ++m_events;
    if (data.trimmed() == "[DONE]") {
        m_finished = true;
        return;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject o = doc.object();
    if (event == QLatin1String("error") || o.value(QStringLiteral("type")).toString() == QLatin1String("error")) {
        const QJsonObject e = o.value(QStringLiteral("error")).toObject();
        m_error = e.value(QStringLiteral("message")).toString(QStringLiteral("stream error"));
        m_finished = true;
        return;
    }
    if (m_format == Format::Anthropic)
        handleAnthropic(o);
    else
        handleOpenAI(o);
}

void StreamAccumulator::handleAnthropic(const QJsonObject &o)
{
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("message_start")) {
        m_message = o.value(QStringLiteral("message")).toObject();
        return;
    }
    if (type == QLatin1String("content_block_start")) {
        const int idx = o.value(QStringLiteral("index")).toInt();
        while (m_blocks.size() <= idx)
            m_blocks.append(QJsonObject());
        m_blocks[idx] = o.value(QStringLiteral("content_block")).toObject();
        return;
    }
    if (type == QLatin1String("content_block_delta")) {
        const int idx = o.value(QStringLiteral("index")).toInt();
        if (idx < 0 || idx >= m_blocks.size())
            return;
        QJsonObject &b = m_blocks[idx];
        const QJsonObject d = o.value(QStringLiteral("delta")).toObject();
        const QString dt = d.value(QStringLiteral("type")).toString();
        if (dt == QLatin1String("text_delta")) {
            const QString t = d.value(QStringLiteral("text")).toString();
            b[QStringLiteral("text")] = b.value(QStringLiteral("text")).toString() + t;
            m_text += t;
        } else if (dt == QLatin1String("input_json_delta")) {
            m_partialJson[idx] += d.value(QStringLiteral("partial_json")).toString();
        } else if (dt == QLatin1String("thinking_delta")) {
            b[QStringLiteral("thinking")] = b.value(QStringLiteral("thinking")).toString()
                                           + d.value(QStringLiteral("thinking")).toString();
        } else if (dt == QLatin1String("signature_delta")) {
            b[QStringLiteral("signature")] = d.value(QStringLiteral("signature")).toString();
        } else if (dt == QLatin1String("citations_delta")) {
            QJsonArray c = b.value(QStringLiteral("citations")).toArray();
            c.append(d.value(QStringLiteral("citation")));
            b[QStringLiteral("citations")] = c;
        }
        return;
    }
    if (type == QLatin1String("content_block_stop")) {
        const int idx = o.value(QStringLiteral("index")).toInt();
        if (idx >= 0 && idx < m_blocks.size() && m_partialJson.contains(idx)) {
            const QString raw = m_partialJson.take(idx);
            const QJsonDocument in = QJsonDocument::fromJson(raw.toUtf8());
            m_blocks[idx][QStringLiteral("input")] = in.isObject() ? in.object() : QJsonObject();
        }
        return;
    }
    if (type == QLatin1String("message_delta")) {
        const QJsonObject d = o.value(QStringLiteral("delta")).toObject();
        for (auto it = d.begin(); it != d.end(); ++it)
            m_message[it.key()] = it.value();
        QJsonObject usage = m_message.value(QStringLiteral("usage")).toObject();
        const QJsonObject du = o.value(QStringLiteral("usage")).toObject();
        for (auto it = du.begin(); it != du.end(); ++it)
            usage[it.key()] = it.value();
        m_message[QStringLiteral("usage")] = usage;
        return;
    }
    if (type == QLatin1String("message_stop"))
        m_finished = true;
}

void StreamAccumulator::handleOpenAI(const QJsonObject &o)
{
    if (m_id.isEmpty())
        m_id = o.value(QStringLiteral("id")).toString();
    if (m_model.isEmpty())
        m_model = o.value(QStringLiteral("model")).toString();
    if (o.value(QStringLiteral("usage")).isObject())
        m_usage = o.value(QStringLiteral("usage")).toObject();
    const QJsonArray choices = o.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return;
    const QJsonObject ch = choices.first().toObject();
    const QJsonObject d = ch.value(QStringLiteral("delta")).toObject();
    const QString t = d.value(QStringLiteral("content")).toString();
    if (!t.isEmpty())
        m_text += t;
    for (const QJsonValue &v : d.value(QStringLiteral("tool_calls")).toArray()) {
        const QJsonObject tc = v.toObject();
        ToolCall &acc = m_toolCalls[tc.value(QStringLiteral("index")).toInt()];
        if (tc.contains(QStringLiteral("id")))
            acc.id = tc.value(QStringLiteral("id")).toString();
        const QJsonObject fn = tc.value(QStringLiteral("function")).toObject();
        if (fn.contains(QStringLiteral("name")))
            acc.name += fn.value(QStringLiteral("name")).toString();
        acc.args += fn.value(QStringLiteral("arguments")).toString();
    }
    const QString fr = ch.value(QStringLiteral("finish_reason")).toString();
    if (!fr.isEmpty())
        m_finishReason = fr;
}

QJsonObject StreamAccumulator::message() const
{
    if (m_format == Format::Anthropic) {
        QJsonObject msg = m_message;
        if (!msg.contains(QStringLiteral("type")))
            msg[QStringLiteral("type")] = QStringLiteral("message");
        if (!msg.contains(QStringLiteral("role")))
            msg[QStringLiteral("role")] = QStringLiteral("assistant");
        QJsonArray content;
        for (int i = 0; i < m_blocks.size(); ++i) {
            QJsonObject b = m_blocks.at(i);
            if (b.isEmpty())
                continue;
            // A block whose stop never arrived (cut-off stream): parse what we have.
            if (m_partialJson.contains(i)) {
                const QJsonDocument in = QJsonDocument::fromJson(m_partialJson.value(i).toUtf8());
                b[QStringLiteral("input")] = in.isObject() ? in.object() : QJsonObject();
            }
            content.append(b);
        }
        msg[QStringLiteral("content")] = content;
        return msg;
    }
    QJsonObject message{ { QStringLiteral("role"), QStringLiteral("assistant") } };
    message[QStringLiteral("content")] = m_text.isEmpty() && !m_toolCalls.isEmpty()
        ? QJsonValue(QJsonValue::Null) : QJsonValue(m_text);
    if (!m_toolCalls.isEmpty()) {
        QJsonArray calls;
        for (auto it = m_toolCalls.cbegin(); it != m_toolCalls.cend(); ++it) {
            calls.append(QJsonObject{
                { QStringLiteral("id"), it->id },
                { QStringLiteral("type"), QStringLiteral("function") },
                { QStringLiteral("function"), QJsonObject{ { QStringLiteral("name"), it->name },
                                                           { QStringLiteral("arguments"), it->args } } } });
        }
        message[QStringLiteral("tool_calls")] = calls;
    }
    QJsonObject out{
        { QStringLiteral("id"), m_id },
        { QStringLiteral("object"), QStringLiteral("chat.completion") },
        { QStringLiteral("model"), m_model },
        { QStringLiteral("choices"), QJsonArray{ QJsonObject{
              { QStringLiteral("index"), 0 },
              { QStringLiteral("message"), message },
              { QStringLiteral("finish_reason"), m_finishReason } } } },
    };
    if (!m_usage.isEmpty())
        out[QStringLiteral("usage")] = m_usage;
    return out;
}
