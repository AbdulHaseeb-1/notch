import QtQuick
import QtTest
import NochQml

// A tool step that failed because desktop control was paused offers Continue,
// so the run can be picked back up after the floating HUD has gone.
Item {
    id: root
    width: 420
    height: 400

    ToolStepCard {
        id: card
        width: 400
        step: ({})
    }

    SignalSpy { id: continueSpy; target: card; signalName: "continueRequested" }

    TestCase {
        name: "ToolStepCard"
        when: windowShown

        function init() {
            card.step = ({})
            continueSpy.clear()
        }

        function chip() { return findChild(card, "continueChip") }

        function test_continueIsHiddenForOrdinarySteps() {
            card.step = ({ status: "done", title: "Ran a command" })
            verify(!card.canContinue)
            verify(!chip().visible)
        }

        function test_pausedComputerStepOffersContinue() {
            card.step = ({ status: "error", title: "Pressed ctrl+shift+a",
                           output: "Paused: you took over the mouse or keyboard.",
                           computerPaused: true })
            verify(card.canContinue)
            verify(chip().visible)
        }

        function test_clickingContinueAsksTheHostToCarryOn() {
            card.step = ({ status: "error", title: "Pressed ctrl+shift+a",
                           output: "Paused: you took over the mouse or keyboard.",
                           computerPaused: true })
            wait(50)   // let the column lay the pause row out
            const hit = findChild(card, "continueHit")
            verify(hit !== null)
            verify(hit.width > 0)
            verify(hit.height > 0)
            const p = hit.mapToItem(card, hit.width / 2, hit.height / 2)
            mouseClick(card, p.x, p.y)
            tryCompare(continueSpy, "count", 1)
            verify(!card.expanded)   // the click was the chip's, not the output well's
        }
    }
}
