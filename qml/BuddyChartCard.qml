pragma ComponentBehavior: Bound
import QtQuick

// Candlestick view of the buddy's sample chart. The fixture comes from
// BuddyRouter (deterministic per request); everything on screen is generated
// locally — the labels say so in plain words instead of posing as a market feed.
Item {
    id: card

    property var host: null   // BuddyBubble or BuddyPanel (provides payload)

    readonly property var cd: host ? host.payload : null

    // The chart payload is the only one with candles; the help panel shares
    // this card file with an empty payload, so every read is guarded by this.
    readonly property bool ready: !!cd && !!cd.candles && Array.isArray(cd.candles)


    function biasColor(bias) {
        if (bias === "uptrend") return Theme.success
        if (bias === "downtrend") return Theme.danger
        return Theme.textDim
    }

    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    function profit(v) {
        const n = Number(v)
        if (n >= 1000)
            return n.toLocaleString(Qt.locale("en-US"), 'f', 0)
        if (n >= 100)
            return n.toFixed(0)
        return n.toFixed(2)
    }

    function chartSummaryText() {
        if (!cd || !cd.bias)
            return ""
        const bias = cd.bias === "uptrend" ? "trending up"
                   : (cd.bias === "downtrend" ? "trending down" : "moving sideways")
        const strong = cd.strength >= 0.6 ? "with conviction"
                     : (cd.strength >= 0.4 ? "moderately" : "weakly")
        const r = cd.resistance && cd.resistance.length
                  ? profit(cd.resistance[0]) : "no nearby resistance"
        const s = cd.support && cd.support.length
                  ? profit(cd.support[0]) : "no nearby support"
        const dir = cd.changePct >= 0 ? "up" : "down"
        return ("The sample series closes %1 %2 over the window. Bias says %3, %4. "
                + "Would-be watch: %5 above for sellers, %6 below for buyers. "
                + "Both levels and trend are read from the local fixture — no feed is wired up.")
            .arg(cd.symbol || "").arg(dir).arg(bias).arg(strong).arg(r).arg(s)
    }

    Connections {
        target: card.host
        function onPayloadChanged() { canvas1.requestPaint() }
    }
    Component.onCompleted: canvas1.requestPaint()

    Column {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        // symbol + sample last + change
        Item {
            width: parent.width
            height: 20

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: (card.ready && card.cd && card.cd.symbol) ? String(card.cd.symbol) : ""
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: (card.ready && card.cd && card.cd.changePct !== undefined)
                      ? (card.profit(card.cd.last)
                         + (card.cd.changePct >= 0 ? "  +" : "  ")
                         + Number(card.cd.changePct).toFixed(2) + "%")
                      : ""
                color: (card.ready && card.cd && card.cd.changePct >= 0) ? Theme.success : Theme.danger
                font.family: Theme.font
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }

        Canvas {
            id: canvas1
            width: parent.width
            height: 182
            antialiasing: true

            onPaint: {
                if (!card.ready || !card.host)
                    return
                const candles = card.cd.candles
                const count = candles.length
                if (count === 0)
                    return
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)

                const res = card.cd.resistance, sup = card.cd.support
                let lo = 1e18, hi = -1e18
                for (let i = 0; i < count; ++i) {
                    if (candles[i].l < lo) lo = candles[i].l
                    if (candles[i].h > hi) hi = candles[i].h
                }
                for (let i = 0; i < res.length; ++i) {
                    if (res[i] > hi) hi = res[i]
                    if (res[i] < lo) lo = res[i]
                }
                for (let i = 0; i < sup.length; ++i) {
                    if (sup[i] > hi) hi = sup[i]
                    if (sup[i] < lo) lo = sup[i]
                }

                const span = Math.max(1e-9, hi - lo)
                const plotH = height - 24
                const plotW = width - 66 // room for the level labels on the right
                const mapY = function (price) {
                    return 6 + (1 - (price - lo) / span) * plotH
                }
                const cw = plotW / count
                const bodyW = Math.max(2, Math.min(8, cw * 0.62))
                const upCol = Theme.success, downCol = Theme.danger

                // Candlesticks.
                for (let i = 0; i < count; ++i) {
                    const c = candles[i]
                    const x = 8 + i * cw + cw / 2
                    const bull = c.c >= c.o
                    ctx.strokeStyle = ctx.fillStyle = bull ? upCol : downCol
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.moveTo(x, mapY(c.h))
                    ctx.lineTo(x, mapY(c.l))
                    ctx.stroke()
                    const yO = mapY(Math.max(c.o, c.c))
                    const yC = mapY(Math.min(c.o, c.c))
                    ctx.fillRect(x - bodyW / 2, yO, bodyW, Math.max(1.5, yC - yO))
                }

                // Volume ribbon along the bottom.
                let vmax = 0.0001
                for (let i = 0; i < count; ++i)
                    if (candles[i].v > vmax) vmax = candles[i].v
                for (let i = 0; i < count; ++i) {
                    const c = candles[i]
                    const x = 8 + i * cw
                    const vh = 16 * (c.v / vmax)
                    ctx.fillStyle = c.c >= c.o ? upCol : downCol
                    ctx.globalAlpha = 0.24
                    ctx.fillRect(x, height - 4 - vh, Math.max(2, cw - 1), vh)
                    ctx.globalAlpha = 1
                }

                // Support / resistance with tags.
                drawLevel(ctx, mapY(res[0]), plotW, res[0], "R1", downCol)
                if (res.length > 1)
                    drawLevel(ctx, mapY(res[1]), plotW, res[1], "R2", downCol)
                drawLevel(ctx, mapY(sup[0]), plotW, sup[0], "S1", upCol)
                if (sup.length > 1)
                    drawLevel(ctx, mapY(sup[1]), plotW, sup[1], "S2", upCol)
            }

            function drawLevel(ctx, y, w, price, tag, color) {
                ctx.strokeStyle = card.alpha(color, 0.6)
                ctx.setLineDash([5, 4])
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(4, y)
                ctx.lineTo(w, y)
                ctx.stroke()
                ctx.setLineDash([])
                ctx.fillStyle = card.alpha(color, 0.95)
                // Quoted family: a bare "Segoe UI" parses as "SegoeUI".
                ctx.font = "600 10px 'Segoe UI'"
                const label = tag + " " + card.profit(price)
                ctx.fillText(label, w + 6, Math.min(canvas1.height - 6, Math.max(10, y + 4)))
            }
        }

        // bias chip + strength bar
        Item {
            width: parent.width
            height: 22

            Rectangle {
                id: biasChip
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: biasLabel.implicitWidth + 18
                height: 20
                radius: 10
                color: card.alpha(card.biasColor(card.cd ? card.cd.bias : ""), 0.14)
                border.width: 1
                border.color: card.alpha(card.biasColor(card.cd ? card.cd.bias : ""), 0.45)

                Text {
                    id: biasLabel
                    anchors.centerIn: parent
                    text: card.cd
                          ? (card.cd.bias === "uptrend" ? qsTr("Trend: up")
                             : (card.cd.bias === "downtrend" ? qsTr("Trend: down") : qsTr("Range")))
                          : ""
                    color: card.biasColor(card.cd ? card.cd.bias : "")
                    font.family: Theme.font
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
            }

            Item {
                anchors.left: biasChip.right
                anchors.leftMargin: 10
                anchors.right: strengthLabel.left
                anchors.rightMargin: 10
                height: 22

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 5
                    radius: 2.5
                    color: Theme.track
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(10, parent.width * (card.cd ? card.cd.strength : 0))
                    height: 5
                    radius: 2.5
                    color: card.biasColor(card.cd ? card.cd.bias : "")
                }
            }

            Text {
                id: strengthLabel
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Strength")
                color: Theme.textFaint
                font.family: Theme.font
                font.pixelSize: 9
            }
        }

        // Honest labels + summary from the fixture numbers.
        Text {
            width: parent.width
            text: card.chartSummaryText()
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            lineHeight: 1.2
        }
    }
}
