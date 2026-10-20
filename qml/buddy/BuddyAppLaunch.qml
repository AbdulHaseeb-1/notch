import QtQuick

// Resolves an "open <app>" request against the Start Menu index. Kept apart
// from the orb so the launch / wait / hand-off path can be tested without the
// rest of Buddy, and because a cold index must not look like "not installed".
Item {
    id: launcher

    property var appIndex: null
    property bool agentEnabled: false
    // The request waiting for the background Start Menu scan to finish.
    property var pending: null
    readonly property bool waiting: pending !== null

    signal launchRequested(var row, string query)
    signal agentRequested(string request, string query)
    signal noMatch(string query)

    function resolve(query, request) {
        if (!appIndex) {
            noMatch(query)
            return
        }
        const rows = appIndex.search(query).filter(function (r) {
            return r.kind === "app"
        })
        if (rows.length > 0) {
            pending = null
            launchRequested(rows[0], query)
            return
        }
        if (appIndex.indexing) {
            pending = { query: query, request: request }
            return
        }
        pending = null
        finish(query, request)
    }

    function cancel() {
        pending = null
    }

    function finish(query, request) {
        if (agentEnabled)
            agentRequested(request, query)
        else
            noMatch(query)
    }

    function indexReady() {
        const pendingRequest = pending
        pending = null
        if (pendingRequest)
            resolve(pendingRequest.query, pendingRequest.request)
    }

    Connections {
        target: launcher.appIndex
        enabled: launcher.waiting
        function onIndexReady() { launcher.indexReady() }
    }
}
