// Unit tests for Buddy's clarifying questions (src/UserQuestions): input
// validation and sanitising, answer normalising, and the text the model reads.
#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>

#include "UserQuestions.h"

namespace {

QJsonObject option(const QString &label, const QString &description = QString())
{
    return QJsonObject{ { "label", label }, { "description", description } };
}

QJsonObject question(const QString &text, QJsonArray options, const QString &header = QString(), bool multi = false)
{
    return QJsonObject{ { "question", text }, { "header", header }, { "multiSelect", multi }, { "options", options } };
}

QJsonObject input(QJsonArray questions)
{
    return QJsonObject{ { "questions", questions } };
}

QJsonObject goodQuestion(const QString &text = QStringLiteral("Which format?"))
{
    return question(text, QJsonArray{ option("PDF", "Fixed layout"), option("Word") }, "Format");
}

} // namespace

class TstUserQuestions : public QObject
{
    Q_OBJECT
private slots:
    void acceptsAWellFormedQuestion() {
        const uq::Parsed p = uq::parse(input(QJsonArray{ goodQuestion() }));
        QVERIFY2(p.valid(), qPrintable(p.error));
        QCOMPARE(p.questions.size(), 1);
        QCOMPARE(p.questions.first().question, QStringLiteral("Which format?"));
        QCOMPARE(p.questions.first().header, QStringLiteral("Format"));
        QCOMPARE(p.questions.first().options.size(), 2);
        QCOMPARE(p.questions.first().options.first().description, QStringLiteral("Fixed layout"));
        QVERIFY(!p.questions.first().multiSelect);
    }

    void optionsMayBeBareStrings() {
        const uq::Parsed p = uq::parse(input(QJsonArray{
            question("Pick one?", QJsonArray{ "Yes", "No" }) }));
        QVERIFY2(p.valid(), qPrintable(p.error));
        QCOMPARE(p.questions.first().options.at(1).label, QStringLiteral("No"));
    }

    void rejectsBadShapes_data() {
        QTest::addColumn<QJsonObject>("input");
        QTest::addColumn<QString>("error");
        QTest::newRow("no questions") << QJsonObject{} << "array";
        QTest::newRow("empty array") << input(QJsonArray{}) << "array";
        QTest::newRow("not an array") << QJsonObject{ { "questions", "why?" } } << "array";
        QTest::newRow("too many questions")
            << input(QJsonArray{ goodQuestion("a?"), goodQuestion("b?"), goodQuestion("c?"), goodQuestion("d?"), goodQuestion("e?") })
            << "at most 4";
        QTest::newRow("empty question") << input(QJsonArray{ question("  ", QJsonArray{ option("A"), option("B") }) }) << "no 'question'";
        QTest::newRow("one option") << input(QJsonArray{ question("Sure?", QJsonArray{ option("Yes") }) }) << "2-4 options";
        QTest::newRow("five options")
            << input(QJsonArray{ question("Which?", QJsonArray{ option("1"), option("2"), option("3"), option("4"), option("5") }) })
            << "2-4 options";
        QTest::newRow("blank label") << input(QJsonArray{ question("Which?", QJsonArray{ option("A"), option("  ") }) }) << "no label";
        QTest::newRow("duplicate label") << input(QJsonArray{ question("Which?", QJsonArray{ option("A"), option("a") }) }) << "twice";
        QTest::newRow("duplicate question") << input(QJsonArray{ goodQuestion("Same?"), goodQuestion("same?") }) << "repeats";
    }
    void rejectsBadShapes() {
        QFETCH(QJsonObject, input);
        QFETCH(QString, error);
        const uq::Parsed p = uq::parse(input);
        QVERIFY(!p.valid());
        QVERIFY2(p.error.contains(error), qPrintable(p.error));
    }

    void stripsHiddenCharactersAndCapsLengths() {
        // Zero-width and bidi characters could make an option say one thing
        // and mean another; very long text would push the chips off screen.
        const QString sneaky = QStringLiteral("Delete") + QChar(0x202E)
                               + QStringLiteral(" files") + QChar(0x200B);
        const uq::Parsed p = uq::parse(input(QJsonArray{ question(
            QString(500, 'q'), QJsonArray{ option(sneaky, QString(900, 'd')), option(QString(200, 'l')) },
            "A very long header indeed") }));
        QVERIFY2(p.valid(), qPrintable(p.error));
        const uq::Question &q = p.questions.first();
        QVERIFY(q.question.size() <= uq::kMaxQuestionChars);
        QVERIFY(q.header.size() <= uq::kMaxHeaderChars);
        QCOMPARE(q.options.first().label, QStringLiteral("Delete files"));
        QVERIFY(q.options.first().description.size() <= uq::kMaxDescriptionChars);
        QVERIFY(q.options.last().label.size() <= uq::kMaxLabelChars);
        QVERIFY(q.options.last().label.endsWith(QChar(0x2026)));
    }

    void inputSchemaMatchesTheLimits() {
        const QJsonObject schema = uq::inputSchema();
        QCOMPARE(schema.value("type").toString(), QStringLiteral("object"));
        const QJsonObject qs = schema.value("properties").toObject().value("questions").toObject();
        QCOMPARE(qs.value("maxItems").toInt(), uq::kMaxQuestions);
        const QJsonObject opts = qs.value("items").toObject().value("properties").toObject().value("options").toObject();
        QCOMPARE(opts.value("minItems").toInt(), uq::kMinOptions);
        QCOMPARE(opts.value("maxItems").toInt(), uq::kMaxOptions);
        QVERIFY(schema.value("required").toArray().contains("questions"));
    }

    void toVariantCarriesWhatTheCardNeeds() {
        const uq::Parsed p = uq::parse(input(QJsonArray{ goodQuestion() }));
        const QVariantList v = uq::toVariant(p.questions);
        QCOMPARE(v.size(), 1);
        const QVariantMap q = v.first().toMap();
        QCOMPARE(q.value("question").toString(), QStringLiteral("Which format?"));
        QCOMPARE(q.value("header").toString(), QStringLiteral("Format"));
        QVERIFY(q.contains("multiSelect"));
        QCOMPARE(q.value("options").toList().size(), 2);
        QCOMPARE(q.value("options").toList().first().toMap().value("label").toString(), QStringLiteral("PDF"));
    }

    // ---- answers ---------------------------------------------------------------

    void singleSelectKeepsAtMostOneChoice() {
        const uq::Parsed p = uq::parse(input(QJsonArray{ goodQuestion() }));
        const QList<uq::Answer> a = uq::normalizeAnswers(p.questions,
            QVariantList{ QVariantMap{ { "selected", QVariantList{ 1, 0 } } } });
        QCOMPARE(a.size(), 1);
        QCOMPARE(a.first().selected, (QList<int>{ 0 }));   // sorted, then trimmed to one
    }

    void multiSelectKeepsSeveralOnceEach() {
        const uq::Parsed p = uq::parse(input(QJsonArray{ question("Which?", QJsonArray{ option("A"), option("B"), option("C") }, "", true) }));
        const QList<uq::Answer> a = uq::normalizeAnswers(p.questions,
            QVariantList{ QVariantMap{ { "selected", QVariantList{ 2, 0, 2, 0 } } } });
        QCOMPARE(a.first().selected, (QList<int>{ 0, 2 }));
    }

    void outOfRangeAndJunkIndicesAreDropped() {
        const uq::Parsed p = uq::parse(input(QJsonArray{ goodQuestion() }));
        const QList<uq::Answer> a = uq::normalizeAnswers(p.questions,
            QVariantList{ QVariantMap{ { "selected", QVariantList{ -1, 7, "x", 99 } } } });
        QVERIFY(a.first().selected.isEmpty());
        QVERIFY(!uq::complete(a));
    }

    void typedAnswerReplacesTheChipOnSingleChoice() {
        const uq::Parsed p = uq::parse(input(QJsonArray{ goodQuestion() }));
        const QList<uq::Answer> a = uq::normalizeAnswers(p.questions,
            QVariantList{ QVariantMap{ { "selected", QVariantList{ 0 } }, { "other", "  Markdown please  " } } });
        QVERIFY(a.first().selected.isEmpty());
        QCOMPARE(a.first().other, QStringLiteral("Markdown please"));
        QVERIFY(uq::complete(a));
    }

    void typedAnswerAddsToChipsOnMultiSelect() {
        const uq::Parsed p = uq::parse(input(QJsonArray{ question("Which?", QJsonArray{ option("A"), option("B") }, "", true) }));
        const QList<uq::Answer> a = uq::normalizeAnswers(p.questions,
            QVariantList{ QVariantMap{ { "selected", QVariantList{ 1 } }, { "other", "and C" } } });
        QCOMPARE(a.first().selected, (QList<int>{ 1 }));
        QCOMPARE(a.first().other, QStringLiteral("and C"));
    }

    void typedAnswerIsCappedAndCleaned() {
        const uq::Parsed p = uq::parse(input(QJsonArray{ goodQuestion() }));
        const QList<uq::Answer> a = uq::normalizeAnswers(p.questions,
            QVariantList{ QVariantMap{ { "other", QString("x​").repeated(900) } } });
        QCOMPARE(a.first().other.size(), uq::kMaxOtherChars);
        QVERIFY(!a.first().other.contains(QChar(0x200B)));
    }

    void missingAnswersLeaveTheSetIncomplete() {
        const uq::Parsed p = uq::parse(input(QJsonArray{ goodQuestion("a?"), goodQuestion("b?") }));
        const QList<uq::Answer> a = uq::normalizeAnswers(p.questions,
            QVariantList{ QVariantMap{ { "selected", QVariantList{ 0 } } } });   // second one never answered
        QCOMPARE(a.size(), 2);
        QVERIFY(!uq::complete(a));
        QVERIFY(!uq::complete({}));
    }

    void formatsAnswersForTheModel() {
        const uq::Parsed p = uq::parse(input(QJsonArray{
            goodQuestion("Which format?"),
            question("Which extras?", QJsonArray{ option("TOC"), option("Index"), option("Cover") }, "", true) }));
        const QList<uq::Answer> a = uq::normalizeAnswers(p.questions, QVariantList{
            QVariantMap{ { "selected", QVariantList{ 0 } } },
            QVariantMap{ { "selected", QVariantList{ 0, 2 } }, { "other", "Appendix" } } });
        const QString text = uq::formatAnswers(p.questions, a);
        QVERIFY(text.contains("Q1. Which format?"));
        QVERIFY(text.contains("A1. PDF"));
        QVERIFY(text.contains("Q2. Which extras?"));
        QVERIFY2(text.contains("A2. TOC; Cover; (their own answer) Appendix"), qPrintable(text));
        QVERIFY(text.contains("do not ask the same thing again"));
    }

    void summaryIsOneShortLine() {
        const uq::Parsed one = uq::parse(input(QJsonArray{ goodQuestion("Which format?") }));
        QCOMPARE(uq::summary(one.questions), QStringLiteral("Ask: Which format?"));
        const uq::Parsed two = uq::parse(input(QJsonArray{ goodQuestion("a?"), goodQuestion("b?") }));
        QCOMPARE(uq::summary(two.questions), QStringLiteral("Ask: a? (+1 more)"));
        const uq::Parsed longQ = uq::parse(input(QJsonArray{ goodQuestion(QString(200, 'z') + "?") }));
        QVERIFY(uq::summary(longQ.questions).size() < 70);
        QVERIFY(uq::summary({}).size() > 0);
    }

    // ---- answers given out loud ---------------------------------------------------------

    static uq::Question fmtQuestion(bool multi = false) {
        return uq::parse(input(QJsonArray{ question("Which format?", QJsonArray{ option("PDF"), option("Word document"),
                                                                                 option("Plain text") }, "Format", multi) })).questions.first();
    }

    void aSpokenOrdinalOrNumberPicksThatOption_data() {
        QTest::addColumn<QString>("said");
        QTest::addColumn<int>("index");
        QTest::newRow("first") << "the first one" << 0;
        QTest::newRow("second") << "Second, please." << 1;
        QTest::newRow("third") << "I'll take the third" << 2;
        QTest::newRow("last") << "the last one" << 2;
        QTest::newRow("option two") << "option two" << 1;
        QTest::newRow("number 3") << "number 3" << 2;
        QTest::newRow("bare number") << "2" << 1;
    }
    void aSpokenOrdinalOrNumberPicksThatOption() {
        QFETCH(QString, said);
        QFETCH(int, index);
        const uq::Answer a = uq::matchSpoken(fmtQuestion(), said);
        QCOMPARE(a.selected, (QList<int>{ index }));
        QVERIFY(a.other.isEmpty());
    }

    void spokenWordsFromALabelPickThatOption() {
        QCOMPARE(uq::matchSpoken(fmtQuestion(), "PDF").selected, (QList<int>{ 0 }));
        QCOMPARE(uq::matchSpoken(fmtQuestion(), "make it a pdf please").selected, (QList<int>{ 0 }));
        QCOMPARE(uq::matchSpoken(fmtQuestion(), "the word document").selected, (QList<int>{ 1 }));
        QCOMPARE(uq::matchSpoken(fmtQuestion(), "word").selected, (QList<int>{ 1 }));       // part of the label is enough
        QCOMPARE(uq::matchSpoken(fmtQuestion(), "just plain text").selected, (QList<int>{ 2 }));
    }

    void multiSelectTakesEveryOptionNamed() {
        const uq::Question q = uq::parse(input(QJsonArray{ question("Which extras?",
            QJsonArray{ option("Table of contents"), option("Index"), option("Cover page") }, "", true) })).questions.first();
        const uq::Answer a = uq::matchSpoken(q, "the index and the cover page");
        QCOMPARE(a.selected, (QList<int>{ 1, 2 }));
    }

    void wordsThatFitNoOptionBecomeTheUsersOwnAnswer() {
        const uq::Answer a = uq::matchSpoken(fmtQuestion(), "markdown, with a table of contents");
        QVERIFY(a.selected.isEmpty());
        QCOMPARE(a.other, QStringLiteral("markdown, with a table of contents"));
        // naming two single-choice options equally is not an answer either
        const uq::Answer both = uq::matchSpoken(fmtQuestion(), "either PDF or plain text");
        QVERIFY(both.selected.isEmpty());
        QVERIFY(!both.other.isEmpty());
    }

    void spokenInputCannotSelectAnythingNotOffered() {
        const uq::Answer far = uq::matchSpoken(fmtQuestion(), "option four");     // there are only three
        QVERIFY(far.selected.isEmpty());
        QVERIFY(uq::matchSpoken(fmtQuestion(), "").empty());
        // invisible characters in what was heard are dropped
        QCOMPARE(uq::matchSpoken(fmtQuestion(), QStringLiteral("p\u200bdf")).other.contains(QChar(0x200b)), false);
    }

    void severalQuestionsInOneBreath() {
        const uq::Parsed p = uq::parse(input(QJsonArray{ goodQuestion("Which format?"),
            question("Which size?", QJsonArray{ option("Small"), option("Large") }) }));
        const QList<uq::Answer> a = uq::matchSpokenAll(p.questions, "word; large");
        QCOMPARE(a.size(), 2);
        QCOMPARE(a.at(0).selected, (QList<int>{ 1 }));
        QCOMPARE(a.at(1).selected, (QList<int>{ 1 }));
        // one part answers every question
        const QList<uq::Answer> same = uq::matchSpokenAll(p.questions, "the second one");
        QCOMPARE(same.at(0).selected, (QList<int>{ 1 }));
        QCOMPARE(same.at(1).selected, (QList<int>{ 1 }));
        QVERIFY(uq::complete(a));
    }

    void spokenDecisions_data() {
        QTest::addColumn<QString>("said");
        QTest::addColumn<int>("decision");
        using D = uq::Decision;
        QTest::newRow("yes") << "yes" << int(D::Approve);
        QTest::newRow("go ahead") << "Sure, go ahead." << int(D::Approve);
        QTest::newRow("okay") << "okay" << int(D::Approve);
        QTest::newRow("do it") << "do it" << int(D::Approve);
        QTest::newRow("always") << "yes, always" << int(D::ApproveAlways);
        QTest::newRow("every time") << "sure, every time" << int(D::ApproveAlways);
        QTest::newRow("no") << "no" << int(D::Deny);
        QTest::newRow("dont") << "don't do that" << int(D::Deny);
        QTest::newRow("do not") << "please do not" << int(D::Deny);
        QTest::newRow("stop") << "stop" << int(D::Deny);
        QTest::newRow("no stop") << "no, stop" << int(D::Deny);
        QTest::newRow("not now") << "not now" << int(D::Deny);
        QTest::newRow("first verdict wins") << "yes but not the second one" << int(D::Approve);
        QTest::newRow("unclear") << "hmm what does that mean" << int(D::Unclear);
        QTest::newRow("empty") << "" << int(D::Unclear);
        QTest::newRow("a question back") << "which file is it" << int(D::Unclear);
    }
    void spokenDecisions() {
        QFETCH(QString, said);
        QFETCH(int, decision);
        QCOMPARE(int(uq::spokenDecision(said)), decision);
    }

    void aQuestionIsReadOutWithItsOptions() {
        const uq::Parsed p = uq::parse(input(QJsonArray{ goodQuestion() }));
        const QString t = uq::spokenForm(p.questions);
        QVERIFY2(t.contains("Which format?") && t.contains("PDF, or Word"), qPrintable(t));
        const uq::Parsed two = uq::parse(input(QJsonArray{ goodQuestion("a?"), goodQuestion("b?") }));
        QVERIFY(uq::spokenForm(two.questions).contains("Question 2: b?"));
    }

    void theDefaultOptionIsTheRecommendedOneElseTheFirst() {
        const uq::Parsed p = uq::parse(input(QJsonArray{
            question("Pick?", QJsonArray{ option("A"), option("B (Recommended)"), option("C") }),
            question("Pick again?", QJsonArray{ option("X"), option("Y") }) }));
        QCOMPARE(uq::defaultOption(p.questions.at(0)), 1);
        QCOMPARE(uq::defaultOption(p.questions.at(1)), 0);
    }
};

QTEST_GUILESS_MAIN(TstUserQuestions)
#include "tst_userquestions.moc"
