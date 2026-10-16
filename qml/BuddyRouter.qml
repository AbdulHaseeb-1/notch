import QtQuick

// The buddy's brain: turns a typed request into a task the app can act on.
// Noch ships no AI model or live data feeds, so this router is explicit about
// what it does:
//   - "chart" and "research" kinds return clearly-labelled demo fixtures built
//     here (deterministic sample series / sample prose, no live prices, no
//     fake article URLs).
//   - "timer", "apps", "tasks" map onto real Noch behaviour (focus timer,
//     command-palette search, a persisted plan stored via Settings).
// Keeping it as a bare QtObject lets a headless test drive the exact same
// logic the on-screen buddy uses.
QtObject {
    id: router

    // ----- intent detection ------------------------------------------------

    readonly property var chartWords: /(btc|bitcoin|eth|ethereum|sol|solana|xau|usdt|crypto|coin|chart|candle|price|levels?|support|resistance)/i
    readonly property var researchWords: /(news|sentiment|summar\w*|research|report|outlook|overview|read\W*up|headline)s?\b/i
    readonly property var taskWords: /(plan|todo|to-do|tasks?|checklist|schedule|remind me to)/i
    readonly property var timerWords: /\b(timer|focus|pomodoro|remind me in|in (\d+) min)/i
    readonly property var openWords: /^(open|launch|run|start|switch to)\b[\s:]+(.+)/i

    // Resolve a typed request to a task descriptor:
    //  { kind, title, simulated, payload } — kind: chart|research|tasks|
    //  timer|apps|help. `simulated` marks content that is a demo fixture.
    function resolve(text) {
        const trimmed = String(text || "").trim()
        if (trimmed.length === 0)
            return null
        const low = trimmed.toLowerCase()

        // Timer first: "start a focus timer" must not be read as "start <app>".
        if (timerWords.test(low))
            return timerTask(trimmed, low)

        let m = openWords.exec(low)
        if (m && m[2])
            return appsTask(trimmed, m[2].trim())

        if (chartWords.test(low))
            return chartTask(trimmed)

        if (taskWords.test(low)
                && !researchWords.test(low)) // "summarize my todo list" → research
            return tasksTask(trimmed)

        if (researchWords.test(low))
            return researchTask(trimmed)

        // Fall through to honest-unhelpfulness: a capabilities card the user
        // can act on, instead of a pretend answer.
        return { kind: "help", title: "What I can do", simulated: false, payload: {} }
    }

    function appsTask(text, query) {
        return { kind: "apps", title: "Find “%1”".arg(query), simulated: false,
                 payload: { query: query } }
    }

    function timerTask(text, low) {
        // Hours first: "timer for 1 hour" must not read the bare 1 as minutes.
        let minutes = 25
        const hours = /\b(\d{1,2})\s*hours?\b/.exec(low)
        const mins = /\b(\d{1,3})\s*(?:min|mins|minutes)\b/.exec(low)
        const bare = /\b(\d{1,3})\b/.exec(low)
        if (hours)
            minutes = parseInt(hours[1], 10) * 60
        else if (mins)
            minutes = parseInt(mins[1], 10)
        else if (/\bhours?\b/.test(low))
            minutes = 60
        else if (bare)
            minutes = parseInt(bare[1], 10)
        minutes = Math.max(1, Math.min(180, minutes))
        return { kind: "timer", title: "%1-minute focus".arg(minutes),
                 simulated: false, payload: { minutes: minutes } }
    }

    // ----- chart demo ------------------------------------------------------

    // Small deterministic PRNG: the same request always draws the same
    // sample chart, so the demo does not randomly contradict itself.
    function seedFrom(text) {
        let h = 2166136261
        const s = text.toLowerCase()
        for (let i = 0; i < s.length; ++i) {
            h ^= s.charCodeAt(i)
            h = Math.imul(h, 16777619)
        }
        return (h >>> 0)
    }

    function mulberry32(seed) {
        let a = seed >>> 0
        return function () {
            a |= 0
            a = (a + 0x6d2b79f5) | 0
            let t = Math.imul(a ^ (a >>> 15), 1 | a)
            t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t
            return ((t ^ (t >>> 14)) >>> 0) / 4294967296
        }
    }

    function detectSymbol(low) {
        if (/\bxau|gold\b/.test(low)) return "XAU/USD"
        if (/\bsol|solana\b/.test(low)) return "SOL/USDT"
        if (/\beth|ethereum\b/.test(low)) return "ETH/USDT"
        return "BTC/USDT"
    }

    readonly property var symbolBase: ({ "BTC/USDT": 67412, "ETH/USDT": 3268.5,
                                        "SOL/USDT": 148.4, "XAU/USD": 2638.7 })

    // Builds { symbol, candles, levels, bias, last, changePct, step } —
    // `candles` entries are {o,h,l,c,v}. All numbers come from the fixture.
    function chartData(symbol, seedText) {
        const rand = mulberry32(seedFrom(seedText + symbol))
        const base = symbolBase[symbol] || 100
        //base price walk
        let price = base * (0.985 + rand() * 0.03)
        const drift = (rand() - 0.45) * base * 0.0016
        const candles = []
        const n = 44
        for (let i = 0; i < n; ++i) {
            const o = price
            const step = Math.max(base * 0.0009, rand() * base * 0.006)
            const c = price + drift + (rand() - 0.48) * step * 2
            const hi = Math.max(o, c) + rand() * step
            const lo = Math.min(o, c) - rand() * step
            candles.push({ o: o, h: hi, l: lo, c: c,
                           v: 0.35 + rand() * 0.65 })
            price = c
        }

        // Swing highs / lows → round into visible levels, de-duplicated so two
        // neighbouring zig-zags do not print the same line twice.
        const priceUnit = base * 0.004 // cluster tolerance
        const highs = [], lows = []
        for (let i = 2; i < candles.length - 2; ++i) {
            if (candles[i].h >= candles[i - 1].h && candles[i].h >= candles[i + 1].h
                    && candles[i].h >= candles[i - 2].h && candles[i].h >= candles[i + 2].h)
                highs.push({ price: candles[i].h, i: i })
            if (candles[i].l <= candles[i - 1].l && candles[i].l <= candles[i + 1].l
                    && candles[i].l <= candles[i - 2].l && candles[i].l <= candles[i + 2].l)
                lows.push({ price: candles[i].l, i: i })
        }
        const pick = function (arr, dir) {
            arr.sort(function (a, b) { return dir * (b.price - a.price) })
            const out = []
            for (let i = 0; i < arr.length; ++i) {
                let near = false
                for (let j = 0; j < out.length; ++j)
                    if (Math.abs(arr[i].price - out[j].price) < priceUnit) { near = true; break }
                if (!near)
                    out.push(arr[i])
                if (out.length >= 2)
                    break
            }
            return out
        }

        const res = pick(highs.slice(), +1).slice(0, 2)
        const sup = pick(lows.slice(), -1).slice(0, 2)

        // Half-split trend: compare the mean of the last ten closes to the
        // ten closes before that.
        const closes = candles.map(function (c) { return c.c })
        let sum = 0, sumPrev = 0, count = 0, countPrev = 0
        for (let i = closes.length - 10; i < closes.length; ++i) { sum += closes[i]; ++count }
        for (let i = closes.length - 20; i < closes.length - 10; ++i) { sumPrev += closes[i]; ++countPrev }
        const meanRecent = count ? sum / count : closes[closes.length - 1]
        const meanOlder = countPrev ? sumPrev / countPrev : closes[0]
        const spread = (meanRecent - meanOlder) / Math.max(1e-9, meanOlder)
        const bias = spread > 0.004 ? "uptrend"
                    : spread < -0.004 ? "downtrend" : "range"
        const strength = Math.min(0.95, Math.max(0.15, Math.abs(spread) * 40 + 0.25))

        const last = closes[closes.length - 1]
        const first = candles[0].o
        return {
            symbol: symbol,
            candles: candles,
            resistance: res.map(function (r) { return r.price }),
            support: sup.map(function (s) { return s.price }),
            bias: bias, strength: strength,
            last: last,
            changePct: (last - first) / first * 100,
            candleStepPrice: base * 0.002
        }
    }

    function chartTask(text) {
        const symbol = detectSymbol(text.toLowerCase())
        return { kind: "chart", title: "Levels check", simulated: true,
                 payload: { symbol: symbol, source: text } }
    }

    // ----- research demo ---------------------------------------------------

    // Sample research cards. Sources are *suggested browsing*, real homepage
    // links to well-known outlets — never presented as fetched articles.
    readonly property var researchTemplates: ({
        gold: {
            summary: "Sample card about gold: In calm tape, bullion tends to trade "
                     + "on two poles — real yields and dollar strength. A demo "
                     + "research note would weigh rate-cut odds against safe-haven "
                     + "demand and phrase a stance like “constructive above trend, "
                     + "watch the yield ledger”.",
            findings: [
                "Real sample themes: yields, dollar, ETF flows and central-bank buying drive positioning.",
                "Sentiment trackers usually blend futures netting with options skew — a demo cannot fetch either.",
                "Any level you see here would come from a fixture, never a live tape."
            ],
            sentiment: { score: 0.62, label: "Constructive (sample)" }
        },
        crypto: {
            summary: "Sample card about crypto: sentiment cycles in this market are "
                     + "dominated by liquidity and derivatives posture. A demo note "
                     + "would talk about funding rates, spot demand and the last "
                     + " breakout band — all placeholders here, no live feed.",
            findings: [
                "Funding, open interest and stablecoin flows are live-market items a demo card cannot see.",
                "Stablecoins and majors are grouped; BTC usually sets the tone that alts follow.",
                "Percentages shown in the chart card for this universe are from fixtures."
            ],
            sentiment: { score: 0.55, label: "Neutral (sample)" }
        },
        generic: null // filled per-request below
    })

    function researchTask(text) {
        const low = text.toLowerCase()
        let topic = "the market"
        let key = "generic"
        if (/\bgold\b|bullion|\bxau\b/.test(low)) { topic = "gold"; key = "gold" }
        else if (/\bbtc|crypto|bitcoin|eth\b/.test(low)) { topic = "crypto"; key = "crypto" }
        else if (/\bai|model|robot|automation\b/.test(low)) topic = "AI"
        else if (/\boil\b|energy|crude\b/.test(low)) topic = "energy"
        else if (/\bmarket|economy|fed|inflation\b/.test(low)) topic = "the economy"

        let tpl = researchTemplates[key]
        if (!tpl)
            tpl = {
                // Parenthesised: .arg() binds to the last literal otherwise.
                summary: ("Demo research card about %1: in a real integration this "
                          + "paragraph would be a short, sourced summary. This one is "
                          + "written locally so the layout can be judged without a "
                          + "live feed — treat every sentence as a sample.").arg(topic),
                findings: [
                    "No live feed is wired up; this card is a layout demo for “%1”.".arg(topic),
                    "Key-finding rows here are placeholders, styled like the real thing.",
                    "Ask for charts, research, plans, timers or app openings to see the routes that do act."
                ],
                sentiment: { score: 0.5, label: "Neutral (sample)" }
            }

        return {
            kind: "research", title: "Research digest", simulated: true,
            payload: {
                topic: topic, source: text,
                summary: tpl.summary,
                findings: tpl.findings.slice(),
                sentiment: { score: tpl.sentiment.score, label: tpl.sentiment.label },
                // Real, well-known front pages — offered as suggested reading,
                // never as fetched article citations.
                sources: [
                    { name: "Reuters Markets", url: "https://www.reuters.com/markets/" },
                    { name: "Bloomberg", url: "https://www.bloomberg.com/markets" },
                    key === "gold" ? { name: "Kitco (metals)", url: "https://www.kitco.com" }
                                   : { name: "Investing.com", url: "https://www.investing.com" }
                ]
            }
        }
    }

    // ----- productivity plan ----------------------------------------------

    readonly property var planTemplates: {
        "study": ["Review yesterday's notes (20 min)", "Work one practice set", "Summarize weak spots into 3 bullets", "Plan tomorrow's first task"],
        "work":  ["Cut the task list to the top 3", "Ship the smallest useful piece", "Book the long-focus block", "Close with a same-day status note"],
        "gym":   ["10-minute warm-up", "Main lift: 3 working sets", "Assistance work, 2 sets", "Short mobility cool-down"]
    }

    function tasksTask(text) {
        const low = text.toLowerCase()
        let key = ""
        for (const k in planTemplates)
            if (low.indexOf(k) >= 0) { key = k; break }
        return { kind: "tasks", title: "Plan for today", simulated: true,
                 payload: { templateKey: key, source: text } }
    }

    // Builds the plan item list for a request. Returns [{text, done:boolean}].
    function buildPlan(templateKey) {
        if (templateKey && planTemplates[templateKey])
            return planTemplates[templateKey].map(function (t) { return { text: t, done: false } })
        return [
            { text: "Name the three things that must land today", done: false },
            { text: "Take one visible step on the hardest one", done: false },
            { text: "Clear two small interruptions in one go", done: false },
            { text: "Evening reset: zero inbox and desk", done: false }
        ]
    }

    // Removes the daily ticks when the calendar day rolled over; keeps the
    // user's item text. Returns null when nothing changed.
    function planForDay(parsed, today) {
        // parsed: { day: string, items: [{text, done}] }
        if (!parsed || !parsed.items || parsed.items.length === 0)
            return null
        if (parsed.day === today)
            return null
        const items = parsed.items.map(function (it) { return { text: it.text, done: false } })
        return { day: today, items: items }
    }
}
