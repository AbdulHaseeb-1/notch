import QtQuick
import QtTest
import NochQml

// The screenshot Buddy puts into the chat: sized from the picture's own
// proportions (so the card does not jump when the image loads), a caption
// when there is one, and it eases in.
Item {
    id: root
    width: 400
    height: 400

    BuddyShot {
        id: wide
        width: 300
        source: "qrc:/does-not-exist.png"
        pixelWidth: 1600
        pixelHeight: 900
    }
    BuddyShot {
        id: captioned
        y: 200
        width: 300
        source: "qrc:/does-not-exist.png"
        caption: "Your desktop right now"
        pixelWidth: 1000
        pixelHeight: 500
    }
    BuddyShot {
        id: unknown
        y: 320
        width: 200
        source: "qrc:/does-not-exist.png"
    }

    TestCase {
        name: "BuddyShot"
        when: windowShown

        function test_heightFollowsTheScreensProportions() {
            compare(wide.imageH, Math.round(300 * 900 / 1600))
            compare(wide.height, wide.imageH)          // no caption: just the picture
        }

        function test_aCaptionAddsRoomBelow() {
            compare(captioned.imageH, 150)
            verify(captioned.height > captioned.imageH)
        }

        function test_withoutSizeItFallsBackToWidescreen() {
            compare(unknown.imageH, Math.round(200 * 0.625))
        }

        function test_easesInThenStaysPut() {
            tryCompare(wide, "appear", 1, 2000)
            compare(wide.opacity, 1)
            compare(wide.scale, 1)
        }
    }
}
