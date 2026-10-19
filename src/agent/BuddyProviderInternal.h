// Shared helpers for the BuddyProvider implementation files.
// This header is private to src/agent and is not installed or exposed to QML.
#pragma once

#include "BuddyProvider.h"

#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QPixmap>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace noch::buddyprovider {

namespace {

constexpr int kDefaultMaxSteps = 40;       // tool rounds per task (was 12); a round can hold a batch of actions
constexpr int kMinMaxSteps = 4;
constexpr int kMaxMaxSteps = 100;
constexpr int kMaxToolOutputChars = 6000;
// The toolset's own wording, so a halted batch reads the way the model was trained on.
const char kBatchHalted[] = "Not executed: an earlier computer action in this turn failed.";
// Safety / recovery tuning.
constexpr int kApprovalTimeoutSec = 120;   // unanswered approvals are declined
constexpr int kMaxRetries = 3;             // transient API errors (429/5xx/529)
constexpr int kMaxIdenticalCalls = 2;      // same tool + same input per run (computer: back to back)
constexpr int kMaxIdenticalComputerInputs = 8;   // the same click / key / type this often in one run is a loop
constexpr int kKeepRecentToolResults = 4;  // newest tool results kept verbatim
constexpr int kCompactMinChars = 600;      // older results above this are cleared
// Trimming comes in batches so the prompt cache survives between them: nothing
// is cleared until one of these is reached, then every older result goes.
constexpr int kClearBatchResults = 6;      // bulky older results waiting to be cleared
constexpr int kClearBatchImages = 4;       // ... or this many old screenshots
constexpr int kMaxTraceFiles = 40;
// Long answers. The old fixed max_tokens of 2048 cut longer replies off, and
// the cut text was shown as if it were the whole answer.
constexpr int kDefaultOutputTokens = 8192;  // Anthropic max_tokens (all current models allow it)
constexpr int kMinOutputTokens = 1024;
constexpr int kMaxContinuations = 3;        // "continue" requests per run after an output-limit stop
constexpr int kHistoryMsgChars = 6000;      // per message of earlier turns sent as context
constexpr int kStreamIdleTimeoutMs = 120000;      // streamed: no bytes for this long = dead
constexpr int kBufferedReplyTimeoutMs = 600000;   // buffered: the whole answer arrives at once

// An earlier turn longer than the history cap keeps its opening and, most
// importantly, its ending (what a follow-up like "continue" refers to),
// cut on whitespace, with a visible marker instead of a mid-sentence chop.
QString shortenForHistory(const QString &text, int cap = kHistoryMsgChars)
{
    if (text.size() <= cap)
        return text;
    const QString marker = QStringLiteral("\n\n[… earlier part of this answer shortened for context …]\n\n");
    const int budget = qMax(200, cap - int(marker.size()));
    int headEnd = budget * 3 / 10;
    int tailStart = text.size() - (budget - headEnd);
    for (int i = headEnd, stop = qMax(0, headEnd - 80); i > stop; --i)
        if (text.at(i).isSpace()) { headEnd = i; break; }
    for (int i = tailStart, stop = qMin(int(text.size()) - 1, tailStart + 80); i < stop; ++i)
        if (text.at(i).isSpace()) { tailStart = i + 1; break; }
    return text.left(headEnd).trimmed() + marker + text.mid(tailStart).trimmed();
}

// The model often repeats the last few words of a cut-off reply when asked
// to continue. Drop that overlap (only when it starts on a word boundary and
// is long enough not to be a coincidence) so the stitched answer reads once.
QString dropContinuationOverlap(const QString &prev, const QString &next)
{
    int pe = prev.size();
    while (pe > 0 && prev.at(pe - 1).isSpace())
        --pe;
    int ns = 0;
    while (ns < next.size() && next.at(ns).isSpace())
        ++ns;
    const QStringView p = QStringView(prev).left(pe);
    const QStringView n = QStringView(next).mid(ns);
    constexpr int kMinOverlap = 12;
    for (int k = qMin<qsizetype>(400, qMin(p.size(), n.size())); k >= kMinOverlap; --k) {
        const qsizetype start = p.size() - k;
        if (start > 0 && !p.at(start - 1).isSpace())
            continue;
        if (p.mid(start) == n.left(k))
            return next.mid(ns + k);
    }
    return next;
}

QString decodeHtmlEntities(QString s)
{
    s.replace(QLatin1String("&amp;"), QLatin1String("&"));
    s.replace(QLatin1String("&lt;"), QLatin1String("<"));
    s.replace(QLatin1String("&gt;"), QLatin1String(">"));
    s.replace(QLatin1String("&quot;"), QLatin1String("\""));
    s.replace(QLatin1String("&#39;"), QLatin1String("'"));
    s.replace(QLatin1String("&#x27;"), QLatin1String("'"));
    s.replace(QLatin1String("&apos;"), QLatin1String("'"));
    s.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    return s;
}

QString stripHtmlTags(QString html)
{
    static const QRegularExpression scriptRe(
        QStringLiteral("<(script|style|noscript|svg|head)[^>]*>.*?</\\1>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression blockTagRe(
        QStringLiteral("</?(p|div|br|li|tr|h[1-6]|section|article|header|footer|blockquote)[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression anyTagRe(QStringLiteral("<[^>]+>"));
    static const QRegularExpression multiSpaceRe(QStringLiteral("[ \\t]+"));
    static const QRegularExpression multiNewlineRe(QStringLiteral("\\n{3,}"));

    html.remove(scriptRe);
    html.replace(blockTagRe, QStringLiteral("\n"));
    html.remove(anyTagRe);
    html = decodeHtmlEntities(html);
    html.replace('\r', '\n');
    html.replace(multiSpaceRe, QStringLiteral(" "));
    html.replace(QRegularExpression(QStringLiteral(" *\\n *")), QStringLiteral("\n"));
    html.replace(multiNewlineRe, QStringLiteral("\n\n"));
    return html.trimmed();
}

QString unwrapDuckDuckGoUrl(const QString &rawHref)
{
    QString href = decodeHtmlEntities(rawHref.trimmed());
    if (href.startsWith(QLatin1String("//")))
        href = QStringLiteral("https:") + href;
    const QUrl url(href);
    const QUrlQuery q(url);
    if (q.hasQueryItem(QStringLiteral("uddg")))
        return QUrl::fromPercentEncoding(q.queryItemValue(QStringLiteral("uddg")).toUtf8());
    return href;
}

QString parseDuckDuckGoHtml(const QString &html, int maxResults,
                            QList<QPair<QString, QString>> *outCitations = nullptr)
{
    static const QRegularExpression linkRe(
        QStringLiteral("<a[^>]+class=\"[^\"]*result__a[^\"]*\"[^>]+href=\"([^\"]+)\"[^>]*>(.*?)</a>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression snippetRe(
        QStringLiteral("<a[^>]+class=\"[^\"]*result__snippet[^\"]*\"[^>]*>(.*?)</a>|<div[^>]+class=\"[^\"]*result__snippet[^\"]*\"[^>]*>(.*?)</div>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatchIterator linkIt = linkRe.globalMatch(html);
    QRegularExpressionMatchIterator snipIt = snippetRe.globalMatch(html);

    QStringList results;
    int count = 0;
    while (linkIt.hasNext() && count < maxResults) {
        const QRegularExpressionMatch lm = linkIt.next();
        const QString url = unwrapDuckDuckGoUrl(lm.captured(1));
        const QString title = stripHtmlTags(lm.captured(2)).simplified();
        QString snippet;
        if (snipIt.hasNext()) {
            const QRegularExpressionMatch sm = snipIt.next();
            snippet = stripHtmlTags(sm.captured(1).isEmpty() ? sm.captured(2) : sm.captured(1)).simplified();
        }
        if (title.isEmpty() || url.isEmpty() || url.contains(QLatin1String("duckduckgo.com/y.js")))
            continue;
        ++count;
        if (outCitations)
            outCitations->append(qMakePair(title, url));
        results << QStringLiteral("%1. %2\n   URL: %3\n   %4")
                       .arg(count)
                       .arg(title, url, snippet.isEmpty() ? QStringLiteral("(no snippet)") : snippet);
    }
    return results.join(QStringLiteral("\n\n"));
}

// Sandboxes any Anthropic memory path (e.g., "/memories", "/memories/notes.md", "notes.md")
// strictly inside `memoriesRoot` and rejects "../" escapes.
bool resolveMemorySandboxPath(const QString &memoriesRoot, const QString &rawInput,
                              QString *outAbsPath, QString *outVirtualPath)
{
    const QString rootClean = QDir::cleanPath(QFileInfo(memoriesRoot).absoluteFilePath());
    QString rel = rawInput.trimmed();
    rel.replace('\\', '/');

    while (rel.startsWith('/'))
        rel.remove(0, 1);
    if (rel == QLatin1String("memories"))
        rel.clear();
    else if (rel.startsWith(QLatin1String("memories/")))
        rel = rel.mid(9);
    while (rel.startsWith('/'))
        rel.remove(0, 1);

    const QString candidate = rel.isEmpty()
        ? rootClean
        : QDir::cleanPath(QDir(rootClean).filePath(rel));

    if (candidate != rootClean && !candidate.startsWith(rootClean + QLatin1Char('/')))
        return false;

    if (outAbsPath)
        *outAbsPath = candidate;
    if (outVirtualPath) {
        const QString sub = candidate == rootClean
            ? QString()
            : candidate.mid(rootClean.size() + 1);
        *outVirtualPath = sub.isEmpty()
            ? QStringLiteral("/memories")
            : QStringLiteral("/memories/") + sub;
    }
    return true;
}

// Ends a cacheable prefix after message `index` by putting cache_control on its
// last block that can carry one. Done on the copy that is sent, never on the
// stored conversation, so a marker never lingers on a message it no longer
// belongs to. Returns false when the message has nothing markable.
bool markCacheBreakpoint(QJsonArray &messages, int index)
{
    if (index < 0 || index >= messages.size())
        return false;
    QJsonObject msg = messages.at(index).toObject();
    const QJsonObject marker{ { QStringLiteral("type"), QStringLiteral("ephemeral") } };
    const QJsonValue content = msg.value(QStringLiteral("content"));
    if (content.isString()) {
        if (content.toString().trimmed().isEmpty())
            return false;   // the API rejects cache_control on empty text
        msg[QStringLiteral("content")] = QJsonArray{ QJsonObject{
            { QStringLiteral("type"), QStringLiteral("text") },
            { QStringLiteral("text"), content.toString() },
            { QStringLiteral("cache_control"), marker } } };
        messages[index] = msg;
        return true;
    }
    if (!content.isArray())
        return false;
    QJsonArray blocks = content.toArray();
    for (int i = blocks.size() - 1; i >= 0; --i) {
        QJsonObject b = blocks.at(i).toObject();
        const QString type = b.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("thinking") || type == QLatin1String("redacted_thinking"))
            continue;   // cannot carry a marker
        if (type == QLatin1String("text") && b.value(QStringLiteral("text")).toString().isEmpty())
            continue;
        b[QStringLiteral("cache_control")] = marker;
        blocks[i] = b;
        msg[QStringLiteral("content")] = blocks;
        messages[index] = msg;
        return true;
    }
    return false;
}

QString cleanBase(const QString &base)
{
    QString b = base.trimmed();
    while (b.endsWith('/'))
        b.chop(1);
    return b;
}

QString joinPath(const QString &base, const QStringList &parts)
{
    QString path = cleanBase(base);
    QString tail;
    for (const QString &p : parts)
        tail += '/' + p;
    if (path.endsWith(tail))
        return path;
    for (const QString &p : parts) {
        const QString seg = '/' + p;
        if (!path.endsWith(seg))
            path += seg;
    }
    return path;
}

QString defaultBaseFor(const QString &provider)
{
    if (provider == QLatin1String("ollama"))
        return QStringLiteral("http://localhost:11434");
    if (provider == QLatin1String("anthropic"))
        return QStringLiteral("https://api.anthropic.com");
    if (provider == QLatin1String("openai"))
        return QStringLiteral("https://api.openai.com");
    return QString();
}

QStringList chatPathFor(const QString &provider)
{
    if (provider == QLatin1String("ollama"))
        return { QStringLiteral("api"), QStringLiteral("chat") };
    if (provider == QLatin1String("anthropic"))
        return { QStringLiteral("v1"), QStringLiteral("messages") };
    return { QStringLiteral("v1"), QStringLiteral("chat"), QStringLiteral("completions") };
}

QStringList modelsPathFor(const QString &provider)
{
    if (provider == QLatin1String("ollama"))
        return { QStringLiteral("api"), QStringLiteral("tags") };
    return { QStringLiteral("v1"), QStringLiteral("models") };
}

QString tidyTransportError(const QString &fallback, int status)
{
    QString msg = fallback.trimmed();
    const int cut = msg.lastIndexOf(QLatin1String(": "));
    if (cut > 0)
        msg = msg.mid(cut + 2).trimmed();
    if (status > 0) {
        return msg.isEmpty() ? QObject::tr("HTTP %1").arg(status)
                             : QObject::tr("HTTP %1 — %2").arg(status).arg(msg);
    }
    return msg.isEmpty() ? QObject::tr("No answer from the provider.") : msg;
}

QString capOutput(const QString &raw, int maxChars = kMaxToolOutputChars)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("(completed with no output)");
    if (trimmed.size() <= maxChars)
        return trimmed;
    const int half = maxChars / 2;
    return trimmed.left(half)
         + QStringLiteral("\n\n... [%1 characters truncated] ...\n\n").arg(trimmed.size() - maxChars)
         + trimmed.right(half);
}

} // namespace

} // namespace noch::buddyprovider

