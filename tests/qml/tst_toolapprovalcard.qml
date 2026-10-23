import QtQuick
import QtTest
import NochQml

// The approval card is flat and outline-free; Enter allows once, Esc denies,
// and the three buttons report what they would do.
Item {
    id: root
    width: 420
    height: 300

    ToolApprovalCard {
        id: card
        width: 400
        request: ({ title: "Run npm install", badge: "BASH", detail: "npm install",
                    reason: "Writes to node_modules", secondsLeft: 30 })
    }

    SignalSpy { id: decidedSpy; target: card; signalName: "decided" }

    TestCase {
        name: "ToolApprovalCard"
        when: windowShown

        function init() { decidedSpy.clear(); card.forceActiveFocus() }

        function test_hasNoOutline() {
            compare(card.border.width, 0)
        }

        function test_enterAllowsOnce() {
            keyClick(Qt.Key_Return)
            compare(decidedSpy.count, 1)
            compare(decidedSpy.signalArguments[0][0], true)
            compare(decidedSpy.signalArguments[0][1], false)
        }

        function test_escapeDenies() {
            keyClick(Qt.Key_Escape)
            compare(decidedSpy.count, 1)
            compare(decidedSpy.signalArguments[0][0], false)
        }

        function test_urgentNearTimeout() {
            verify(!card.urgent)
            card.request = ({ title: "x", secondsLeft: 10 })
            verify(card.urgent)
        }
    }
}
