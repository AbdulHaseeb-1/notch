import QtQuick
import QtTest
import NochQml

// The live conversation caption under the orb: the line it shows in each state,
// which end of a long line stays in view, when it shows, and that a click ends the call.
Item {
    id: root
    width: 420
    height: 60

    BuddyLiveCaption {
        id: caption
        x: 10
        y: 10
        s: 1
        shown: true
        active: true
        liveState: "listening"
    }

    SignalSpy { id: endSpy; target: caption; signalName: "endRequested" }

    TestCase {
        name: "LiveCaption"
        when: windowShown

        function init() {
            caption.shown = true
            caption.active = true
            caption.liveState = "listening"
            caption.muted = false
            caption.buddyBusy = false
            caption.userText = ""
            caption.agentText = ""
            endSpy.clear()
        }

        function label() { return findChild(caption, "captionText") }
        function area() { return findChild(caption, "captionArea") }

        function test_listeningIsAStatus() {
            compare(caption.caption, "Listening…")
            verify(!caption.speech)
            compare(label().color, Theme.textDim)
            compare(label().elide, Text.ElideRight)
        }

        function test_micOpenOnlyWhileListening() {
            verify(caption.micOpen)
            caption.muted = true
            verify(!caption.micOpen)
            compare(caption.caption, "Muted")
        }

        function test_thinkingShowsWhatYouSaid() {
            caption.liveState = "thinking"
            caption.userText = "what time is it"
            compare(caption.caption, "what time is it")
            verify(caption.speech)
            compare(label().color, Theme.ink)
        }

        function test_thinkingWithoutWords() {
            caption.liveState = "thinking"
            compare(caption.caption, "Thinking…")
            verify(!caption.speech)
        }

        function test_buddyBusyOverridesWhatYouSaid() {
            caption.liveState = "thinking"
            caption.userText = "open my mail"
            caption.buddyBusy = true
            compare(caption.caption, "Working on it…")
            verify(!caption.speech)
        }

        function test_speakingShowsBuddysWordsAndKeepsTheNewestInView() {
            caption.liveState = "speaking"
            caption.agentText = "Sure, here it is."
            compare(caption.caption, "Sure, here it is.")
            verify(caption.speech)
            compare(label().elide, Text.ElideLeft)
            compare(label().font.weight, Theme.weightBody)
        }

        function test_speakingWithoutWords() {
            caption.liveState = "speaking"
            compare(caption.caption, "Speaking…")
            verify(!caption.speech)
        }

        function test_capsTheWidthAtTheDesktopSize() {
            caption.liveState = "speaking"
            caption.agentText = "A long answer that runs on and on, well past the width the caption can take up on the desktop, so it has to be cut."
            tryCompare(caption, "width", 300, 1000)
        }

        function test_showsAndHidesWithTheConversation() {
            tryCompare(caption, "visible", true, 1000)
            caption.shown = false
            tryCompare(caption, "visible", false, 1000)
            caption.shown = true
            tryCompare(caption, "visible", true, 1000)
        }

        function test_clickEndsTheConversation() {
            tryCompare(caption, "visible", true, 1000)
            compare(area().Accessible.name, "End the conversation")
            mouseClick(caption, caption.width / 2, caption.height / 2)
            compare(endSpy.count, 1)
        }
    }
}
