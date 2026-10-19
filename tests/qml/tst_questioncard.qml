import QtQuick
import QtTest
import NochQml

// Logic of Buddy's clarifying-question card: tap-to-answer, multi-select,
// "Other…", several questions at once, and what it hands back to C++.
Item {
    id: root
    width: 520
    height: 600

    function q(text, options, multi, header) {
        return { question: text, header: header || "", multiSelect: !!multi,
                 options: options.map(function (o) { return { label: o, description: "" } }) }
    }

    BuddyQuestionCard {
        id: card
        width: 480
        visible: true
        request: ({ id: "q1", secondsLeft: 120, title: "x",
                    questions: [root.q("Which format?", ["PDF", "Word", "Markdown"], false, "Format")] })
    }

    SignalSpy { id: answeredSpy; target: card; signalName: "answered" }
    SignalSpy { id: skippedSpy; target: card; signalName: "skipped" }

    TestCase {
        name: "QuestionCard"
        when: windowShown

        function init() {
            answeredSpy.clear()
            skippedSpy.clear()
            card.request = { id: "q" + Math.random(), secondsLeft: 120, title: "x",
                             questions: [root.q("Which format?", ["PDF", "Word", "Markdown"], false, "Format")] }
        }

        function test_startsUnanswered() {
            compare(card.questions.length, 1)
            verify(!card.complete)
            verify(card.tapToAnswer)
            compare(card.picks.length, 1)
            compare(card.picks[0].length, 0)
        }

        function test_tapOnALoneQuestionAnswersAtOnce() {
            card.choose(0, 1)
            compare(answeredSpy.count, 1)
            const a = answeredSpy.signalArguments[0][0]
            compare(a.length, 1)
            compare(a[0].selected.length, 1)
            compare(a[0].selected[0], 1)
            compare(a[0].other, "")
        }

        function test_singleChoiceKeepsOnlyTheLatestChip() {
            card.request = { id: "multi-q", secondsLeft: 60, title: "x", questions: [
                root.q("Which format?", ["PDF", "Word"], false), root.q("Which size?", ["A4", "Letter"], false) ] }
            verify(!card.tapToAnswer)               // two questions: collect, then Send
            card.choose(0, 0)
            card.choose(0, 1)
            compare(card.picks[0].length, 1)
            compare(card.picks[0][0], 1)
            compare(answeredSpy.count, 0)           // nothing sent yet
        }

        function test_severalQuestionsNeedEveryAnswerBeforeSend() {
            card.request = { id: "two", secondsLeft: 60, title: "x", questions: [
                root.q("Which format?", ["PDF", "Word"], false), root.q("Which size?", ["A4", "Letter"], false) ] }
            verify(!card.complete)
            card.choose(0, 0)
            verify(!card.complete)
            card.send()                             // incomplete: ignored
            compare(answeredSpy.count, 0)
            card.choose(1, 1)
            verify(card.complete)
            card.send()
            compare(answeredSpy.count, 1)
            const a = answeredSpy.signalArguments[0][0]
            compare(a.length, 2)
            compare(a[0].selected[0], 0)
            compare(a[1].selected[0], 1)
        }

        function test_multiSelectToggles() {
            card.request = { id: "multi", secondsLeft: 60, title: "x", questions: [
                root.q("Which extras?", ["TOC", "Index", "Cover"], true) ] }
            verify(!card.tapToAnswer)               // multi-select always waits for Send
            card.choose(0, 0)
            card.choose(0, 2)
            compare(answeredSpy.count, 0)
            compare(card.picks[0].length, 2)
            verify(card.isPicked(0, 0) && card.isPicked(0, 2) && !card.isPicked(0, 1))
            card.choose(0, 0)                       // tap again: off
            verify(!card.isPicked(0, 0))
            card.send()
            compare(answeredSpy.signalArguments[0][0][0].selected.length, 1)
            compare(answeredSpy.signalArguments[0][0][0].selected[0], 2)
        }

        function test_multiSelectWithNothingPickedCannotBeSent() {
            card.request = { id: "multi2", secondsLeft: 60, title: "x", questions: [
                root.q("Which extras?", ["TOC", "Index"], true) ] }
            verify(!card.complete)
            card.send()
            compare(answeredSpy.count, 0)
        }

        function test_typingYourOwnAnswer() {
            card.openOther(0)
            verify(card.typing[0])
            verify(!card.complete)                  // an empty line is not an answer
            card.setTyped(0, "   ")
            verify(!card.complete)
            card.setTyped(0, "  Markdown please  ")
            verify(card.complete)
            card.send()
            compare(answeredSpy.count, 1)
            const a = answeredSpy.signalArguments[0][0][0]
            compare(a.other, "Markdown please")     // trimmed
            compare(a.selected.length, 0)
        }

        function test_aChipReplacesATypedAnswerOnSingleChoice() {
            card.request = { id: "two2", secondsLeft: 60, title: "x", questions: [
                root.q("Which format?", ["PDF", "Word"], false), root.q("Which size?", ["A4", "Letter"], false) ] }
            card.openOther(0)
            card.setTyped(0, "something else")
            card.choose(0, 1)
            verify(!card.typing[0])
            card.choose(1, 0)
            card.send()
            const a = answeredSpy.signalArguments[0][0]
            compare(a[0].other, "")
            compare(a[0].selected[0], 1)
        }

        function test_openingOtherClearsTheChipOnSingleChoice() {
            card.request = { id: "two3", secondsLeft: 60, title: "x", questions: [
                root.q("Which format?", ["PDF", "Word"], false), root.q("Which size?", ["A4", "Letter"], false) ] }
            card.choose(0, 0)
            card.openOther(0)
            compare(card.picks[0].length, 0)
        }

        function test_otherAddsToChipsOnMultiSelect() {
            card.request = { id: "multi3", secondsLeft: 60, title: "x", questions: [
                root.q("Which extras?", ["TOC", "Index"], true) ] }
            card.choose(0, 1)
            card.openOther(0)
            card.setTyped(0, "Appendix")
            card.send()
            const a = answeredSpy.signalArguments[0][0][0]
            compare(a.selected[0], 1)
            compare(a.other, "Appendix")
        }

        function test_aTickOfTheCountdownDoesNotWipeTheAnswers() {
            card.request = { id: "keep", secondsLeft: 60, title: "x", questions: [
                root.q("Which format?", ["PDF", "Word"], false), root.q("Which size?", ["A4", "Letter"], false) ] }
            card.choose(0, 1)
            // The provider re-publishes the question every second with fewer seconds left.
            card.request = { id: "keep", secondsLeft: 59, title: "x", questions: [
                root.q("Which format?", ["PDF", "Word"], false), root.q("Which size?", ["A4", "Letter"], false) ] }
            compare(card.picks[0][0], 1)
            compare(card.secondsLeft, 59)
        }

        function test_theQuestionListIsNotRebuiltOnEveryTick() {
            const qs = [root.q("Which format?", ["PDF", "Word"], false), root.q("Which size?", ["A4", "Letter"], false)]
            card.request = { id: "stable", secondsLeft: 60, title: "x", questions: qs }
            const before = card.questions
            // Same question, one second later, as a fresh object (as the provider publishes it).
            card.request = { id: "stable", secondsLeft: 59, title: "x",
                             questions: [root.q("Which format?", ["PDF", "Word"], false), root.q("Which size?", ["A4", "Letter"], false)] }
            verify(card.questions === before)       // the same array: the chips (and the typing line) are kept
            compare(card.secondsLeft, 59)
            // A different question does replace it.
            card.request = { id: "other", secondsLeft: 60, title: "x", questions: [root.q("New?", ["A", "B"], false)] }
            verify(card.questions !== before)
            compare(card.questions.length, 1)
        }

        function test_aNewQuestionStartsClean() {
            card.request = { id: "first", secondsLeft: 60, title: "x", questions: [
                root.q("Which format?", ["PDF", "Word"], false), root.q("Which size?", ["A4", "Letter"], false) ] }
            card.choose(0, 1)
            card.openOther(1)
            card.setTyped(1, "stale")
            card.request = { id: "second", secondsLeft: 60, title: "x", questions: [
                root.q("Another?", ["Yes", "No"], false), root.q("And?", ["A", "B"], false) ] }
            compare(card.picks[0].length, 0)
            verify(!card.typing[1])
            compare(card.typed[1], "")
        }

        function test_skipEmitsSkipped() {
            card.skipped()
            compare(skippedSpy.count, 1)
        }

        function test_escapeSkips() {
            card.forceActiveFocus()
            keyClick(Qt.Key_Escape)
            compare(skippedSpy.count, 1)
        }

        function test_numberKeysAnswerALoneQuestion() {
            card.forceActiveFocus()
            keyClick(Qt.Key_2)
            compare(answeredSpy.count, 1)
            compare(answeredSpy.signalArguments[0][0][0].selected[0], 1)
        }

        function test_numberKeysBeyondTheOptionsDoNothing() {
            card.forceActiveFocus()
            keyClick(Qt.Key_4)                      // only three options
            compare(answeredSpy.count, 0)
        }

        function test_enterSendsOnlyWhenComplete() {
            card.request = { id: "multi4", secondsLeft: 60, title: "x", questions: [
                root.q("Which extras?", ["TOC", "Index"], true) ] }
            card.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(answeredSpy.count, 0)
            card.choose(0, 0)
            keyClick(Qt.Key_Return)
            compare(answeredSpy.count, 1)
        }

        function test_aTallQuestionSetIsCappedAndScrolls() {
            const many = []
            for (let i = 0; i < 4; ++i)
                many.push(root.q("Question number " + i + " with a fairly long wording to make it wrap?",
                                 ["First option here", "Second option here", "Third option here", "Fourth option here"], false))
            card.request = { id: "tall", secondsLeft: 60, title: "x", questions: many }
            card.maxCardHeight = 260
            verify(card.height <= 260)
            card.maxCardHeight = 380
        }

        function test_theClockReadsAsMinutesAndSeconds() {
            compare(card.clock(298), "4:58")
            compare(card.clock(5), "0:05")
            compare(card.clock(-3), "0:00")
        }

        function test_urgentInTheLastHalfMinute() {
            card.request = { id: "u", secondsLeft: 20, title: "x", questions: [root.q("Q?", ["A", "B"], false)] }
            verify(card.urgent)
            card.request = { id: "u", secondsLeft: 120, title: "x", questions: [root.q("Q?", ["A", "B"], false)] }
            verify(!card.urgent)
        }

        function test_anEmptyRequestIsHarmless() {
            card.request = ({})
            compare(card.questions.length, 0)
            verify(!card.complete)
            card.send()
            compare(answeredSpy.count, 0)
        }
    }
}
