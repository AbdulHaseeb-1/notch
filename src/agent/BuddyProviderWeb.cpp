#include "BuddyProviderInternal.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace noch::buddyprovider;

void BuddyProvider::executeWebSearchTool(const PendingToolCall &call)
{
    const QString query = call.input.value(QStringLiteral("query")).toString().trimmed();
    const QString directUrl = call.input.value(QStringLiteral("url")).toString().trimmed();
    const int maxResults = qBound(1, call.input.value(QStringLiteral("max_results")).toInt(5), 8);

    if (query.isEmpty() && directUrl.isEmpty()) {
        finishCurrentToolCall(call, QStringLiteral("Error: provide a 'query' or 'url' for web_search."), QString(), true);
        return;
    }

    // If only a URL was passed to web_search, delegate to executeWebFetchTool
    if (query.isEmpty() && !directUrl.isEmpty()) {
        executeWebFetchTool(call);
        return;
    }

    const auto makeBrowserRequest = [](const QUrl &u) -> QNetworkRequest {
        QNetworkRequest req(u);
        req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36");
        req.setRawHeader("Accept", "text/html,application/xhtml+xml,application/json;q=0.9,*/*;q=0.8");
        req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(15000);
        return req;
    };

    // Live web search via DuckDuckGo HTML endpoint + Wikipedia JSON fallback
    QUrl ddgUrl(QStringLiteral("https://html.duckduckgo.com/html/"));
    QUrlQuery ddgQuery;
    ddgQuery.addQueryItem(QStringLiteral("q"), query);
    ddgUrl.setQuery(ddgQuery);

    QNetworkReply *reply = m_nam.get(makeBrowserRequest(ddgUrl));
    m_webSearchReply = reply;
    const quint64 webGen = m_runGeneration;
    connect(reply, &QNetworkReply::finished, this, [this, reply, call, query, maxResults, makeBrowserRequest, webGen]() {
        if (m_webSearchReply == reply)
            m_webSearchReply = nullptr;
        const QByteArray raw = reply->readAll();
        reply->deleteLater();
        if (webGen != m_runGeneration)
            return;

        QList<QPair<QString, QString>> foundCites;
        const QString parsed = parseDuckDuckGoHtml(QString::fromUtf8(raw), maxResults, &foundCites);
        if (!parsed.isEmpty()) {
            for (const auto &cite : foundCites)
                addCitation(cite.first, cite.second);
            emit responseMetaChanged();
            finishCurrentToolCall(call,
                QStringLiteral("Web search results for \"%1\":\n\n%2").arg(query, parsed),
                QString(), false);
            return;
        }

        // Fallback to Wikipedia Search API if DuckDuckGo HTML was blocked or empty
        QUrl wikiUrl(QStringLiteral("https://en.wikipedia.org/w/api.php"));
        QUrlQuery wq;
        wq.addQueryItem(QStringLiteral("action"), QStringLiteral("query"));
        wq.addQueryItem(QStringLiteral("list"), QStringLiteral("search"));
        wq.addQueryItem(QStringLiteral("srsearch"), query);
        wq.addQueryItem(QStringLiteral("utf8"), QStringLiteral("1"));
        wq.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
        wq.addQueryItem(QStringLiteral("srlimit"), QString::number(maxResults));
        wikiUrl.setQuery(wq);

        QNetworkReply *wikiReply = m_nam.get(makeBrowserRequest(wikiUrl));
        m_webSearchReply = wikiReply;
        connect(wikiReply, &QNetworkReply::finished, this, [this, wikiReply, call, query, webGen]() {
            if (m_webSearchReply == wikiReply)
                m_webSearchReply = nullptr;
            if (webGen != m_runGeneration) {
                wikiReply->deleteLater();
                return;
            }
            const bool ok = (wikiReply->error() == QNetworkReply::NoError);
            const QString errStr = wikiReply->errorString();
            const QByteArray wRaw = wikiReply->readAll();
            wikiReply->deleteLater();

            if (!ok) {
                finishCurrentToolCall(call, QStringLiteral("Web search error: %1").arg(errStr), QString(), true);
                return;
            }
            const QJsonArray items = QJsonDocument::fromJson(wRaw).object()
                                         .value(QStringLiteral("query")).toObject()
                                         .value(QStringLiteral("search")).toArray();
            if (items.isEmpty()) {
                finishCurrentToolCall(call, QStringLiteral("No web search results found for \"%1\".").arg(query), QString(), false);
                return;
            }
            QStringList outRows;
            for (int i = 0; i < items.size(); ++i) {
                const QJsonObject it = items.at(i).toObject();
                const QString title = it.value(QStringLiteral("title")).toString();
                const QString snip = stripHtmlTags(it.value(QStringLiteral("snippet")).toString()).simplified();
                const QString pageUrl = QStringLiteral("https://en.wikipedia.org/wiki/")
                                      + QString::fromUtf8(QUrl::toPercentEncoding(title.simplified().replace(' ', '_')));
                addCitation(title, pageUrl);
                outRows << QStringLiteral("%1. %2\n   URL: %3\n   %4").arg(i + 1).arg(title, pageUrl, snip);
            }
            emit responseMetaChanged();
            finishCurrentToolCall(call,
                QStringLiteral("Web search results for \"%1\":\n\n%2").arg(query, outRows.join(QStringLiteral("\n\n"))),
                QString(), false);
        });
    });
}

void BuddyProvider::executeWebFetchTool(const PendingToolCall &call)
{
    const QString directUrl = call.input.value(QStringLiteral("url")).toString().trimmed();
    const QString extractMode = call.input.value(QStringLiteral("extract_mode")).toString(QStringLiteral("text")).trimmed().toLower();
    const QUrl targetUrl = QUrl::fromUserInput(directUrl);
    if (directUrl.isEmpty() || !targetUrl.isValid()) {
        finishCurrentToolCall(call, QStringLiteral("Error: valid 'url' is required for web_fetch."), QString(), true);
        return;
    }
    if (!AgentPolicy::isFetchableUrl(directUrl)) {
        finishCurrentToolCall(call, QStringLiteral("Error: only http(s) URLs can be fetched."), QString(), true);
        return;
    }

    QNetworkRequest req(targetUrl);
    req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36");
    req.setRawHeader("Accept", "text/html,application/xhtml+xml,application/json;q=0.9,*/*;q=0.8");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(15000);

    QNetworkReply *reply = m_nam.get(req);
    m_webSearchReply = reply;
    const quint64 fetchGen = m_runGeneration;
    connect(reply, &QNetworkReply::finished, this, [this, reply, call, directUrl, extractMode, fetchGen]() {
        if (m_webSearchReply == reply)
            m_webSearchReply = nullptr;
        if (fetchGen != m_runGeneration) {
            reply->deleteLater();
            return;
        }
        const bool ok = (reply->error() == QNetworkReply::NoError);
        const QString errStr = reply->errorString();
        // SSRF guard: redirects are followed automatically, but the gate only
        // vetted the original URL. Re-validate the final URL before the body
        // can reach the model.
        const QString finalUrl = reply->url().toString();
        if (finalUrl != directUrl
                && (!AgentPolicy::isFetchableUrl(finalUrl) || AgentPolicy::isPrivateUrl(finalUrl))) {
            reply->deleteLater();
            finishCurrentToolCall(call,
                QStringLiteral("Failed to fetch '%1': it redirected to a URL that is not allowed.").arg(directUrl),
                QString(), true);
            return;
        }
        QByteArray raw = reply->readAll();
        reply->deleteLater();
        if (!ok && raw.isEmpty()) {
            finishCurrentToolCall(call, QStringLiteral("Failed to fetch '%1': %2").arg(directUrl, errStr), QString(), true);
            return;
        }
        // A failed transfer can still carry a partial body: hand it over, but
        // label it as incomplete so the model doesn't treat it as the page.
        const QString partialNote = ok ? QString()
            : QStringLiteral("\n\n[The fetch failed partway (%1): content below is incomplete.]").arg(errStr);
        constexpr int kMaxFetchBytes = 1 * 1024 * 1024;
        const bool truncated = raw.size() > kMaxFetchBytes;
        if (truncated)
            raw = raw.left(kMaxFetchBytes);
        const QString html = QString::fromUtf8(raw)
            + (truncated ? QStringLiteral("\n\n[truncated at 1 MiB]") : QString());
        static const QRegularExpression titleRe(
            QStringLiteral("<title[^>]*>(.*?)</title>"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        const QString title = stripHtmlTags(titleRe.match(html).captured(1)).simplified();
        addCitation(title, directUrl);
        emit responseMetaChanged();

        if (extractMode == QLatin1String("links")) {
            static const QRegularExpression hrefRe(
                QStringLiteral("<a[^>]+href=\"(https?://[^\"]+)\"[^>]*>(.*?)</a>"),
                QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
            QRegularExpressionMatchIterator it = hrefRe.globalMatch(html);
            QStringList links;
            QSet<QString> seen;
            while (it.hasNext() && links.size() < 40) {
                const QRegularExpressionMatch m = it.next();
                const QString u = decodeHtmlEntities(m.captured(1).trimmed());
                const QString label = stripHtmlTags(m.captured(2)).simplified();
                if (seen.contains(u))
                    continue;
                seen.insert(u);
                links << (label.isEmpty() ? u : QStringLiteral("- %1 → %2").arg(label.left(60), u));
            }
            finishCurrentToolCall(call,
                QStringLiteral("Title: %1\nURL: %2\nExtracted %3 link(s):\n%4%5")
                    .arg(title, directUrl).arg(links.size()).arg(links.join('\n'), partialNote),
                QString(), false);
            return;
        }

        if (extractMode == QLatin1String("metadata")) {
            static const QRegularExpression metaDescRe(
                QStringLiteral("<meta[^>]+name=[\"']description[\"'][^>]+content=[\"']([^\"']*)[\"']"),
                QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression headingRe(
                QStringLiteral("<(h[1-3])[^>]*>(.*?)</\\1>"),
                QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
            const QString desc = decodeHtmlEntities(metaDescRe.match(html).captured(1)).simplified();
            QRegularExpressionMatchIterator hit = headingRe.globalMatch(html);
            QStringList headings;
            while (hit.hasNext() && headings.size() < 20) {
                const QRegularExpressionMatch hm = hit.next();
                const QString hText = stripHtmlTags(hm.captured(2)).simplified();
                if (!hText.isEmpty())
                    headings << QStringLiteral("[%1] %2").arg(hm.captured(1).toUpper(), hText);
            }
            finishCurrentToolCall(call,
                QStringLiteral("Title: %1\nURL: %2\nDescription: %3\nHeadings:\n%4%5")
                    .arg(title, directUrl, desc.isEmpty() ? QStringLiteral("(none)") : desc, headings.join('\n'), partialNote),
                QString(), false);
            return;
        }

        const QString body = stripHtmlTags(html);
        const QString header = title.isEmpty()
            ? QStringLiteral("URL: %1\n\n").arg(directUrl)
            : QStringLiteral("Title: %1\nURL: %2\n\n").arg(title, directUrl);
        finishCurrentToolCall(call, header + body + partialNote, QString(), false);
    });
}

void BuddyProvider::executeToolSearchTool(const PendingToolCall &call)
{
    const QString query = call.input.value(QStringLiteral("query")).toString(
        call.input.value(QStringLiteral("pattern")).toString()).trimmed();
    const QString mode = call.input.value(QStringLiteral("mode")).toString(QStringLiteral("bm25")).trimmed().toLower();

    struct CatalogEntry {
        QString name;
        QString typeTag;
        QString category;
        QString description;
        QString signature;
        bool armed;
    };

    const QList<CatalogEntry> catalog = {
        { QStringLiteral("bash"), QStringLiteral("bash_20250124"), QStringLiteral("Execution & Shell"),
          QStringLiteral("Execute PowerShell and terminal commands asynchronously on Windows with working directory and timeout protection."),
          QStringLiteral("{ command: string, working_dir?: string }"), m_allowBash },
        { QStringLiteral("computer"), QStringLiteral("computer_20250124"), QStringLiteral("Computer Use"),
          QStringLiteral("Anthropic Computer Use API: capture 1280x800 screenshots, move/click/drag mouse cursor, scroll wheel, type text, and press keyboard shortcuts."),
          QStringLiteral("{ action: 'screenshot'|'left_click'|'double_click'|'right_click'|'mouse_move'|'left_click_drag'|'scroll'|'type'|'key'|'cursor_position'|'wait', coordinate?: [x,y], text?: string }"), m_allowComputer },
        { QStringLiteral("str_replace_editor"), QStringLiteral("text_editor_20250728"), QStringLiteral("File Operations"),
          QStringLiteral("Anthropic Text Editor tool: view files and directories with line numbers, create files, replace exact strings, insert lines, and undo_edit."),
          QStringLiteral("{ command: 'view'|'create'|'str_replace'|'insert'|'undo_edit', path: string, file_text?: string, old_str?: string, new_str?: string, view_range?: [start,end] }"), m_allowEditor },
        { QStringLiteral("glob_grep"), QStringLiteral("code_search_2025"), QStringLiteral("Search & Discovery"),
          QStringLiteral("Fast recursive Glob filename pattern matching and Grep regular expression content search across files with line numbers."),
          QStringLiteral("{ mode: 'glob'|'grep', pattern: string, path?: string, file_glob?: string, max_results?: int }"), m_allowCodeSearch },
        { QStringLiteral("memory"), QStringLiteral("memory_20250818"), QStringLiteral("Memory & Context"),
          QStringLiteral("Anthropic Memory Tool: persistent sandboxed cross-session file system at /memories supporting view, create, str_replace, insert, delete, and rename."),
          QStringLiteral("{ command: 'view'|'create'|'str_replace'|'insert'|'delete'|'rename', path?: string, file_text?: string, old_str?: string, new_str?: string }"), m_allowMemory },
        { QStringLiteral("memory_search"), QStringLiteral("memory_search_20261020"), QStringLiteral("Memory & Context"),
          QStringLiteral("P8 retrieval over /memories: BM25 search across saved facts and procedures; returns gists with paths."),
          QStringLiteral("{ query: string, top_k?: int }"), m_allowMemory },
        { QStringLiteral("web_search"), QStringLiteral("web_search_20250305"), QStringLiteral("Web & Research"),
          QStringLiteral("Anthropic Web Search tool: search the live web via DuckDuckGo & Wikipedia with automatic source citation extraction."),
          QStringLiteral("{ query: string, max_results?: int }"), m_allowWebSearch },
        { QStringLiteral("web_fetch"), QStringLiteral("web_fetch_20250910"), QStringLiteral("Web & Research"),
          QStringLiteral("Anthropic Web Fetch tool: retrieve an HTTP/HTTPS URL and extract readable text, hyperlinks, or page metadata/headings."),
          QStringLiteral("{ url: string, extract_mode?: 'text'|'links'|'metadata' }"), m_allowWebSearch },
        { QStringLiteral("tool_search"), QStringLiteral("tool_search_tool_bm25_20251119"), QStringLiteral("Advanced Tool Use"),
          QStringLiteral("Anthropic Tool Search (BM25 & Regex): dynamically search and inspect the Agent SDK tool catalog and schemas on demand."),
          QStringLiteral("{ query: string, mode?: 'bm25'|'regex' }"), m_allowToolSearch },
        { QStringLiteral("advisor"), QStringLiteral("advisor_20260301"), QStringLiteral("Intelligence Routing & Verification"),
          QStringLiteral("Anthropic Advisor Tool: consult a high-reasoning strategic advisor for architecture planning, root-cause debugging, security review, and verification."),
          QStringLiteral("{ task: string, question: string, focus?: 'architecture'|'debugging'|'security'|'verification'|'planning', draft_plan?: string }"), m_allowAdvisor },
        { QStringLiteral("todo_write"), QStringLiteral("todo_write_2025"), QStringLiteral("Planning & Desktop Context"),
          QStringLiteral("Anthropic TodoWrite & Desktop Context tool: update live multi-step task checklists in the UI, read/write the Windows clipboard, or inspect OS/CPU/RAM telemetry."),
          QStringLiteral("{ action: 'todos'|'clipboard_read'|'clipboard_write'|'system_info', todos?: [{id, content, status, priority}], text?: string }"), m_allowTodoTask }
    };

    struct ScoredTool {
        CatalogEntry entry;
        double score = 0.0;
    };
    QList<ScoredTool> matches;

    if (query.isEmpty() || query == QLatin1String("*") || query.compare(QLatin1String("all"), Qt::CaseInsensitive) == 0) {
        for (const CatalogEntry &e : catalog)
            matches.append({ e, 1.0 });
    } else if (mode == QLatin1String("regex")) {
        QRegularExpression rx(query, QRegularExpression::CaseInsensitiveOption);
        if (!rx.isValid())
            rx = QRegularExpression(QRegularExpression::escape(query), QRegularExpression::CaseInsensitiveOption);
        for (const CatalogEntry &e : catalog) {
            const QString hay = e.name + QLatin1Char(' ') + e.typeTag + QLatin1Char(' ') + e.category + QLatin1Char(' ') + e.description;
            if (rx.match(hay).hasMatch())
                matches.append({ e, 1.0 });
        }
    } else {
        // BM25-inspired term frequency & field-weighted scoring
        const QStringList terms = query.toLower().split(QRegularExpression(QStringLiteral("[^a-z0-9_]+")), Qt::SkipEmptyParts);
        for (const CatalogEntry &e : catalog) {
            double s = 0.0;
            const QString nameLow = e.name.toLower();
            const QString catLow = e.category.toLower();
            const QString descLow = (e.description + QLatin1Char(' ') + e.signature + QLatin1Char(' ') + e.typeTag).toLower();
            for (const QString &term : terms) {
                if (nameLow == term)
                    s += 6.0;
                else if (nameLow.contains(term))
                    s += 3.5;
                if (catLow.contains(term))
                    s += 2.2;
                if (descLow.contains(term))
                    s += 1.4;
            }
            if (s > 0.0)
                matches.append({ e, s });
        }
        std::sort(matches.begin(), matches.end(), [](const ScoredTool &a, const ScoredTool &b) {
            return a.score > b.score;
        });
    }

    if (matches.isEmpty()) {
        for (const CatalogEntry &e : catalog)
            matches.append({ e, 0.5 });
    }

    QStringList lines;
    lines << QStringLiteral("Anthropic Tool Search (%1) — %2 tool(s) matched for \"%3\":")
                 .arg(mode.toUpper()).arg(matches.size()).arg(query.isEmpty() ? QStringLiteral("*") : query);
    for (const ScoredTool &m : matches) {
        m_unlockedDeferredTools.insert(m.entry.name);
        lines << QStringLiteral("\n• %1 [%2] (%3) — %4\n  Description: %5\n  Schema: %6")
                     .arg(m.entry.name,
                          m.entry.typeTag,
                          m.entry.armed ? QStringLiteral("ARMED") : QStringLiteral("DISABLED"),
                          m.entry.category,
                          m.entry.description,
                          m.entry.signature);
    }

    finishCurrentToolCall(call, lines.join('\n'), QString(), false);
}

void BuddyProvider::executeAdvisorTool(const PendingToolCall &call)
{
    const QString task = call.input.value(QStringLiteral("task")).toString().trimmed();
    const QString question = call.input.value(QStringLiteral("question")).toString(task).trimmed();
    const QString focus = call.input.value(QStringLiteral("focus")).toString(QStringLiteral("architecture")).trimmed();
    const QString draftPlan = call.input.value(QStringLiteral("draft_plan")).toString().trimmed();

    if (task.isEmpty() && question.isEmpty()) {
        finishCurrentToolCall(call, QStringLiteral("Error: 'task' and 'question' are required for advisor."), QString(), true);
        return;
    }

    m_advisorUsedInTurn = true;
    m_advisorFocusInTurn = focus;

    // Build a structured local strategic evaluation as a fast baseline/fallback
    auto buildStructuredGuidance = [task, question, focus, draftPlan]() -> QString {
        QStringList advice;
        advice << QStringLiteral("[Anthropic Advisor Consultation · Focus: %1]").arg(focus.toUpper());
        advice << QStringLiteral("Task: %1").arg(task.isEmpty() ? question : task);
        if (!question.isEmpty() && question != task)
            advice << QStringLiteral("Question: %1").arg(question);
        advice << QString();
        if (focus == QLatin1String("debugging")) {
            advice << QStringLiteral("1. Isolate Reproduction & Telemetry: Verify exact error output/exit codes via `bash` or `glob_grep` before changing files.");
            advice << QStringLiteral("2. Root-Cause Hypothesis: Check path resolution, permissions, process state, and recent edits first.");
            advice << QStringLiteral("3. Minimal Fix & Verification: Apply targeted `str_replace_editor` changes and re-run the failing command to confirm exit code 0.");
        } else if (focus == QLatin1String("security")) {
            advice << QStringLiteral("1. Boundary Validation: Ensure paths stay inside intended directories (especially `/memories` or workspace paths) and avoid destructive shell flags.");
            advice << QStringLiteral("2. Secret Hygiene: Never echo API keys or tokens into logs or public URLs.");
            advice << QStringLiteral("3. Least Privilege: Prefer read-only inspection (`view`, `glob_grep`) before mutating system state.");
        } else if (focus == QLatin1String("verification")) {
            advice << QStringLiteral("1. Completeness Check: Confirm every part of the user's prompt has been directly addressed.");
            advice << QStringLiteral("2. Empirical Proof: Run a quick verification command or file check rather than assuming success.");
            advice << QStringLiteral("3. Concise Synthesis: Present the verified outcome clearly to the user.");
        } else {
            advice << QStringLiteral("1. Tool Selection Strategy: Prefer deterministic CLI/file tools (`bash`, `glob_grep`, `str_replace_editor`, `web_search`) over GUI automation (`computer`) unless visual interaction is required.");
            advice << QStringLiteral("2. Incremental Execution: Track multi-step work using `todo_write` and persist reusable findings in `memory` (`/memories`).");
            advice << QStringLiteral("3. Verification Gate: Validate outputs after each step before moving to the next.");
        }
        if (!draftPlan.isEmpty())
            advice << QStringLiteral("\nDraft Plan Review: \"%1\" — Proceed step-by-step and verify intermediate outputs before finalizing.").arg(draftPlan.left(240));
        return advice.join('\n');
    };

    // If a live LLM provider is configured, perform a real high-reasoning Advisor consultation turn!
    if (m_enabled && m_provider != QLatin1String("none") && (!needsApiKey() || !m_apiKey.trimmed().isEmpty())) {
        const QString advSys = QStringLiteral(
            "You are the Anthropic Agent SDK Strategic Advisor (`advisor_20260301`). "
            "An executor agent is consulting you on a decision point (focus: %1). "
            "Provide concise, high-signal strategic guidance (under 140 words): "
            "1) Key risks or edge cases, 2) Recommended concrete tool sequence, and 3) How to verify success. "
            "Do not call tools yourself; reply with direct actionable advice.")
            .arg(focus);

        const QString advPrompt = QStringLiteral("Task: %1\nQuestion: %2%3")
            .arg(task, question,
                 draftPlan.isEmpty() ? QString() : QStringLiteral("\nDraft Plan: %1").arg(draftPlan));

        QJsonObject payload;
        payload[QStringLiteral("model")] = m_model;
        if (m_provider == QLatin1String("anthropic")) {
            payload[QStringLiteral("max_tokens")] = 450;
            payload[QStringLiteral("system")] = advSys;
            payload[QStringLiteral("temperature")] = 0.3;
            payload[QStringLiteral("messages")] = QJsonArray{
                QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") }, { QStringLiteral("content"), advPrompt } }
            };
        } else {
            payload[QStringLiteral("temperature")] = 0.3;
            if (m_provider == QLatin1String("ollama"))
                payload[QStringLiteral("stream")] = false;
            payload[QStringLiteral("messages")] = QJsonArray{
                QJsonObject{ { QStringLiteral("role"), QStringLiteral("system") }, { QStringLiteral("content"), advSys } },
                QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") }, { QStringLiteral("content"), advPrompt } }
            };
        }

        QNetworkRequest req = buildRequest(buildUrl());
        req.setTransferTimeout(20000);
        QNetworkReply *reply = m_nam.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        m_advisorReply = reply;
        connect(reply, &QNetworkReply::finished, this, [this, reply, call, buildStructuredGuidance]() {
            if (m_advisorReply == reply)
                m_advisorReply = nullptr;
            const bool ok = (reply->error() == QNetworkReply::NoError);
            const QByteArray raw = reply->readAll();
            reply->deleteLater();

            QString guidance;
            if (ok) {
                const QJsonDocument doc = QJsonDocument::fromJson(raw);
                if (doc.isObject())
                    guidance = extractText(doc).trimmed();
            }
            if (guidance.isEmpty())
                guidance = buildStructuredGuidance();

            m_advisorInsightInTurn = guidance.left(750);
            updateResponseMeta();
            finishCurrentToolCall(call, guidance, QString(), false);
        });
        return;
    }

    const QString fallbackGuidance = buildStructuredGuidance();
    m_advisorInsightInTurn = fallbackGuidance.left(750);
    updateResponseMeta();
    finishCurrentToolCall(call, fallbackGuidance, QString(), false);
}

void BuddyProvider::executeTodoTaskTool(const PendingToolCall &call)
{
    const QString action = call.input.value(QStringLiteral("action")).toString(QStringLiteral("todos")).trimmed().toLower();

    if (action == QLatin1String("todos")) {
        const QJsonArray arr = call.input.value(QStringLiteral("todos")).toArray();
        QVariantList updated;
        QStringList summaryLines;
        int doneCount = 0;
        for (int i = 0; i < arr.size(); ++i) {
            const QJsonObject o = arr.at(i).toObject();
            const QString id = o.value(QStringLiteral("id")).toString(QString::number(i + 1));
            const QString content = o.value(QStringLiteral("content")).toString().trimmed();
            const QString status = o.value(QStringLiteral("status")).toString(QStringLiteral("pending")).toLower();
            const QString priority = o.value(QStringLiteral("priority")).toString(QStringLiteral("medium")).toLower();
            if (content.isEmpty())
                continue;
            if (status == QLatin1String("completed"))
                ++doneCount;
            QVariantMap row;
            row[QStringLiteral("id")] = id;
            row[QStringLiteral("content")] = content;
            row[QStringLiteral("status")] = status;
            row[QStringLiteral("priority")] = priority;
            updated.append(row);

            const QString mark = status == QLatin1String("completed")
                ? QStringLiteral("[✓]")
                : (status == QLatin1String("in_progress") ? QStringLiteral("[→]") : QStringLiteral("[ ]"));
            summaryLines << QStringLiteral("%1 %2 (%3)").arg(mark, content, priority);
        }
        m_agentTodos = updated;
        emit agentTodosChanged();
        finishCurrentToolCall(call,
            QStringLiteral("Updated Agent SDK task checklist (%1/%2 completed):\n%3")
                .arg(doneCount).arg(updated.size()).arg(summaryLines.join('\n')),
            QString(), false);
        return;
    }

    if (action == QLatin1String("clipboard_read")) {
        QClipboard *cb = QGuiApplication::clipboard();
        const QString text = cb ? cb->text() : QString();
        finishCurrentToolCall(call,
            text.isEmpty() ? QStringLiteral("(clipboard is currently empty)")
                           : QStringLiteral("Clipboard content (%1 chars):\n%2").arg(text.size()).arg(text),
            QString(), false);
        return;
    }

    if (action == QLatin1String("clipboard_write")) {
        const QString text = call.input.value(QStringLiteral("text")).toString();
        QClipboard *cb = QGuiApplication::clipboard();
        if (cb)
            cb->setText(text);
        finishCurrentToolCall(call,
            QStringLiteral("Copied %1 character(s) to the Windows clipboard.").arg(text.size()),
            QString(), false);
        return;
    }

    if (action == QLatin1String("system_info")) {
        QScreen *scr = QGuiApplication::primaryScreen();
        const QRect g = scr ? scr->geometry() : QRect(0, 0, 1920, 1080);
        QStringList info;
        info << QStringLiteral("OS: %1 (%2)").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
        info << QStringLiteral("Hostname: %1").arg(QSysInfo::machineHostName());
        info << QStringLiteral("Primary Display: %1x%2 (Agent coordinate space: %3x%4)")
                    .arg(g.width()).arg(g.height()).arg(m_computer->mapper().agentSize().width()).arg(m_computer->mapper().agentSize().height());
        info << QStringLiteral("Working Directory: %1").arg(QDir::currentPath());
        info << QStringLiteral("Memories Directory: %1").arg(memoriesPath());
#ifdef Q_OS_WIN
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            const double totalGb = double(memInfo.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
            const double availGb = double(memInfo.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
            info << QStringLiteral("Physical RAM: %1 GB free / %2 GB total (%3% used)")
                        .arg(availGb, 0, 'f', 1).arg(totalGb, 0, 'f', 1).arg(memInfo.dwMemoryLoad);
        }
#endif
        finishCurrentToolCall(call, info.join('\n'), QString(), false);
        return;
    }

    finishCurrentToolCall(call, QStringLiteral("Error: unsupported todo_write action '%1'.").arg(action), QString(), true);
}


