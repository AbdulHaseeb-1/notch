import QtQuick
import QtTest
import NochQml

// The "open <app>" hand-off: launch a real Start Menu app, wait for a cold
// index, or let the agent take over when the app is not installed.
Item {
    id: root
    width: 100
    height: 100

    QtObject {
        id: fakeIndex
        property var results: []
        property bool indexing: false
        property var searches: []
        signal indexReady()

        function search(query) {
            searches = searches.concat([query])
            return results
        }
    }

    BuddyAppLaunch {
        id: launcher
        appIndex: fakeIndex
        agentEnabled: true
    }

    SignalSpy { id: launchSpy; target: launcher; signalName: "launchRequested" }
    SignalSpy { id: agentSpy; target: launcher; signalName: "agentRequested" }
    SignalSpy { id: noMatchSpy; target: launcher; signalName: "noMatch" }

    TestCase {
        name: "BuddyAppLaunch"
        when: windowShown

        function init() {
            launcher.cancel()
            launcher.agentEnabled = true
            fakeIndex.results = []
            fakeIndex.indexing = false
            fakeIndex.searches = []
            launchSpy.clear()
            agentSpy.clear()
            noMatchSpy.clear()
        }

        function test_installedAppLaunches() {
            fakeIndex.results = [
                { kind: "app", title: "Google Chrome" },
                { kind: "web", title: "Search the web" }
            ]
            launcher.resolve("chrome", "open chrome")

            compare(fakeIndex.searches.length, 1)
            compare(fakeIndex.searches[0], "chrome")
            compare(launchSpy.count, 1)
            compare(launchSpy.signalArguments[0][0].title, "Google Chrome")
            compare(launchSpy.signalArguments[0][1], "chrome")
            compare(agentSpy.count, 0)
            compare(noMatchSpy.count, 0)
        }

        function test_webResultIsNotAnApp() {
            fakeIndex.results = [{ kind: "web", title: "Search the web" }]
            launcher.resolve("chatgpt", "open chatgpt")

            compare(launchSpy.count, 0)
            compare(agentSpy.count, 1)
            compare(agentSpy.signalArguments[0][0], "open chatgpt")
            compare(agentSpy.signalArguments[0][1], "chatgpt")
        }

        function test_waitsForAColdIndexThenHandsOff() {
            fakeIndex.indexing = true
            launcher.resolve("chatgpt in the browser", "open chatgpt in the browser")

            compare(launcher.waiting, true)
            compare(agentSpy.count, 0)

            fakeIndex.indexing = false
            fakeIndex.results = [{ kind: "web" }]
            fakeIndex.indexReady()

            compare(launcher.waiting, false)
            compare(agentSpy.count, 1)
            compare(agentSpy.signalArguments[0][0], "open chatgpt in the browser")
        }

        function test_aColdIndexStillFindsAnInstalledApp() {
            fakeIndex.indexing = true
            launcher.resolve("chrome", "open chrome")
            compare(launcher.waiting, true)

            fakeIndex.indexing = false
            fakeIndex.results = [{ kind: "app", title: "Google Chrome" }]
            fakeIndex.indexReady()

            compare(launcher.waiting, false)
            compare(launchSpy.count, 1)
            compare(agentSpy.count, 0)
        }

        function test_missingAppWithoutTheAgentReportsNoMatch() {
            launcher.agentEnabled = false
            launcher.resolve("chatgpt", "open chatgpt")

            compare(agentSpy.count, 0)
            compare(noMatchSpy.count, 1)
            compare(noMatchSpy.signalArguments[0][0], "chatgpt")
        }

        function test_cancelStopsWaiting() {
            fakeIndex.indexing = true
            launcher.resolve("chrome", "open chrome")
            compare(launcher.waiting, true)

            launcher.cancel()
            compare(launcher.waiting, false)

            fakeIndex.indexing = false
            fakeIndex.indexReady()
            compare(launchSpy.count, 0)
            compare(agentSpy.count, 0)
        }
    }
}
