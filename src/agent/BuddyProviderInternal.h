// Shared helpers for the BuddyProvider implementation files.
// This header is private to src/agent and is not installed or exposed to QML.
#pragma once

#include "BuddyProvider.h"
#include "ToolSchemas.h"

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
#include <QMap>
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
inline QString shortenForHistory(const QString &text, int cap = kHistoryMsgChars)
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
inline QString dropContinuationOverlap(const QString &prev, const QString &next)
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

inline QString decodeHtmlEntities(QString s)
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

inline QString stripHtmlTags(QString html)
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

inline QString unwrapDuckDuckGoUrl(const QString &rawHref)
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

inline QString parseDuckDuckGoHtml(const QString &html, int maxResults,
                            QList<QPair<QString, QString>> *outCitations = nullptr)
{
    static const QRegularExpression linkRe(
        QStringLiteral("<a[^>]+class=\"[^\"]*result__a[^\"]*\"[^>]+href=\"([^\"]+)\"[^>]*>(.*?)</a>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression snippetRe(
        QStringLiteral("<a[^>]+class=\"[^\"]*result__snippet[^\"]*\"[^>]*>(.*?)</a>|<div[^>]+class=\"[^\"]*result__snippet[^\"]*\"[^>]*>(.*?)</div>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatchIterator linkIt = linkRe.globalMatch(html);

    QStringList results;
    int count = 0;
    while (linkIt.hasNext() && count < maxResults) {
        const QRegularExpressionMatch lm = linkIt.next();
        const QString url = unwrapDuckDuckGoUrl(lm.captured(1));
        const QString title = stripHtmlTags(lm.captured(2)).simplified();
        if (title.isEmpty() || url.isEmpty() || url.contains(QLatin1String("duckduckgo.com/y.js")))
            continue;
        // The snippet belongs to the HTML between this link and the next one:
        // a separate parallel iterator desyncs whenever a link is skipped.
        QString snippet;
        const int regionStart = lm.capturedEnd(0);
        const int regionEnd = linkIt.hasNext() ? linkIt.peekNext().capturedStart(0) : html.size();
        const QRegularExpressionMatch sm = snippetRe.match(html.mid(regionStart, regionEnd - regionStart));
        if (sm.hasMatch())
            snippet = stripHtmlTags(sm.captured(1).isEmpty() ? sm.captured(2) : sm.captured(1)).simplified();
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
inline bool resolveMemorySandboxPath(const QString &memoriesRoot, const QString &rawInput,
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
inline bool markCacheBreakpoint(QJsonArray &messages, int index)
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

inline QString cleanBase(const QString &base)
{
    QString b = base.trimmed();
    while (b.endsWith('/'))
        b.chop(1);
    return b;
}

inline QString joinPath(const QString &base, const QStringList &parts)
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

inline QString defaultBaseFor(const QString &provider)
{
    if (provider == QLatin1String("ollama"))
        return QStringLiteral("http://localhost:11434");
    if (provider == QLatin1String("anthropic"))
        return QStringLiteral("https://api.anthropic.com");
    if (provider == QLatin1String("openai"))
        return QStringLiteral("https://api.openai.com");
    if (provider == QLatin1String("opencode"))
        return QStringLiteral("https://opencode.ai/zen");
    return QString();
}

inline QStringList chatPathFor(const QString &provider)
{
    if (provider == QLatin1String("ollama"))
        return { QStringLiteral("api"), QStringLiteral("chat") };
    if (provider == QLatin1String("anthropic"))
        return { QStringLiteral("v1"), QStringLiteral("messages") };
    return { QStringLiteral("v1"), QStringLiteral("chat"), QStringLiteral("completions") };
}

inline QStringList modelsPathFor(const QString &provider)
{
    if (provider == QLatin1String("ollama"))
        return { QStringLiteral("api"), QStringLiteral("tags") };
    return { QStringLiteral("v1"), QStringLiteral("models") };
}

inline QString tidyTransportError(const QString &fallback, int status)
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

inline QString capOutput(const QString &raw, int maxChars = kMaxToolOutputChars)
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

// ---- Model-agnostic tool-call repair --------------------------------------
// Weak models emit near-miss tool calls: JSON wrapped in fences, trailing
// commas, wrong argument names ("cmd" for "command"), or numbers/booleans as
// strings. normalizeToolCall fixes what it safely can and reports the rest,
// so a bad call costs a correction nudge instead of a wasted turn. Pure and
// unit-tested (tests/tst_toolrepair.cpp).

enum class ToolCallRepair { Ok, Repaired, Unrepairable };

// "tool:alias" -> canonical argument name. Only renames when the canonical
// name is absent, so a real argument is never clobbered.
inline const QMap<QString, QString> &toolArgAliases()
{
    static const QMap<QString, QString> aliases{
        { QStringLiteral("bash:cmd"), QStringLiteral("command") },
        { QStringLiteral("bash:cwd"), QStringLiteral("working_dir") },
        { QStringLiteral("bash:dir"), QStringLiteral("working_dir") },
        { QStringLiteral("bash:workingdir"), QStringLiteral("working_dir") },
        { QStringLiteral("str_replace_editor:cmd"), QStringLiteral("command") },
        { QStringLiteral("str_replace_editor:filepath"), QStringLiteral("path") },
        { QStringLiteral("str_replace_editor:file"), QStringLiteral("path") },
        { QStringLiteral("str_replace_editor:filename"), QStringLiteral("path") },
        { QStringLiteral("memory:cmd"), QStringLiteral("command") },
        { QStringLiteral("memory:filepath"), QStringLiteral("path") },
        { QStringLiteral("memory:file"), QStringLiteral("path") },
        { QStringLiteral("glob_grep:query"), QStringLiteral("pattern") },
        { QStringLiteral("glob_grep:regex"), QStringLiteral("pattern") },
        { QStringLiteral("computer:coords"), QStringLiteral("coordinate") },
        { QStringLiteral("computer:coordinates"), QStringLiteral("coordinate") },
        { QStringLiteral("computer:xy"), QStringLiteral("coordinate") },
        { QStringLiteral("web_fetch:link"), QStringLiteral("url") },
        { QStringLiteral("web_fetch:uri"), QStringLiteral("url") },
        { QStringLiteral("web_search:q"), QStringLiteral("query") },
        { QStringLiteral("todo_write:tasks"), QStringLiteral("todos") },
        { QStringLiteral("todo_write:todos_list"), QStringLiteral("todos") },
    };
    return aliases;
}

struct ToolArgSpec {
    QStringList required;  // missing -> the call cannot run
    QStringList known;     // named in the correction nudge
};

// Mirrors the schemas built in BuddyProviderPrompts.cpp. Tools without
// required arguments accept an empty input.
inline const QMap<QString, ToolArgSpec> &toolArgSpecs()
{
    static const QMap<QString, ToolArgSpec> specs{
        { QStringLiteral("bash"),
          { { QStringLiteral("command") },
            { QStringLiteral("command"), QStringLiteral("working_dir") } } },
        { QStringLiteral("computer"),
          { { QStringLiteral("action") },
            { QStringLiteral("action"), QStringLiteral("coordinate"),
              QStringLiteral("start_coordinate"), QStringLiteral("text"),
              QStringLiteral("scroll_direction"), QStringLiteral("scroll_amount"),
              QStringLiteral("duration"), QStringLiteral("region"),
              QStringLiteral("display") } } },
        { QStringLiteral("str_replace_editor"),
          { { QStringLiteral("command"), QStringLiteral("path") },
            { QStringLiteral("command"), QStringLiteral("path"),
              QStringLiteral("file_text"), QStringLiteral("old_str"),
              QStringLiteral("new_str"), QStringLiteral("insert_line"),
              QStringLiteral("view_range") } } },
        { QStringLiteral("glob_grep"),
          { { QStringLiteral("mode"), QStringLiteral("pattern") },
            { QStringLiteral("mode"), QStringLiteral("pattern"),
              QStringLiteral("path"), QStringLiteral("file_glob"),
              QStringLiteral("max_results") } } },
        { QStringLiteral("memory"),
          { { QStringLiteral("command") },
            { QStringLiteral("command"), QStringLiteral("path"),
              QStringLiteral("file_text"), QStringLiteral("old_str"),
              QStringLiteral("new_str"), QStringLiteral("insert_line"),
              QStringLiteral("insert_text"), QStringLiteral("old_path"),
              QStringLiteral("new_path"), QStringLiteral("view_range") } } },
        { QStringLiteral("web_search"),
          { {},
            { QStringLiteral("query"), QStringLiteral("url"),
              QStringLiteral("max_results") } } },
        { QStringLiteral("web_fetch"),
          { { QStringLiteral("url") },
            { QStringLiteral("url"), QStringLiteral("extract_mode") } } },
        { QStringLiteral("todo_write"),
          { { QStringLiteral("action") },
            { QStringLiteral("action"), QStringLiteral("todos"),
              QStringLiteral("text") } } },
        { QStringLiteral("ask_user_question"),
          { { QStringLiteral("questions") },
            { QStringLiteral("questions") } } },
        { QStringLiteral("skill"),
          { { QStringLiteral("name") },
            { QStringLiteral("name"), QStringLiteral("file") } } },
        { QStringLiteral("share_screenshot"),
          { {},
            { QStringLiteral("caption"), QStringLiteral("display") } } },
        { QStringLiteral("music"),
          { { QStringLiteral("action") },
            { QStringLiteral("action"), QStringLiteral("query"), QStringLiteral("path"),
              QStringLiteral("volume") } } },
    };
    return specs;
}

// "tool:argument" -> "int" | "double" | "bool": stringified scalars are
// converted when they parse cleanly, otherwise left alone.
QJsonValue coerceScalar(const QJsonValue &v, const QString &kind, bool *ok)
{
    *ok = false;
    if (!v.isString())
        return v;
    const QString s = v.toString().trimmed();
    if (kind == QLatin1String("int")) {
        static const QRegularExpression intRe(QStringLiteral("^-?\\d+$"));
        if (intRe.match(s).hasMatch()) {
            *ok = true;
            return QJsonValue(s.toInt());
        }
    } else if (kind == QLatin1String("double")) {
        static const QRegularExpression dblRe(QStringLiteral("^-?\\d+(\\.\\d+)?$"));
        if (dblRe.match(s).hasMatch()) {
            *ok = true;
            return QJsonValue(s.toDouble());
        }
    } else if (kind == QLatin1String("bool")) {
        if (s.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) {
            *ok = true;
            return QJsonValue(true);
        }
        if (s.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) {
            *ok = true;
            return QJsonValue(false);
        }
    }
    return v;
}

// P1: Field-level validation against the registry. Returns a list of
// specific problems ("missing required 'command'", "'insert_line' should be
// an integer, got 'abc'"), empty when the call is valid.
inline QStringList validateToolCall(const QString &toolName, const QJsonObject &input,
                                    const ToolSchema *schemaOverride = nullptr)
{
    QStringList problems;
    const ToolSchema *schema = schemaOverride ? schemaOverride : toolSchema(toolName);
    if (!schema)
        return problems;  // unknown tools keep the gate's deny path

    for (const ToolArgDef &arg : schema->args) {
        const QJsonValue v = input.value(arg.name);
        const bool present = !v.isUndefined() && !v.isNull()
            && !(v.isString() && v.toString().trimmed().isEmpty());
        if (arg.required && !present) {
            problems << QStringLiteral("missing required argument '%1'").arg(arg.name);
            continue;
        }
        if (!present)
            continue;
        // Type check.
        bool typeOk = true;
        if (arg.type == QLatin1String("string"))
            typeOk = v.isString();
        else if (arg.type == QLatin1String("integer"))
            typeOk = v.isDouble() && qRound(v.toDouble()) == v.toDouble();
        else if (arg.type == QLatin1String("number"))
            typeOk = v.isDouble();
        else if (arg.type == QLatin1String("boolean"))
            typeOk = v.isBool();
        else if (arg.type == QLatin1String("array"))
            typeOk = v.isArray();
        else if (arg.type == QLatin1String("object"))
            typeOk = v.isObject();
        if (!typeOk) {
            problems << QStringLiteral("'%1' should be %2, got %3")
                            .arg(arg.name, arg.type,
                                 v.isString() ? QStringLiteral("string \"%1\"").arg(v.toString().left(40))
                                              : QStringLiteral("non-%1 value").arg(arg.type));
        }
        // Enum check.
        if (!arg.enumValues.isEmpty() && v.isString()
                && !arg.enumValues.contains(v.toString())) {
            problems << QStringLiteral("'%1' should be one of [%2], got \"%3\"")
                            .arg(arg.name, arg.enumValues.join(QStringLiteral(", ")),
                                 v.toString().left(40));
        }
    }
    // Unknown arguments (likely misnamings the alias pass missed).
    for (auto it = input.constBegin(); it != input.constEnd(); ++it) {
        if (!schema->findArg(it.key()))
            problems << QStringLiteral("unknown argument '%1'").arg(it.key());
    }
    return problems;
}

// `schemaOverride`: the schema of a tool that is not in the static registry (a connector tool).
inline ToolCallRepair normalizeToolCall(const QString &toolName, QJsonObject &input,
                                 const QString &rawArguments, QString *noteOut,
                                 const ToolSchema *schemaOverride = nullptr)
{
    bool fixed = false;
    const ToolSchema *schema = schemaOverride ? schemaOverride : toolSchema(toolName);

    // 1. The arguments were not a JSON object: strip code fences and fix the
    // classic weak-model tell, trailing commas, then parse once more.
    if (input.isEmpty() && !rawArguments.trimmed().isEmpty()) {
        QString s = rawArguments.trimmed();
        static const QRegularExpression fenceRe(
            QStringLiteral("^```[a-zA-Z]*\\s*\\n?(.*?)\\n?```\\s*$"),
            QRegularExpression::DotMatchesEverythingOption);
        const QRegularExpressionMatch fm = fenceRe.match(s);
        if (fm.hasMatch())
            s = fm.captured(1).trimmed();
        QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
        if ((doc.isNull() || !doc.isObject()) && s.contains(QChar(','))) {
            s.replace(QRegularExpression(QStringLiteral(",\\s*([}\\]])")),
                      QStringLiteral("\\1"));
            doc = QJsonDocument::fromJson(s.toUtf8());
        }
        if (!doc.isNull() && doc.isObject() && !doc.object().isEmpty()) {
            input = doc.object();
            fixed = true;
        }
    }

    // Unknown tools keep the gate's deny path; nothing to repair against.
    if (!schema)
        return fixed ? ToolCallRepair::Repaired : ToolCallRepair::Ok;

    // 2. Rename known near-miss argument names (never clobber a real one).
    // A redundant alias alongside its canonical name is dropped.
    for (auto it = schema->aliases.constBegin(); it != schema->aliases.constEnd(); ++it) {
        const QString alias = it.key();
        const QString canonical = it.value();
        if (!input.contains(alias))
            continue;
        if (input.contains(canonical)) {
            input.remove(alias);
            fixed = true;
        } else {
            input[canonical] = input.value(alias);
            input.remove(alias);
            fixed = true;
        }
    }

    // 3. Coerce stringified scalars for typed arguments.
    for (auto it = schema->coercions.constBegin(); it != schema->coercions.constEnd(); ++it) {
        const QString arg = it.key();
        if (!input.contains(arg))
            continue;
        bool ok = false;
        const QJsonValue coerced = coerceScalar(input.value(arg), it.value(), &ok);
        if (ok) {
            input[arg] = coerced;
            fixed = true;
        }
    }

    // 4. Validate against the registry: field-level errors, not just
    // "missing X", so the model knows exactly what to fix.
    const QStringList problems = validateToolCall(toolName, input, schema);
    if (!problems.isEmpty()) {
        if (noteOut) {
            *noteOut = QStringLiteral("Your '%1' tool call was not run:\n- %2\n")
                           .arg(toolName, problems.join(QStringLiteral("\n- ")))
                       + QStringLiteral("Call it again with corrected arguments. Required: %1. Known arguments: %2.")
                           .arg(schema->requiredArgs().join(QStringLiteral(", ")),
                                schema->knownArgs().join(QStringLiteral(", ")));
            if (!rawArguments.trimmed().isEmpty())
                *noteOut += QStringLiteral(" Your arguments were: %1")
                                .arg(rawArguments.trimmed().left(300));
        }
        return ToolCallRepair::Unrepairable;
    }
    return fixed ? ToolCallRepair::Repaired : ToolCallRepair::Ok;
}

// ---- P2: text/JSON tool protocol -------------------------------------------
// For models without native function calling, tool calls ride in the reply
// text as a JSON envelope:
//   {"text": "...", "tool_calls": [{"name": "bash", "input": {...}}]}
// or a single call: {"tool": "bash", "input": {...}}. In Ollama JSON mode
// the whole reply is that object (constrained by the envelope schema as the
// `format`); elsewhere the object may sit inside a ```json / ```tool fence.
// A call is accepted only when its name is a real tool in the registry, so
// prose that happens to contain JSON cannot misfire into actions.
struct TextToolCall {
    QString name;
    QJsonObject input;
};

// `isExtra`: names that are tools although they are not in the static registry (connector tools).
using KnownToolFn = std::function<bool(const QString &)>;

inline QList<TextToolCall> callsFromEnvelope(const QJsonObject &obj, const KnownToolFn &isExtra = {})
{
    QList<TextToolCall> out;
    auto accept = [&out, &isExtra](const QString &name, const QJsonValue &input) {
        if (name.isEmpty() || (!toolSchema(name) && !(isExtra && isExtra(name))))
            return;
        TextToolCall tc;
        tc.name = name;
        tc.input = input.isObject() ? input.toObject() : QJsonObject();
        // A stringified input is common from weak models; parse it through.
        if (input.isString()) {
            const QJsonDocument d = QJsonDocument::fromJson(input.toString().toUtf8());
            if (d.isObject())
                tc.input = d.object();
        }
        out.append(tc);
    };
    const QJsonArray arr = obj.value(QStringLiteral("tool_calls")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject c = v.toObject();
        accept(c.value(QStringLiteral("name")).toString(
                   c.value(QStringLiteral("tool")).toString()),
               c.value(QStringLiteral("input")));
    }
    if (out.isEmpty())   // single-call shape: {"tool": ..., "input": ...}
        accept(obj.value(QStringLiteral("tool")).toString(), obj.value(QStringLiteral("input")));
    return out;
}

inline QList<TextToolCall> extractTextToolCalls(const QString &text, const KnownToolFn &isExtra = {})
{
    if (text.trimmed().isEmpty())
        return {};
    // 1. The whole reply is the envelope (Ollama JSON mode).
    QJsonParseError err;
    const QJsonDocument whole = QJsonDocument::fromJson(text.trimmed().toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && whole.isObject()) {
        const QList<TextToolCall> found = callsFromEnvelope(whole.object(), isExtra);
        if (!found.isEmpty())
            return found;
    }
    // 2. Fenced blocks: ```json or ```tool.
    static const QRegularExpression fenceRe(
        QStringLiteral("```(?:json|tool)\\s*\\n(.*?)\\n?```"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = fenceRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QJsonDocument doc = QJsonDocument::fromJson(m.captured(1).trimmed().toUtf8());
        if (doc.isObject()) {
            const QList<TextToolCall> found = callsFromEnvelope(doc.object(), isExtra);
            if (!found.isEmpty())
                return found;
        }
    }
    return {};
}

} // namespace

} // namespace noch::buddyprovider

