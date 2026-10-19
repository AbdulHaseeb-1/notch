// Unit tests for StreamAccumulator: Anthropic and OpenAI SSE parsing under
// arbitrary chunking, tool-call argument fragments, errors and [DONE].
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>

#include "../src/StreamAccumulator.h"

static QByteArray anthropicStream()
{
    return QByteArray(
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_1\",\"type\":\"message\",\"role\":\"assistant\",\"model\":\"claude-x\",\"content\":[],\"usage\":{\"input_tokens\":12,\"cache_read_input_tokens\":9}}}\n\n"
        ": keep-alive\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello \"}}\n\n"
        "event: content_block_delta\r\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"wörld\"}}\r\n\r\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"tool_use\",\"id\":\"tu_1\",\"name\":\"bash\",\"input\":{}}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"comm\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"and\\\": \\\"ls\\\"}\"}}\n\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":1}\n\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"},\"usage\":{\"output_tokens\":7}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n");
}

class TstStream : public QObject
{
    Q_OBJECT
private slots:
    void anthropicWholeAndByteByByte_data() {
        QTest::addColumn<int>("chunk");
        QTest::newRow("whole") << 100000;
        QTest::newRow("bytes") << 1;
        QTest::newRow("odd") << 7;
    }
    void anthropicWholeAndByteByByte() {
        QFETCH(int, chunk);
        StreamAccumulator acc(StreamAccumulator::Format::Anthropic);
        const QByteArray all = anthropicStream();
        bool grew = false;
        for (int i = 0; i < all.size(); i += chunk)
            grew |= acc.feed(all.mid(i, chunk));
        QVERIFY(grew);
        QVERIFY(acc.finished());
        QVERIFY(!acc.hasError());
        QCOMPARE(acc.text(), QStringLiteral("Hello wörld"));
        const QJsonObject m = acc.message();
        QCOMPARE(m.value("id").toString(), QStringLiteral("msg_1"));
        QCOMPARE(m.value("stop_reason").toString(), QStringLiteral("tool_use"));
        const QJsonArray c = m.value("content").toArray();
        QCOMPARE(c.size(), 2);
        QCOMPARE(c.at(0).toObject().value("text").toString(), QStringLiteral("Hello wörld"));
        QCOMPARE(c.at(1).toObject().value("input").toObject().value("command").toString(), QStringLiteral("ls"));
        const QJsonObject u = m.value("usage").toObject();
        QCOMPARE(u.value("input_tokens").toInt(), 12);
        QCOMPARE(u.value("output_tokens").toInt(), 7);
        QCOMPARE(u.value("cache_read_input_tokens").toInt(), 9);
    }

    void anthropicErrorEvent() {
        StreamAccumulator acc(StreamAccumulator::Format::Anthropic);
        acc.feed("event: error\ndata: {\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\",\"message\":\"Overloaded\"}}\n\n");
        QVERIFY(acc.hasError());
        QCOMPARE(acc.errorMessage(), QStringLiteral("Overloaded"));
    }

    void anthropicCutOffToolInputStillParses() {
        StreamAccumulator acc(StreamAccumulator::Format::Anthropic);
        const QByteArray all = anthropicStream();
        acc.feed(all.left(all.indexOf("\"type\":\"content_block_stop\",\"index\":1")));
        QVERIFY(!acc.finished());
        // Partial JSON for block 1 is complete text-wise but no stop arrived.
        const QJsonArray c = acc.message().value("content").toArray();
        QCOMPARE(c.size(), 2);
        QCOMPARE(c.at(1).toObject().value("input").toObject().value("command").toString(), QStringLiteral("ls"));
    }

    void openAiTextAndToolCalls() {
        StreamAccumulator acc(StreamAccumulator::Format::OpenAI);
        const QByteArray s =
            "data: {\"id\":\"c1\",\"model\":\"gpt-x\",\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\",\"content\":\"Hi\"}}]}\n\n"
            "data: {\"id\":\"c1\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\" there\"}}]}\n\n"
            "data: {\"id\":\"c1\",\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_9\",\"type\":\"function\",\"function\":{\"name\":\"bash\",\"arguments\":\"{\\\"command\\\":\"}}]}}]}\n\n"
            "data: {\"id\":\"c1\",\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"\\\"pwd\\\"}\"}}]}}]}\n\n"
            "data: {\"id\":\"c1\",\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
            "data: {\"id\":\"c1\",\"choices\":[],\"usage\":{\"prompt_tokens\":30,\"completion_tokens\":4,\"prompt_tokens_details\":{\"cached_tokens\":16}}}\n\n"
            "data: [DONE]\n\n";
        for (int i = 0; i < s.size(); i += 5)
            acc.feed(s.mid(i, 5));
        QVERIFY(acc.finished());
        QCOMPARE(acc.text(), QStringLiteral("Hi there"));
        const QJsonObject m = acc.message();
        QCOMPARE(m.value("model").toString(), QStringLiteral("gpt-x"));
        const QJsonObject choice = m.value("choices").toArray().first().toObject();
        QCOMPARE(choice.value("finish_reason").toString(), QStringLiteral("tool_calls"));
        const QJsonObject tc = choice.value("message").toObject().value("tool_calls").toArray().first().toObject();
        QCOMPARE(tc.value("id").toString(), QStringLiteral("call_9"));
        QCOMPARE(tc.value("function").toObject().value("arguments").toString(), QStringLiteral("{\"command\":\"pwd\"}"));
        QCOMPARE(m.value("usage").toObject().value("prompt_tokens_details").toObject().value("cached_tokens").toInt(), 16);
    }

    void completeOnlyWhenTheModelFinished() {
        StreamAccumulator a(StreamAccumulator::Format::Anthropic);
        a.feed("event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"id\":\"m\",\"role\":\"assistant\",\"content\":[],\"stop_reason\":null}}\n\n"
               "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
               "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"hal\"}}\n\n");
        QVERIFY(!a.complete());
        a.feed("event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"}}\n\n");
        QVERIFY(a.complete());   // stop reason seen, message_stop may still be in flight

        StreamAccumulator o(StreamAccumulator::Format::OpenAI);
        o.feed("data: {\"id\":\"c\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"hi\"}}]}\n\n");
        QVERIFY(!o.complete());
        o.feed("data: {\"id\":\"c\",\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n");
        QVERIFY(o.complete());   // lenient servers omit [DONE]
    }

    void garbageIsIgnored() {
        StreamAccumulator acc(StreamAccumulator::Format::OpenAI);
        QVERIFY(!acc.feed("data: {not json\n\nretry: 1000\n\n"));
        QVERIFY(!acc.hasError());
        QCOMPARE(acc.text(), QString());
    }
    // Regression for the long-chat cut-off: a stream whose last event has no
    // terminating blank line (server closed right after it) still completes,
    // and max_tokens is reported, not hidden.
    void finishFlushesLastEventAndReportsMaxTokens() {
        StreamAccumulator acc(StreamAccumulator::Format::Anthropic);
        QByteArray all = anthropicStream();
        all.replace("\"tool_use\"},\"usage\"", "\"max_tokens\"},\"usage\"");
        all.chop(2);   // "...message_stop\"}" with no "\n\n"
        for (int i = 0; i < all.size(); i += 5)
            acc.feed(all.mid(i, 5));
        QVERIFY(!acc.finished());
        acc.finish();
        QVERIFY(acc.finished());
        QVERIFY(acc.complete());
        QCOMPARE(acc.stopReason(), QStringLiteral("max_tokens"));
        const int events = acc.events();
        acc.finish();   // idempotent
        QCOMPARE(acc.events(), events);
    }

    void longUtf8TextSplitEverywhere() {
        // 3000 deltas of multi-byte text, fed in 1..17-byte chunks that land
        // inside UTF-8 sequences, "data:" prefixes and \r\n pairs.
        QByteArray all = "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"id\":\"m\",\"content\":[]}}\n\n"
                         "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n";
        QString expect;
        for (int i = 0; i < 3000; ++i) {
            const QString t = QStringLiteral("ü日本%1✓ ").arg(i);
            expect += t;
            QJsonObject d{ { "type", "content_block_delta" }, { "index", 0 },
                           { "delta", QJsonObject{ { "type", "text_delta" }, { "text", t } } } };
            all += "event: content_block_delta\r\ndata: " + QJsonDocument(d).toJson(QJsonDocument::Compact) + "\r\n\r\n";
        }
        all += "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"}}\n\n"
               "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";
        StreamAccumulator acc(StreamAccumulator::Format::Anthropic);
        int step = 1;
        for (int i = 0; i < all.size(); i += step, step = step % 17 + 1)
            acc.feed(all.mid(i, step));
        QVERIFY(acc.complete());
        QCOMPARE(acc.text().size(), expect.size());
        QCOMPARE(acc.text(), expect);
        QCOMPARE(acc.stopReason(), QStringLiteral("end_turn"));
    }

    void openAiLengthIsReported() {
        StreamAccumulator acc(StreamAccumulator::Format::OpenAI);
        acc.feed("data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"cut\"}}]}\n\n"
                 "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"length\"}]}\n\ndata: [DONE]");
        acc.finish();
        QVERIFY(acc.finished());
        QCOMPARE(acc.stopReason(), QStringLiteral("length"));
        QCOMPARE(acc.message().value("choices").toArray().first().toObject().value("finish_reason").toString(),
                 QStringLiteral("length"));
    }
};

QTEST_APPLESS_MAIN(TstStream)
#include "tst_stream.moc"
