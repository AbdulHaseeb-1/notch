// Chat sessions (src/ChatSessions): saving every turn, resuming after a
// restart, branching, rolling summaries, limits.
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ChatSessions.h"

namespace {

void turns(ChatSessions &s, int n, const QString &tag = QStringLiteral("q"), int answerChars = 40)
{
    for (int i = 0; i < n; ++i)
        s.appendTurn(QStringLiteral("%1%2").arg(tag).arg(i), QStringLiteral("answer %1 ").arg(i) + QString(answerChars, 'a'));
}

QStringList roles(const QVariantList &ctx)
{
    QStringList r;
    for (const QVariant &v : ctx)
        r << v.toMap().value("role").toString();
    return r;
}

QString content(const QVariantList &ctx, int i)
{
    return ctx.at(i).toMap().value("content").toString();
}

} // namespace

class TstChatSessions : public QObject
{
    Q_OBJECT
    QTemporaryDir dir;
    QString root() const { return dir.filePath("sessions"); }

private slots:
    void init() { QDir(dir.path()).removeRecursively(); QDir().mkpath(dir.path()); }

    void startsWithNothing() {
        ChatSessions s(root());
        QVERIFY(s.currentId().isEmpty());
        QVERIFY(s.sessions().isEmpty());
        QVERIFY(s.context().isEmpty());
        QVERIFY(s.lastTurn().isEmpty());
        QVERIFY(!QDir(root()).exists());   // nothing is created until there is something to keep
    }

    void theFirstTurnCreatesTheChatAndSavesIt() {
        ChatSessions s(root());
        QSignalSpy cur(&s, &ChatSessions::currentChanged);
        QSignalSpy list(&s, &ChatSessions::sessionsChanged);
        s.appendTurn("How do I undo a commit?", "Use git revert or git reset.");
        QVERIFY(!s.currentId().isEmpty());
        QCOMPARE(s.currentTitle(), QStringLiteral("How do I undo a commit?"));
        QCOMPARE(s.currentTurns(), 1);
        QVERIFY(cur.size() >= 1 && list.size() >= 1);
        QVERIFY(QFileInfo::exists(QDir(root()).filePath(s.currentId() + ".json")));
        QCOMPARE(s.sessions().size(), 1);
        const QVariantMap row = s.sessions().first().toMap();
        QCOMPARE(row.value("turns").toInt(), 1);
        QVERIFY(row.value("current").toBool());
        QCOMPARE(row.value("preview").toString(), QStringLiteral("How do I undo a commit?"));
    }

    void everyTurnIsSavedAsItHappens() {
        ChatSessions s(root());
        turns(s, 3);
        // Read the file itself, not the object: it is what survives a crash.
        QFile f(QDir(root()).filePath(s.currentId() + ".json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(doc.value("turns").toArray().size(), 3);
        QCOMPARE(doc.value("turns").toArray().last().toObject().value("q").toString(), QStringLiteral("q2"));
    }

    void aChatSurvivesARestartAndCanBeResumed() {
        QString id;
        {
            ChatSessions s(root());
            turns(s, 3);
            id = s.currentId();
        }
        ChatSessions later(root());
        QVERIFY(later.currentId().isEmpty());                 // a launch starts a fresh chat ...
        QCOMPARE(later.sessions().size(), 1);                 // ... but the old one is listed
        QVERIFY(later.open(id));
        QCOMPARE(later.currentId(), id);
        QCOMPARE(later.currentTurns(), 3);
        QCOMPARE(later.lastTurn().value("q").toString(), QStringLiteral("q2"));
        // And the conversation carries on where it stopped.
        later.appendTurn("q3", "answer 3");
        QCOMPARE(later.currentTurns(), 4);
        QCOMPARE(later.sessions().size(), 1);
    }

    void newChatStartsOverWithoutMakingAnEmptyOne() {
        ChatSessions s(root());
        turns(s, 2);
        const QString first = s.currentId();
        s.newChat();
        QVERIFY(s.currentId().isEmpty());
        QVERIFY(s.context().isEmpty());
        QCOMPARE(s.sessions().size(), 1);                     // nothing new on disk yet
        QTest::qWait(3);
        s.appendTurn("a fresh start", "ok");
        QVERIFY(s.currentId() != first);
        QCOMPARE(s.sessions().size(), 2);
        QCOMPARE(s.sessions().first().toMap().value("id").toString(), s.currentId());   // newest first
    }

    void contextAlternatesUserAndAssistant() {
        ChatSessions s(root());
        turns(s, 2);
        const QVariantList ctx = s.context();
        QCOMPARE(roles(ctx), (QStringList{ "user", "assistant", "user", "assistant" }));
        QCOMPARE(content(ctx, 0), QStringLiteral("q0"));
        QVERIFY(content(ctx, 3).startsWith("answer 1"));
    }

    void blankTurnsAreIgnoredAndTextIsCleaned() {
        ChatSessions s(root());
        s.appendTurn("   ", "an answer");
        s.appendTurn("a question", "   ");
        QVERIFY(s.currentId().isEmpty());
        s.appendTurn("Sneaky‮ question​ here", "ok");
        QCOMPARE(s.lastTurn().value("q").toString(), QStringLiteral("Sneaky question here"));
    }

    void titlesAreShort() {
        ChatSessions s(root());
        s.appendTurn(QString(300, 'x') + "?", "ok");
        QVERIFY(s.currentTitle().size() <= ChatSessions::kMaxTitleChars);
        QVERIFY(s.currentTitle().endsWith(QChar(0x2026)));
    }

    void hugeAnswersAreCappedWhenStored() {
        ChatSessions s(root());
        s.appendTurn("write a novel", QString(ChatSessions::kMaxAnswerChars * 2, 'w'));
        QCOMPARE(s.lastTurn().value("a").toString().size(), ChatSessions::kMaxAnswerChars);
    }

    // ---- branching ----------------------------------------------------------------------

    void branchingKeepsTheOriginalAndContinuesFromTheCopy() {
        ChatSessions s(root());
        turns(s, 4);
        const QString original = s.currentId();
        QTest::qWait(3);
        const QString b = s.branch();
        QVERIFY(!b.isEmpty() && b != original);
        QCOMPARE(s.currentId(), b);
        QCOMPARE(s.currentTurns(), 4);
        QVERIFY(s.currentTitle().endsWith("(branch)"));
        QCOMPARE(s.session(b).value("parentId").toString(), original);
        // Going on in the branch leaves the original as it was.
        s.appendTurn("only in the branch", "yes");
        QCOMPARE(s.session(b).value("turns").toList().size(), 5);
        QCOMPARE(s.session(original).value("turns").toList().size(), 4);
        QCOMPARE(s.sessions().size(), 2);
        const QVariantMap row = s.sessions().first().toMap();
        QCOMPARE(row.value("parentId").toString(), original);
        QVERIFY(!row.value("parentTitle").toString().isEmpty());
    }

    void branchingFromAnEarlierTurn_data() {
        QTest::addColumn<int>("atTurn");
        QTest::addColumn<int>("kept");
        QTest::newRow("first") << 0 << 1;
        QTest::newRow("middle") << 2 << 3;
        QTest::newRow("last") << 5 << 6;
        QTest::newRow("past the end") << 99 << 6;
        QTest::newRow("default") << -1 << 6;
    }
    void branchingFromAnEarlierTurn() {
        QFETCH(int, atTurn);
        QFETCH(int, kept);
        ChatSessions s(root());
        turns(s, 6);
        const QString original = s.currentId();
        QTest::qWait(3);
        const QString b = s.branch(original, atTurn);
        QCOMPARE(s.currentTurns(), kept);
        QCOMPARE(s.session(b).value("turns").toList().last().toMap().value("q").toString(), QStringLiteral("q%1").arg(kept - 1));
        QCOMPARE(s.session(original).value("turns").toList().size(), 6);
    }

    void aChatThatIsNotOpenCanBeBranched() {
        ChatSessions s(root());
        turns(s, 3, "old");
        const QString old = s.currentId();
        QTest::qWait(3);
        s.newChat();
        s.appendTurn("something else", "ok");
        QTest::qWait(3);
        const QString b = s.branch(old, 1);
        QCOMPARE(s.currentTurns(), 2);
        QCOMPARE(s.session(b).value("turns").toList().first().toMap().value("q").toString(), QStringLiteral("old0"));
    }

    void branchingNothingOrAnUnknownChatDoesNothing() {
        ChatSessions s(root());
        QVERIFY(s.branch().isEmpty());                   // no current chat
        QVERIFY(s.branch("s-does-not-exist").isEmpty());
        QVERIFY(s.branch("../../etc/passwd").isEmpty());
        QVERIFY(s.sessions().isEmpty());
    }

    void aSummaryOnlyComesAlongWhenItDescribesKeptTurns() {
        ChatSessions s(root());
        turns(s, 8);
        QVERIFY(s.setSummary(s.currentId(), "Talked about X and Y.", 5));   // covers turns 0..4
        const QString original = s.currentId();
        QTest::qWait(3);
        // Branch after turn 2: the summary describes turns that are not in the branch, so it stays behind.
        const QString early = s.branch(original, 2);
        QVERIFY(s.session(early).value("summary").toString().isEmpty());
        QCOMPARE(s.session(early).value("summarizedTurns").toInt(), 0);
        QCOMPARE(s.context().size(), 3 * 2);                                // the three turns, word for word
        QTest::qWait(3);
        // Branch after turn 6: the summary covers only what the branch has, so it is kept.
        const QString late = s.branch(original, 6);
        QCOMPARE(s.session(late).value("summary").toString(), QStringLiteral("Talked about X and Y."));
        QCOMPARE(s.session(late).value("summarizedTurns").toInt(), 5);
    }

    // ---- summaries ---------------------------------------------------------------------

    void asksForASummaryOnceEnoughOlderTurnsPileUp() {
        ChatSessions s(root());
        QSignalSpy wanted(&s, &ChatSessions::summaryWanted);
        turns(s, ChatSessions::kKeepVerbatimTurns + ChatSessions::kSummarizeAfterTurns - 1);
        QCOMPARE(wanted.size(), 0);                                          // one short
        turns(s, 1, "last");
        QCOMPARE(wanted.size(), 1);
        QCOMPARE(wanted.first().at(0).toString(), s.currentId());
        QCOMPARE(wanted.first().at(1).toInt(), ChatSessions::kKeepVerbatimTurns);
    }

    void bulkyTurnsAskSoonerThanManyShortOnes() {
        ChatSessions s(root());
        QSignalSpy wanted(&s, &ChatSessions::summaryWanted);
        turns(s, 2 + ChatSessions::kKeepVerbatimTurns, "big", 7000);          // two old turns of ~7000 chars each
        QCOMPARE(wanted.size(), 1);
    }

    void aSummaryJobHandsOverTheOlderTurnsOnly() {
        ChatSessions s(root());
        turns(s, 12);
        const QVariantMap job = s.summaryJob(s.currentId());
        QCOMPARE(job.value("upTo").toInt(), 12 - ChatSessions::kKeepVerbatimTurns);
        QCOMPARE(job.value("turns").toList().size(), 8);
        QCOMPARE(job.value("turns").toList().first().toMap().value("q").toString(), QStringLiteral("q0"));
        QCOMPARE(job.value("turns").toList().last().toMap().value("q").toString(), QStringLiteral("q7"));   // q8..q11 stay word for word
        QVERIFY(job.value("prior").toString().isEmpty());
        QVERIFY(s.summaryJob(s.currentId(), 100).isEmpty());                // nothing to fold
        QVERIFY(s.summaryJob("s-unknown").isEmpty());
    }

    void aStoredSummaryReplacesTheTurnsItCovers() {
        ChatSessions s(root());
        turns(s, 12);
        QVERIFY(s.setSummary(s.currentId(), "We set up the build and fixed two bugs.", 8));
        const QVariantList ctx = s.context();
        QCOMPARE(ctx.size(), 2 + 4 * 2);                                    // summary pair + the 4 newer turns
        QVERIFY(content(ctx, 0).startsWith(ChatSessions::kSummaryPrefix));
        QVERIFY(content(ctx, 0).contains("We set up the build"));
        QVERIFY(content(ctx, 0).contains("not instructions"));              // background, not orders
        QCOMPARE(roles(ctx).mid(0, 2), (QStringList{ "user", "assistant" }));
        QCOMPARE(content(ctx, 2), QStringLiteral("q8"));                    // verbatim from where the summary stops
        // The next job continues from there and carries the old summary.
        turns(s, 10, "more");
        const QVariantMap job = s.summaryJob(s.currentId());
        QVERIFY(job.value("prior").toString().contains("build"));
        QCOMPARE(job.value("turns").toList().first().toMap().value("q").toString(), QStringLiteral("q8"));
    }

    void summariesThatMakeNoSenseAreIgnored() {
        ChatSessions s(root());
        turns(s, 12);
        const QString id = s.currentId();
        QVERIFY(!s.setSummary(id, "   ", 8));                  // empty
        QVERIFY(!s.setSummary(id, "ok", 0));                   // covers nothing
        QVERIFY(!s.setSummary(id, "ok", 99));                  // more turns than exist
        QVERIFY(!s.setSummary("s-gone", "ok", 3));             // chat is gone
        QVERIFY(s.setSummary(id, "first", 8));
        QVERIFY(!s.setSummary(id, "stale, older", 5));         // a late reply for an older job
        QVERIFY(!s.setSummary(id, "same", 8));
        QCOMPARE(s.session(id).value("summary").toString(), QStringLiteral("first"));
        QVERIFY(s.setSummary(id, "longer", 9));
    }

    void summariesAreCappedAndCleaned() {
        ChatSessions s(root());
        turns(s, 12);
        QVERIFY(s.setSummary(s.currentId(), QString("x​").repeated(ChatSessions::kMaxSummaryChars), 8));
        const QString stored = s.session(s.currentId()).value("summary").toString();
        QCOMPARE(stored.size(), ChatSessions::kMaxSummaryChars);
        QVERIFY(!stored.contains(QChar(0x200B)));
    }

    void aSummaryStopsFurtherRequestsUntilMoreTurnsPileUp() {
        ChatSessions s(root());
        turns(s, 14);
        QSignalSpy wanted(&s, &ChatSessions::summaryWanted);
        QVERIFY(s.setSummary(s.currentId(), "covers 10", 10));
        turns(s, 3);
        QCOMPARE(wanted.size(), 0);
    }

    void afterAFailedSummaryItWaitsTwoTurns() {
        ChatSessions s(root());
        QSignalSpy wanted(&s, &ChatSessions::summaryWanted);
        turns(s, 14);
        QVERIFY(wanted.size() >= 1);
        wanted.clear();
        s.noteSummaryFailed(s.currentId());
        turns(s, 1);
        QCOMPARE(wanted.size(), 0);          // too soon
        turns(s, 1);
        QCOMPARE(wanted.size(), 1);          // two turns on, try again
    }

    void summaryPersistsAcrossARestart() {
        QString id;
        {
            ChatSessions s(root());
            turns(s, 12);
            id = s.currentId();
            QVERIFY(s.setSummary(id, "We did things.", 8));
        }
        ChatSessions later(root());
        QVERIFY(later.sessions().first().toMap().value("summarized").toBool());
        QVERIFY(later.open(id));
        QCOMPARE(later.context().size(), 2 + 4 * 2);
    }

    void compactAsksNowEvenWhenNotDue() {
        ChatSessions s(root());
        QSignalSpy wanted(&s, &ChatSessions::summaryWanted);
        turns(s, 5);
        QCOMPARE(wanted.size(), 0);
        QVERIFY(s.requestSummary());                                  // /compact keeps only the newest 2
        QCOMPARE(wanted.size(), 1);
        QCOMPARE(wanted.first().at(1).toInt(), 2);
        QCOMPARE(s.summaryJob(s.currentId(), 2).value("turns").toList().size(), 3);
        s.newChat();
        QVERIFY(!s.requestSummary());                                 // nothing open, nothing to do
        ChatSessions tiny(root() + "2");
        turns(tiny, 2);
        QVERIFY(!tiny.requestSummary());                              // two turns: nothing older to fold
    }

    // ---- housekeeping -------------------------------------------------------------------

    void removesAChatAndItsFile() {
        ChatSessions s(root());
        turns(s, 2);
        const QString a = s.currentId();
        QTest::qWait(3);
        s.newChat();
        turns(s, 1, "other");
        const QString b = s.currentId();
        QVERIFY(s.remove(a));
        QVERIFY(!QFileInfo::exists(QDir(root()).filePath(a + ".json")));
        QCOMPARE(s.sessions().size(), 1);
        QCOMPARE(s.currentId(), b);                                     // removing another chat leaves this one open
        QVERIFY(!s.remove(a));                                          // already gone
        QVERIFY(s.remove(b));
        QVERIFY(s.currentId().isEmpty());                               // removing the open one closes it
        QVERIFY(s.sessions().isEmpty());
    }

    void idsCannotNameFilesOutsideTheFolder() {
        ChatSessions s(root());
        turns(s, 1);
        QFile outside(dir.filePath("secret.json"));
        QVERIFY(outside.open(QIODevice::WriteOnly));
        outside.write("{\"id\":\"../secret\"}");
        outside.close();
        QVERIFY(!s.remove("../secret"));
        QVERIFY(QFileInfo::exists(dir.filePath("secret.json")));
        QVERIFY(!s.open("../secret"));
        QVERIFY(!s.rename("../secret", "x"));
        QVERIFY(s.session("..\\secret").isEmpty());
    }

    void renamesAChat() {
        ChatSessions s(root());
        turns(s, 1);
        const QString id = s.currentId();
        QVERIFY(s.rename(id, "  Build   fixes​  "));
        QCOMPARE(s.currentTitle(), QStringLiteral("Build fixes"));
        QCOMPARE(s.sessions().first().toMap().value("title").toString(), QStringLiteral("Build fixes"));
        QVERIFY(!s.rename(id, "   "));
        QVERIFY(!s.rename("s-nope", "x"));
        ChatSessions later(root());
        QCOMPARE(later.sessions().first().toMap().value("title").toString(), QStringLiteral("Build fixes"));
    }

    void clearsEverything() {
        ChatSessions s(root());
        turns(s, 1);
        QTest::qWait(3);
        s.newChat();
        turns(s, 1, "b");
        s.clearAll();
        QVERIFY(s.sessions().isEmpty());
        QVERIFY(s.currentId().isEmpty());
        QCOMPARE(QDir(root()).entryList({ "s-*.json" }).size(), 0);
    }

    void keepsOnlyTheNewestHundredChats() {
        ChatSessions s(root());
        QString first;
        for (int i = 0; i < ChatSessions::kMaxSessions + 3; ++i) {
            s.newChat();
            s.appendTurn(QStringLiteral("chat %1").arg(i), "ok");
            if (i == 0)
                first = s.currentId();
            QTest::qWait(1);
        }
        QCOMPARE(s.sessions().size(), ChatSessions::kMaxSessions);
        QVERIFY(!QFileInfo::exists(QDir(root()).filePath(first + ".json")));          // the oldest went
        QCOMPARE(s.sessions().first().toMap().value("title").toString(), QStringLiteral("chat 102"));
        QVERIFY(s.sessions().first().toMap().value("current").toBool());
    }

    void aLongChatDropsItsOldestTurnsAndKeepsTheSummaryHonest() {
        ChatSessions s(root());
        turns(s, ChatSessions::kMaxTurns, "t", 5);
        QVERIFY(s.setSummary(s.currentId(), "covers the first hundred", 100));
        turns(s, 5, "late", 5);
        QCOMPARE(s.currentTurns(), ChatSessions::kMaxTurns);
        QCOMPARE(s.session(s.currentId()).value("turns").toList().first().toMap().value("q").toString(), QStringLiteral("t5"));
        QCOMPARE(s.session(s.currentId()).value("summarizedTurns").toInt(), 95);   // five of the covered turns are gone
    }

    void brokenFilesAreSkippedNotFatal() {
        {
            ChatSessions s(root());
            turns(s, 2);
        }
        QFile bad(QDir(root()).filePath("s-broken.json"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{ not json");
        bad.close();
        QFile mismatch(QDir(root()).filePath("s-mismatch.json"));
        QVERIFY(mismatch.open(QIODevice::WriteOnly));
        mismatch.write("{\"id\":\"s-someone-else\",\"turns\":[{\"q\":\"x\",\"a\":\"y\"}]}");
        mismatch.close();
        ChatSessions later(root());
        QCOMPARE(later.sessions().size(), 1);
        QVERIFY(!later.open("s-broken"));
        QVERIFY(!later.open("s-mismatch"));
    }

    void aSummaryClaimingMoreTurnsThanExistIsClamped() {
        QFile f(QDir(root()).filePath("s-lying.json"));
        QDir().mkpath(root());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"id\":\"s-lying\",\"title\":\"t\",\"summary\":\"S\",\"summarizedTurns\":50,"
                "\"turns\":[{\"q\":\"a\",\"a\":\"b\"},{\"q\":\"c\",\"a\":\"d\"}]}");
        f.close();
        ChatSessions s(root());
        QVERIFY(s.open("s-lying"));
        QCOMPARE(s.session("s-lying").value("summarizedTurns").toInt(), 2);
        QCOMPARE(s.context().size(), 2);                         // the summary pair and no turns, never a negative range
    }

    void aSummaryWithoutTextIsNotCounted() {
        QFile f(QDir(root()).filePath("s-empty.json"));
        QDir().mkpath(root());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"id\":\"s-empty\",\"title\":\"t\",\"summary\":\"\",\"summarizedTurns\":2,"
                "\"turns\":[{\"q\":\"a\",\"a\":\"b\"},{\"q\":\"c\",\"a\":\"d\"}]}");
        f.close();
        ChatSessions s(root());
        QVERIFY(s.open("s-empty"));
        QCOMPARE(s.context().size(), 4);                         // both turns word for word
    }
};

QTEST_GUILESS_MAIN(TstChatSessions)
#include "tst_chatsessions.moc"
