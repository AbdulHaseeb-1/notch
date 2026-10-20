// Unit tests for the model-agnostic tool-call repair pipeline
// (normalizeToolCall in BuddyProviderInternal.h): fenced JSON, trailing
// commas, argument aliases, scalar coercion and correction nudges.
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>

#include "BuddyProviderInternal.h"
#include "HookBus.h"
#include "MemoryExtractor.h"
#include "MemoryStore.h"
#include "PlanValidator.h"

#include <QTemporaryDir>

using namespace noch::buddyprovider;

class TstToolRepair : public QObject
{
    Q_OBJECT
private slots:
    void validCallPassesThrough();
    void fencedJsonIsRepaired();
    void trailingCommaIsRepaired();
    void aliasIsRenamed();
    void canonicalArgIsNeverClobbered();
    void stringifiedIntIsCoerced();
    void missingRequiredIsUnrepairable();
    void unknownToolKeepsExistingPath();
    void emptyInputOkWhenNothingRequired();
    void nonObjectJsonIsUnrepairable();
    void fieldLevelTypeError();
    void fieldLevelEnumError();
    void unknownArgIsFlagged();
    // P5 plan validation
    void validPlanPasses();
    void planWithUnknownToolFails();
    void planWithBadArgFails();
    void planWithEmptyVerifyFails();
    // P2 text tool protocol
    void textProtocolEnvelopeParses();
    void textProtocolSingleCallParses();
    void textProtocolFencedParses();
    void textProtocolRejectsUnknownTool();
    // P3 hook bus
    void hookDenyBeatsAskAndAllow();
    void hookAskBeatsAllow();
    void hookRewritesChain();
    // P8 memory extraction
    void extractorPicksUpExplicitDirectives();
    void extractorIgnoresQuestionsAndHarnessNotes();
    // P8 MemoryStore (audit: previously untested)
    void memoryStoreAddSearchUpdate();
    void memoryStoreRejectsTraversalIds();
    void memoryStoreGistKeepsInnerHash();
    void memoryStoreTokenizesUnicode();
    // P5 plan extras (L4/L5, M7)
    void planExtractsSingleLineFence();
    void planValidationHonorsAvailability();
};

static ToolCallRepair repair(const QString &tool, const QJsonObject &input,
                             const QString &raw, QString *note, QJsonObject *out)
{
    QJsonObject in = input;
    const ToolCallRepair rr = normalizeToolCall(tool, in, raw, note);
    if (out)
        *out = in;
    return rr;
}

void TstToolRepair::validCallPassesThrough()
{
    const QJsonObject in{ { QStringLiteral("command"), QStringLiteral("ls") } };
    QJsonObject out;
    QString note;
    QVERIFY(repair(QStringLiteral("bash"), in, QString(), &note, &out)
            == ToolCallRepair::Ok);
    QVERIFY(note.isEmpty());
    QVERIFY(out == in);
}

void TstToolRepair::fencedJsonIsRepaired()
{
    QJsonObject out;
    QString note;
    const QString raw = QStringLiteral("```json\n{\"command\": \"ls\",}\n```");
    QVERIFY(repair(QStringLiteral("bash"), QJsonObject(), raw, &note, &out)
            == ToolCallRepair::Repaired);
    QCOMPARE(out.value(QStringLiteral("command")).toString(), QStringLiteral("ls"));
}

void TstToolRepair::trailingCommaIsRepaired()
{
    QJsonObject out;
    QString note;
    const QString raw = QStringLiteral("{\"command\": \"ls\", \"working_dir\": \"c:/\",}");
    QVERIFY(repair(QStringLiteral("bash"), QJsonObject(), raw, &note, &out)
            == ToolCallRepair::Repaired);
    QCOMPARE(out.value(QStringLiteral("command")).toString(), QStringLiteral("ls"));
    QCOMPARE(out.value(QStringLiteral("working_dir")).toString(), QStringLiteral("c:/"));
}

void TstToolRepair::aliasIsRenamed()
{
    const QJsonObject in{ { QStringLiteral("cmd"), QStringLiteral("ls") } };
    QJsonObject out;
    QString note;
    QVERIFY(repair(QStringLiteral("bash"), in, QString(), &note, &out)
            == ToolCallRepair::Repaired);
    QCOMPARE(out.value(QStringLiteral("command")).toString(), QStringLiteral("ls"));
    QVERIFY(!out.contains(QStringLiteral("cmd")));
}

void TstToolRepair::canonicalArgIsNeverClobbered()
{
    const QJsonObject in{ { QStringLiteral("command"), QStringLiteral("a") },
                          { QStringLiteral("cmd"), QStringLiteral("b") } };
    QJsonObject out;
    QString note;
    // The real argument wins; the redundant alias is dropped (a repair),
    // never overwriting the canonical value.
    QVERIFY(repair(QStringLiteral("bash"), in, QString(), &note, &out)
            == ToolCallRepair::Repaired);
    QCOMPARE(out.value(QStringLiteral("command")).toString(), QStringLiteral("a"));
    QVERIFY(!out.contains(QStringLiteral("cmd")));
}

void TstToolRepair::stringifiedIntIsCoerced()
{
    const QJsonObject in{ { QStringLiteral("command"), QStringLiteral("view") },
                          { QStringLiteral("path"), QStringLiteral("f.cpp") },
                          { QStringLiteral("insert_line"), QStringLiteral("3") } };
    QJsonObject out;
    QString note;
    QVERIFY(repair(QStringLiteral("str_replace_editor"), in, QString(), &note, &out)
            == ToolCallRepair::Repaired);
    QVERIFY(!out.value(QStringLiteral("insert_line")).isString());
    QCOMPARE(out.value(QStringLiteral("insert_line")).toInt(), 3);
}

void TstToolRepair::missingRequiredIsUnrepairable()
{
    QJsonObject out;
    QString note;
    QVERIFY(repair(QStringLiteral("bash"), QJsonObject(), QString(), &note, &out)
            == ToolCallRepair::Unrepairable);
    QVERIFY(note.contains(QStringLiteral("command")));
    QVERIFY(note.contains(QStringLiteral("Required")));
}

void TstToolRepair::unknownToolKeepsExistingPath()
{
    const QJsonObject in{ { QStringLiteral("x"), QStringLiteral("y") } };
    QJsonObject out;
    QString note;
    // Unknown tools are not the repair pipeline's business: the gate denies them.
    QVERIFY(repair(QStringLiteral("frobnicate"), in, QString(), &note, &out)
            == ToolCallRepair::Ok);
    QVERIFY(out == in);
}

void TstToolRepair::emptyInputOkWhenNothingRequired()
{
    QJsonObject out;
    QString note;
    QVERIFY(repair(QStringLiteral("web_search"), QJsonObject(), QString(), &note, &out)
            == ToolCallRepair::Ok);
    QVERIFY(note.isEmpty());
}

void TstToolRepair::nonObjectJsonIsUnrepairable()
{
    QJsonObject out;
    QString note;
    QVERIFY(repair(QStringLiteral("bash"), QJsonObject(),
                   QStringLiteral("[1, 2]"), &note, &out)
            == ToolCallRepair::Unrepairable);
    QVERIFY(note.contains(QStringLiteral("command")));
}

void TstToolRepair::fieldLevelTypeError()
{
    // insert_line should be an integer, not a string that won't coerce.
    const QJsonObject in{ { QStringLiteral("command"), QStringLiteral("insert") },
                          { QStringLiteral("path"), QStringLiteral("f.txt") },
                          { QStringLiteral("insert_line"), QStringLiteral("abc") } };
    QJsonObject out;
    QString note;
    QVERIFY(repair(QStringLiteral("str_replace_editor"), in, QString(), &note, &out)
            == ToolCallRepair::Unrepairable);
    QVERIFY(note.contains(QStringLiteral("insert_line")));
    QVERIFY(note.contains(QStringLiteral("integer")));
}

void TstToolRepair::fieldLevelEnumError()
{
    const QJsonObject in{ { QStringLiteral("mode"), QStringLiteral("find") },
                          { QStringLiteral("pattern"), QStringLiteral("*.cpp") } };
    QJsonObject out;
    QString note;
    QVERIFY(repair(QStringLiteral("glob_grep"), in, QString(), &note, &out)
            == ToolCallRepair::Unrepairable);
    QVERIFY(note.contains(QStringLiteral("mode")));
    QVERIFY(note.contains(QStringLiteral("glob")));
}

void TstToolRepair::unknownArgIsFlagged()
{
    const QJsonObject in{ { QStringLiteral("command"), QStringLiteral("ls") },
                          { QStringLiteral("bogus_arg"), QStringLiteral("x") } };
    QJsonObject out;
    QString note;
    QVERIFY(repair(QStringLiteral("bash"), in, QString(), &note, &out)
            == ToolCallRepair::Unrepairable);
    QVERIFY(note.contains(QStringLiteral("bogus_arg")));
}

// ---- P5 plan validation ---------------------------------------------------

static QJsonObject planStep(int n, const QString &tool, const QJsonObject &args,
                            const QString &verify)
{
    return QJsonObject{ { QStringLiteral("step"), n },
                        { QStringLiteral("tool"), tool },
                        { QStringLiteral("args_sketch"), args },
                        { QStringLiteral("verify"), verify } };
}

void TstToolRepair::validPlanPasses()
{
    const QJsonArray plan{
        planStep(1, QStringLiteral("bash"),
                 QJsonObject{ { QStringLiteral("command"), QStringLiteral("ls") } },
                 QStringLiteral("listing shown")),
        planStep(2, QStringLiteral("str_replace_editor"),
                 QJsonObject{ { QStringLiteral("command"), QStringLiteral("view") },
                              { QStringLiteral("path"), QStringLiteral("f.txt") } },
                 QStringLiteral("file contents visible")),
    };
    const PlanValidation v = validatePlan(plan);
    QVERIFY2(v.ok, qPrintable(v.errors.join('\n')));
    QCOMPARE(v.steps.size(), 2);
    QCOMPARE(v.steps.at(0).tool, QStringLiteral("bash"));
}

void TstToolRepair::planWithUnknownToolFails()
{
    const QJsonArray plan{
        planStep(1, QStringLiteral("teleport"), QJsonObject(), QStringLiteral("done")),
    };
    const PlanValidation v = validatePlan(plan);
    QVERIFY(!v.ok);
    QVERIFY(v.errors.join('\n').contains(QStringLiteral("unknown tool")));
}

void TstToolRepair::planWithBadArgFails()
{
    const QJsonArray plan{
        planStep(1, QStringLiteral("bash"),
                 QJsonObject{ { QStringLiteral("bogus"), QStringLiteral("x") } },
                 QStringLiteral("done")),
    };
    const PlanValidation v = validatePlan(plan);
    QVERIFY(!v.ok);
    QVERIFY(v.errors.join('\n').contains(QStringLiteral("bogus")));
}

void TstToolRepair::planWithEmptyVerifyFails()
{
    const QJsonArray plan{
        planStep(1, QStringLiteral("bash"),
                 QJsonObject{ { QStringLiteral("command"), QStringLiteral("ls") } },
                 QString()),
    };
    const PlanValidation v = validatePlan(plan);
    QVERIFY(!v.ok);
    QVERIFY(v.errors.join('\n').contains(QStringLiteral("verify")));
}

// ---- P2 text tool protocol ----------------------------------------------

void TstToolRepair::textProtocolEnvelopeParses()
{
    const QString reply = QStringLiteral(
        "{\"text\": \"listing\", \"tool_calls\": [{\"name\": \"bash\", "
        "\"input\": {\"command\": \"ls\"}}]}");
    const QList<TextToolCall> calls = extractTextToolCalls(reply);
    QCOMPARE(calls.size(), 1);
    QCOMPARE(calls.at(0).name, QStringLiteral("bash"));
    QCOMPARE(calls.at(0).input.value(QStringLiteral("command")).toString(),
             QStringLiteral("ls"));
}

void TstToolRepair::textProtocolSingleCallParses()
{
    const QString reply = QStringLiteral("{\"tool\": \"web_fetch\", \"input\": "
                                         "{\"url\": \"https://example.com\"}}");
    const QList<TextToolCall> calls = extractTextToolCalls(reply);
    QCOMPARE(calls.size(), 1);
    QCOMPARE(calls.at(0).name, QStringLiteral("web_fetch"));
}

void TstToolRepair::textProtocolFencedParses()
{
    const QString reply = QStringLiteral(
        "Sure, running it:\n```json\n{\"tool_calls\": [{\"name\": \"glob_grep\", "
        "\"input\": {\"mode\": \"glob\", \"pattern\": \"*.cpp\"}}]}\n```");
    const QList<TextToolCall> calls = extractTextToolCalls(reply);
    QCOMPARE(calls.size(), 1);
    QCOMPARE(calls.at(0).name, QStringLiteral("glob_grep"));
}

void TstToolRepair::textProtocolRejectsUnknownTool()
{
    // Prose JSON for a tool that doesn't exist must not become an action.
    const QString reply = QStringLiteral("{\"tool\": \"teleport\", \"input\": {}}");
    QVERIFY(extractTextToolCalls(reply).isEmpty());
    QVERIFY(extractTextToolCalls(QStringLiteral("plain text, no JSON")).isEmpty());
}

// ---- P3 hook bus ----------------------------------------------------------

static HookContext preToolCtx(const QString &tool, const QJsonObject &input)
{
    HookContext ctx;
    ctx.event = HookEvent::PreToolUse;
    ctx.toolName = tool;
    ctx.input = input;
    return ctx;
}

void TstToolRepair::hookDenyBeatsAskAndAllow()
{
    HookBus bus;
    bus.addHook(HookEvent::PreToolUse, QStringLiteral("a"),
                [](const HookContext &) { return HookResult::allow(); });
    bus.addHook(HookEvent::PreToolUse, QStringLiteral("b"),
                [](const HookContext &) {
                    return HookResult::ask(QStringLiteral("ask:x"), QStringLiteral("need ok"));
                });
    bus.addHook(HookEvent::PreToolUse, QStringLiteral("c"),
                [](const HookContext &) {
                    return HookResult::deny(QStringLiteral("deny:y"), QStringLiteral("nope"));
                });
    HookContext ctx = preToolCtx(QStringLiteral("bash"), QJsonObject());
    QStringList ran;
    const HookResult r = bus.run(HookEvent::PreToolUse, ctx, &ran);
    QCOMPARE(r.decision, HookDecision::Deny);
    QCOMPARE(r.rule, QStringLiteral("deny:y"));
    QCOMPARE(ran.size(), 3);

    // L3: the real short-circuit check — deny FIRST: nothing after it runs.
    HookBus bus2;
    bus2.addHook(HookEvent::PreToolUse, QStringLiteral("first"),
                 [](const HookContext &) {
                     return HookResult::deny(QStringLiteral("deny:first"), QStringLiteral("stop"));
                 });
    bus2.addHook(HookEvent::PreToolUse, QStringLiteral("never"),
                 [](const HookContext &) { return HookResult::allow(); });
    QStringList ran2;
    const HookResult r2 = bus2.run(HookEvent::PreToolUse, ctx, &ran2);
    QCOMPARE(r2.decision, HookDecision::Deny);
    QCOMPARE(ran2.size(), 1);
}

void TstToolRepair::hookAskBeatsAllow()
{
    HookBus bus;
    bus.addHook(HookEvent::PreToolUse, QStringLiteral("a"),
                [](const HookContext &) { return HookResult::allow(); });
    bus.addHook(HookEvent::PreToolUse, QStringLiteral("b"),
                [](const HookContext &) {
                    return HookResult::ask(QStringLiteral("ask:x"), QStringLiteral("need ok"));
                });
    HookContext ctx = preToolCtx(QStringLiteral("bash"), QJsonObject());
    const HookResult r = bus.run(HookEvent::PreToolUse, ctx);
    QCOMPARE(r.decision, HookDecision::Ask);
    QCOMPARE(r.reason, QStringLiteral("need ok"));
}

void TstToolRepair::hookRewritesChain()
{
    HookBus bus;
    bus.addHook(HookEvent::PreToolUse, QStringLiteral("norm"),
                [](const HookContext &ctx) {
                    QJsonObject in = ctx.input;
                    in[QStringLiteral("path")] = QStringLiteral("/abs/f.txt");
                    return HookResult::rewriteInput(in);
                });
    bus.addHook(HookEvent::PreToolUse, QStringLiteral("observer"),
                [](const HookContext &ctx) -> HookResult {
                    // Sees the rewrite from the earlier hook.
                    if (ctx.input.value(QStringLiteral("path")).toString()
                            != QStringLiteral("/abs/f.txt"))
                        return HookResult::deny(QStringLiteral("test"), QStringLiteral("no chain"));
                    return HookResult::allow();
                });
    HookContext ctx = preToolCtx(QStringLiteral("str_replace_editor"),
                                 QJsonObject{ { QStringLiteral("path"), QStringLiteral("f.txt") } });
    const HookResult r = bus.run(HookEvent::PreToolUse, ctx);
    QVERIFY(r.hasRewrite);
    QCOMPARE(ctx.input.value(QStringLiteral("path")).toString(),
             QStringLiteral("/abs/f.txt"));
}

// ---- P8 memory extraction -------------------------------------------------

void TstToolRepair::extractorPicksUpExplicitDirectives()
{
    const QStringList msgs{
        QStringLiteral("Remember that my editor is neovim."),
        QStringLiteral("I prefer dark mode everywhere."),
    };
    const QStringList facts = extractFactCandidates(msgs);
    QVERIFY(facts.contains(QStringLiteral("My editor is neovim.")));
    QVERIFY(facts.contains(QStringLiteral("I prefer dark mode everywhere.")));
}

void TstToolRepair::extractorIgnoresQuestionsAndHarnessNotes()
{
    const QStringList msgs{
        QStringLiteral("[Tool results]\n- bash: done"),
        QStringLiteral("What is my favorite color?"),
        QStringLiteral("can you help with this."),
        QStringLiteral("My name is"),
    };
    QVERIFY(extractFactCandidates(msgs).isEmpty());
}

// ---- P8 MemoryStore --------------------------------------------------------

void TstToolRepair::memoryStoreAddSearchUpdate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryStore store(dir.path());
    const QString id = store.addFact(QStringLiteral("The user's editor is neovim. Keyboard-first workflow."));
    QVERIFY(!id.isEmpty());
    QVERIFY(id.endsWith(QStringLiteral(".md")));
    QCOMPARE(store.list().size(), 1);

    const QList<MemoryStore::SearchHit> hits = store.search(QStringLiteral("editor neovim"), 8);
    QVERIFY(!hits.isEmpty());
    QCOMPARE(hits.first().fact.id, id);

    // M3: updateFact renames the file to the new content hash; the stale
    // file must not linger as a duplicate.
    const QString id2 = store.updateFact(id, QStringLiteral("The user's editor is emacs. Keyboard-first workflow."));
    QVERIFY(!id2.isEmpty());
    QVERIFY(id2 != id);
    QCOMPARE(store.list().size(), 1);
    QVERIFY(store.get(id).text.isEmpty());
    QVERIFY(store.get(id2).text.contains(QStringLiteral("emacs")));
}

void TstToolRepair::memoryStoreRejectsTraversalIds()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryStore store(dir.path());
    const QString id = store.addFact(QStringLiteral("plain fact here"));
    QVERIFY(!id.isEmpty());

    // M5: sandbox rejects ids that escape the root.
    QVERIFY(store.get(QStringLiteral("../escape.md")).text.isEmpty());
    QVERIFY(store.get(QStringLiteral("/abs.md")).text.isEmpty());
    QVERIFY(store.get(QStringLiteral("..\\win.md")).text.isEmpty());
    QVERIFY(store.updateFact(QStringLiteral("../../evil.md"), QStringLiteral("x")).isEmpty());
}

void TstToolRepair::memoryStoreGistKeepsInnerHash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryStore store(dir.path());
    // L7: gist strips only LEADING markdown markers, not inner '#'.
    const QString id = store.addFact(QStringLiteral("# C# project notes\nSome body text."));
    QVERIFY(!id.isEmpty());
    QCOMPARE(store.list().first().gist, QStringLiteral("C# project notes"));
}

void TstToolRepair::memoryStoreTokenizesUnicode()
{
    // L6: non-Latin text must tokenize instead of silently vanishing.
    const QStringList toks = MemoryStore::tokenize(QStringLiteral("میری ایڈیٹر neovim ہے"));
    QVERIFY(!toks.isEmpty());
    QVERIFY(toks.contains(QStringLiteral("neovim")));
}

// ---- P5 plan extras --------------------------------------------------------

void TstToolRepair::planExtractsSingleLineFence()
{
    // L5: single-line ```plan blocks (common from small models) are detected.
    const QJsonArray arr = extractPlanBlock(
        QStringLiteral("```plan[{\"step\":1,\"tool\":\"bash\",\"args_sketch\":{},\"verify\":\"ok\"}]```"));
    QCOMPARE(arr.size(), 1);
    QCOMPARE(arr.first().toObject().value(QStringLiteral("tool")).toString(),
             QStringLiteral("bash"));
}

void TstToolRepair::planValidationHonorsAvailability()
{
    // M7: a plan naming a tool the harness won't run fails validation,
    // naming which tool and why.
    const QJsonArray arr = extractPlanBlock(
        QStringLiteral("```plan\n[{\"step\":1,\"tool\":\"web_search\",\"args_sketch\":{\"query\":\"x\"},\"verify\":\"ok\"}]\n```"));
    QVERIFY(!arr.isEmpty());
    const PlanValidation v = validatePlan(arr, [](const QString &tool) {
        return tool != QStringLiteral("web_search");   // diet: not loaded
    });
    QVERIFY(!v.ok);
    QVERIFY(v.errors.join(QStringLiteral(" ")).contains(QStringLiteral("not available")));
}

QTEST_APPLESS_MAIN(TstToolRepair)
#include "tst_toolrepair.moc"
