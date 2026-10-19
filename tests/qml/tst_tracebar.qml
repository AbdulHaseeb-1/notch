import QtQuick
import QtTest
import NochQml

// The run summary bar: step counts, the "skipped" count of a halted batch,
// and the Undo button for the files the run changed.
Item {
    id: root
    width: 520
    height: 120

    // Stands in for BuddyAgent: the properties and the one call the bar uses.
    QtObject {
        id: agent
        property var lastRunChanges: ({})
        property var calls: []
        property var nextResult: ({ message: "Restored 2 files.", conflicts: [] })
        function undoLastRun(force) {
            calls = calls.concat([force])
            lastRunChanges = ({})
            return nextResult
        }
    }

    ToolTraceBar {
        id: bar
        width: 480
        steps: [{ status: "ok" }, { status: "error" }, { status: "skipped" }, { status: "skipped" }, { status: "denied" }]
        icons: ["terminal"]
        durationMs: 1200
    }

    SignalSpy { id: toggledSpy; target: bar; signalName: "toggled" }

    TestCase {
        name: "TraceBar"
        when: windowShown

        function init() {
            bar.undoSource = null
            bar.undoNote = ""
            bar.undoLeftFiles = false
            agent.lastRunChanges = ({})
            agent.calls = []
            toggledSpy.clear()
            agent.nextResult = { message: "Restored 2 files.", conflicts: [] }
        }

        function chip() { return findChild(bar, "undoChip") }

        function test_countsEachKindOfStep() {
            compare(bar.failed, 1)
            compare(bar.blocked, 1)
            compare(bar.skipped, 2)
        }

        function test_noUndoWithoutASource() {
            verify(!chip().visible)
        }

        function test_noUndoWhenTheRunChangedNothing() {
            bar.undoSource = agent
            verify(!chip().visible)
        }

        function test_undoAppearsWhenFilesChanged() {
            bar.undoSource = agent
            agent.lastRunChanges = { runId: "r1", count: 3 }
            verify(chip().visible)
            compare(bar.changedCount, 3)
        }

        function test_clickUndoesTheRunOnceAndShowsWhatHappened() {
            bar.undoSource = agent
            agent.lastRunChanges = { runId: "r1", count: 2 }
            mouseClick(findChild(bar, "undoHit"))
            compare(agent.calls.length, 1)
            compare(agent.calls[0], false)        // never forced the first time
            verify(!chip().visible)               // nothing left to undo
            compare(bar.undoNote, "Restored 2 files.")
            compare(toggledSpy.count, 0)          // the click was the button's, not the bar's
        }

        function test_clickingElsewhereOnTheBarStillTogglesTheTrace() {
            bar.undoSource = agent
            agent.lastRunChanges = { runId: "r1", count: 2 }
            mouseClick(bar, 20, bar.height / 2)
            compare(toggledSpy.count, 1)
            compare(agent.calls.length, 0)
        }

        function test_filesChangedSinceAreLeftAloneUntilYouInsist() {
            bar.undoSource = agent
            agent.nextResult = { message: "1 file changed since and was left alone.", conflicts: ["a.txt"] }
            agent.lastRunChanges = { runId: "r1", count: 2 }
            mouseClick(findChild(bar, "undoHit"))
            compare(agent.calls[0], false)
            verify(bar.undoLeftFiles)
            // The journal still holds the conflicting file, so the chip stays and offers to override.
            agent.lastRunChanges = { runId: "r1", count: 1 }
            verify(chip().visible)
            agent.nextResult = { message: "Restored 1 file.", conflicts: [] }
            mouseClick(findChild(bar, "undoHit"))
            compare(agent.calls.length, 2)
            compare(agent.calls[1], true)         // only now
        }
    }
}
