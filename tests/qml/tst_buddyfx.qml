// The ring styles, the glow and the listening / speaking animations: every style
// draws something, the animations only run while active, and settings are honoured.
import QtQuick
import QtTest
import NochQml

TestCase {
    id: tc
    name: "BuddyFx"
    when: windowShown
    width: 220
    height: 220
    visible: true

    readonly property var styles: ["neon", "aurora", "comet", "orbit", "dashes", "pulse", "prism", "halo"]

    Component {
        id: ringC
        Item {
            width: 200; height: 200
            property alias ring: r
            BuddyRing { id: r; anchors.fill: parent; midR: 40; thickness: 3; s: 1 }
        }
    }
    Component {
        id: glowC
        BuddyGlow { x: 100; y: 100; orbRadius: 26; s: 1 }
    }
    Component {
        id: listenC
        BuddyListenFx { x: 100; y: 100; orbRadius: 26; s: 1 }
    }
    Component {
        id: speakC
        BuddySpeakFx { x: 100; y: 100; orbRadius: 26; s: 1 }
    }

    // How many pixels differ from the background (the picture's corner).
    function inked(item) {
        let n = 0
        const grab = grabImage(item)
        const bg = grab.pixel(0, 0)
        for (let y = 0; y < grab.height; y += 2)
            for (let x = 0; x < grab.width; x += 2) {
                const p = grab.pixel(x, y)
                if (Math.abs(p.r - bg.r) + Math.abs(p.g - bg.g) + Math.abs(p.b - bg.b) > 0.08)
                    ++n
            }
        return n
    }

    function test_everyRingStyleDraws_data() {
        return styles.map(function (s) { return { tag: s, style: s } })
    }
    function test_everyRingStyleDraws(data) {
        const box = createTemporaryObject(ringC, tc)
        verify(box)
        box.ring.style = data.style
        box.ring.running = true
        wait(250)
        verify(inked(box) > 60, data.style + " drew nothing")
    }

    function test_ringColoursFollowThePalette() {
        const box = createTemporaryObject(ringC, tc)
        box.ring.stops = ["#ff0000", "#ff0000", "#ff0000", "#ff0000"]
        wait(200)
        const grab = grabImage(box)
        let red = 0, notRed = 0
        for (let y = 0; y < grab.height; y += 2)
            for (let x = 0; x < grab.width; x += 2) {
                const p = grab.pixel(x, y)
                if (p.r > 0.7 && p.g < 0.3 && p.b < 0.3)
                    ++red
                else if (p.g > p.r + 0.25 || p.b > p.r + 0.25)
                    ++notRed            // anything green or blue would be a colour that is not in the palette
            }
        verify(red > 20, "no red ring")
        compare(notRed, 0)
    }

    function test_glowFollowsModeAndStrength() {
        const g = createTemporaryObject(glowC, tc)
        g.activity = 1
        g.strength = 1
        tryVerify(function () { return g.visible })
        g.mode = "off"
        verify(!g.visible)
        g.mode = "floor"
        tryVerify(function () { return g.visible })
        g.strength = 0
        tryVerify(function () { return !g.visible })
        // Faintly lit when idle only if asked to be.
        g.strength = 0.5
        g.activity = 0
        tryVerify(function () { return !g.visible })
        g.always = true
        tryVerify(function () { return g.visible })
        verify(g.reach > g.orbRadius)
    }

    function test_listeningRunsOnlyWhileActive() {
        const f = createTemporaryObject(listenC, tc)
        verify(!f.visible)
        f.active = true
        f.mic = true
        f.level = 0.6
        tryVerify(function () { return f.visible })
        tryVerify(function () { return f.ptsA.length === f.steps + 1 }, 2000)
        // The ribbon follows the level: louder is wider.
        wait(300)
        const quietSpan = Math.abs(f.ptsA[0].x)
        verify(quietSpan > f.orbRadius)
        f.active = false
        tryVerify(function () { return !f.visible }, 2000)
    }

    function test_listeningWithoutMicHasNoRibbon() {
        const f = createTemporaryObject(listenC, tc)
        f.active = true
        f.mic = false
        wait(300)
        compare(f.ptsA.length, 0)
    }

    function test_speakingMakesAnEnergyAndStops() {
        const f = createTemporaryObject(speakC, tc)
        verify(!f.visible)
        f.active = true
        tryVerify(function () { return f.env > 0.05 }, 3000)
        f.active = false
        tryVerify(function () { return !f.visible }, 2000)
    }

    function test_speakingFollowsARealEnergyWhenGiven() {
        const f = createTemporaryObject(speakC, tc)
        f.energy = 0.9
        f.active = true
        tryVerify(function () { return f.env > 0.8 }, 2000)
        f.energy = 0
        tryVerify(function () { return f.env < 0.1 }, 2000)
    }
}
