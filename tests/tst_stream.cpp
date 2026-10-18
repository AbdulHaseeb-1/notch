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

    void garbageIsIgnored() {
        StreamAccumulator acc(StreamAccumulator::Format::OpenAI);
        QVERIFY(!acc.feed("data: {not json\n\nretry: 1000\n\n"));
        QVERIFY(!acc.hasError());
        QCOMPARE(acc.text(), QString());
    }
};

QTEST_APPLESS_MAIN(TstStream)
#include "tst_stream.moc"
