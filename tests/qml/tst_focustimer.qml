import QtQuick
import QtTest
import NochQml

// The focus timer publishes the time left once per displayed second (so the
// notch window is not repainted several times a second for the whole session)
// and still ends on time.
Item {
    id: root
    width: 100
    height: 100

    FocusTimer { id: ft; minutes: 1 }

    SignalSpy { id: remainingSpy; target: ft; signalName: "remainingMsChanged" }
    SignalSpy { id: doneSpy; target: ft; signalName: "done" }

    TestCase {
        name: "FocusTimer"
        when: windowShown

        function init() {
            ft.reset()
            remainingSpy.clear()
            doneSpy.clear()
        }

        function test_publishesOncePerSecond() {
            ft.start()
            ft.endAt = Date.now() + 2600
            ft.remainingMs = 2600
            remainingSpy.clear()
            wait(1300)
            // 2.6 s -> 1.3 s left: the shown second changed once or twice,
            // where a 200 ms tick would have written it six times.
            verify(remainingSpy.count >= 1 && remainingSpy.count <= 3,
                   "writes: " + remainingSpy.count)
            compare(ft.running, true)
            ft.pause()
        }

        function test_finishesOnTime() {
            ft.start()
            ft.endAt = Date.now() + 300
            tryCompare(ft, "finished", true, 1500)
            compare(ft.running, false)
            compare(ft.remainingMs, 0)
            compare(doneSpy.count, 1)
        }

        function test_pauseKeepsExactRemaining() {
            ft.start()
            ft.endAt = Date.now() + 5000
            wait(300)
            ft.pause()
            verify(ft.remainingMs > 4000 && ft.remainingMs <= 4750, "remaining: " + ft.remainingMs)
            compare(ft.running, false)
        }
    }
}
