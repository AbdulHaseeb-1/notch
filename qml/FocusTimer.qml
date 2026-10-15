import QtQuick

// Focus timer: a countdown that survives closing the panel. It keeps an end
// timestamp instead of counting ticks, so it stays exact when the notch is
// busy, and it keeps running while the panel is shut (the notch shows a live
// pill for it).
Item {
    id: ft
    visible: false

    property int minutes: 25                    // chosen length
    property bool running: false
    property bool finished: false
    property real remainingMs: minutes * 60000
    property real endAt: 0

    readonly property real totalMs: minutes * 60000
    // Fraction of the session still to go, 1 -> 0.
    readonly property real ratio: totalMs > 0 ? Math.max(0, Math.min(1, remainingMs / totalMs)) : 0
    // Show the live pill while a session is running, paused part-way, or just ended.
    readonly property bool active: running || finished || (remainingMs > 0 && remainingMs < totalMs)
    readonly property int secondsLeft: Math.ceil(remainingMs / 1000)
    readonly property string label: {
        const s = ft.secondsLeft
        const h = Math.floor(s / 3600)
        const m = Math.floor((s % 3600) / 60)
        const sec = s % 60
        const two = (n) => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + two(m) + ":" + two(sec) : two(m) + ":" + two(sec)
    }

    signal done()

    function start() {
        if (finished)
            reset()
        if (remainingMs <= 0)
            remainingMs = totalMs
        endAt = Date.now() + remainingMs
        running = true
    }

    function pause() {
        if (!running)
            return
        remainingMs = Math.max(0, endAt - Date.now())
        running = false
    }

    function toggle() {
        if (running)
            pause()
        else
            start()
    }

    function reset() {
        running = false
        finished = false
        remainingMs = totalMs
    }

    function setMinutes(m) {
        minutes = Math.max(1, Math.min(180, Math.round(m)))
        finished = false
        remainingMs = totalMs
        if (running)
            endAt = Date.now() + remainingMs
    }

    Timer {
        interval: 200
        running: ft.running
        repeat: true
        onTriggered: {
            ft.remainingMs = Math.max(0, ft.endAt - Date.now())
            if (ft.remainingMs <= 0) {
                ft.running = false
                ft.finished = true
                ft.done()
            }
        }
    }
}
