#include "BuddyProviderInternal.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace noch::buddyprovider;

// The DuckDuckGo backend for web_search (client-side): used for every
// non-Anthropic provider. The Anthropic provider uses Anthropic's server-side
// web_search tool instead and never calls this (see webSearchBackendFor in
// BuddyProviderInternal.h). Scrapes html.duckduckgo.com and falls back to the
// Wikipedia Search API when the scrape yields nothing.
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

    constexpr int kMaxFetchBytes = 1 * 1024 * 1024;
    QNetworkReply *reply = m_nam.get(req);
    m_webSearchReply = reply;
    const quint64 fetchGen = m_runGeneration;
    // Drain as the body arrives and stop at the cap: QNetworkReply otherwise
    // buffers the whole download (a multi-GB file) before finished() fires.
    auto body = std::make_shared<QByteArray>();
    auto capped = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::readyRead, this, [reply, body, capped]() {
        if (*capped)
            return;
        body->append(reply->readAll());
        if (body->size() > kMaxFetchBytes) {
            *capped = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, body, capped, call, directUrl, extractMode, fetchGen]() {
        if (m_webSearchReply == reply)
            m_webSearchReply = nullptr;
        if (fetchGen != m_runGeneration) {
            reply->deleteLater();
            return;
        }
        // Hitting the cap aborts the transfer on purpose: that is a complete,
        // truncated page, not a failed fetch.
        const bool ok = *capped || (reply->error() == QNetworkReply::NoError);
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
        QByteArray raw = *body;
        if (!*capped)
            raw.append(reply->readAll());
        reply->deleteLater();
        if (!ok && raw.isEmpty()) {
            finishCurrentToolCall(call, QStringLiteral("Failed to fetch '%1': %2").arg(directUrl, errStr), QString(), true);
            return;
        }
        // A failed transfer can still carry a partial body: hand it over, but
        // label it as incomplete so the model doesn't treat it as the page.
        const QString partialNote = ok ? QString()
            : QStringLiteral("\n\n[The fetch failed partway (%1): content below is incomplete.]").arg(errStr);
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


