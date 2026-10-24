#include "OutputGuard.h"

#include <QRegularExpression>

namespace {
// The same regex shorthand the policy implementation files use, kept
// local so the guard does not depend on AgentPolicy's internals.
using RX = QRegularExpression;
constexpr auto CI = QRegularExpression::CaseInsensitiveOption;
} // namespace

QString OutputGuard::redactSecrets(const QString &text, const QStringList &knownSecrets)
{
    if (text.isEmpty())
        return text;
    QString out = text;
    for (const QString &s : knownSecrets) {
        if (s.trimmed().size() >= 8)
            out.replace(s.trimmed(), QStringLiteral("[redacted]"));
    }
    static const RX patterns[] = {
        RX(QStringLiteral(R"RX(-----BEGIN [A-Z ]*PRIVATE KEY-----[\s\S]*?-----END [A-Z ]*PRIVATE KEY-----)RX")),
        RX(QStringLiteral(R"RX(\bsk-(?:ant-|proj-)?[A-Za-z0-9_\-]{20,})RX")),
        RX(QStringLiteral(R"RX(\bAKIA[0-9A-Z]{16}\b)RX")),
        RX(QStringLiteral(R"RX(\b(?:ASIA|AGPA|AIDA|AROA|AIPA|ANPA|ANVA|AROA)[0-9A-Z]{16}\b)RX")),
        RX(QStringLiteral(R"RX((?:(?:aws)?_?secret_?(?:access)?_?key|AWS_SECRET_ACCESS_KEY)\s*[:=]\s*["']?([A-Za-z0-9/+=]{30,}))RX"), CI),
        RX(QStringLiteral(R"RX(\bgh[pousr]_[A-Za-z0-9]{30,})RX")),
        RX(QStringLiteral(R"RX(\bgithub_pat_[A-Za-z0-9_]{40,})RX")),
        RX(QStringLiteral(R"RX(\bxox[abprs]-[A-Za-z0-9\-]{10,})RX")),
        RX(QStringLiteral(R"RX(\bAIza[0-9A-Za-z_\-]{35}\b)RX")),
        RX(QStringLiteral(R"RX(\beyJ[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,})RX")),
        RX(QStringLiteral(R"RX(\bBearer\s+[A-Za-z0-9\-._~+/]+=*)RX"), CI),
        RX(QStringLiteral(R"RX(\b(?:x-api-key|api-key)\s*[:=]\s*["']?[A-Za-z0-9_\-]{16,})RX"), CI),
        RX(QStringLiteral(R"RX((?:postgres|mysql|mongodb|redis|amqp|https?)://[^:\s/@]+:[^@\s/]{6,}@)RX"), CI),
        RX(QStringLiteral(R"RX(\b[rs]k_(?:live|test)_[A-Za-z0-9]{16,})RX")),
        RX(QStringLiteral(R"RX(\bhf_[A-Za-z0-9]{20,})RX")),
        RX(QStringLiteral(R"RX(\bnpm_[A-Za-z0-9]{20,})RX")),
    };
    for (const RX &re : patterns)
        out.replace(re, QStringLiteral("[redacted]"));
    // Key = value forms. Keep the key, drop the value.
    static const RX kv(QStringLiteral(
        R"RX(((?:api[_-]?key|secret(?:[_-]?access[_-]?key)?|password|passwd|pwd|access[_-]?token|auth[_-]?token|client[_-]?secret|private[_-]?key|session[_-]?token)\s*[:=]\s*["']?)([^\s"',;]{6,}))RX"), CI);
    out.replace(kv, QStringLiteral("\\1[redacted]"));
    // JSON "password": "..."
    static const RX jsonKv(QStringLiteral(
        R"RX(("(?:api[_-]?key|secret|password|passwd|access[_-]?token|auth[_-]?token|client[_-]?secret)"\s*:\s*")([^"]{6,})")RX"), CI);
    out.replace(jsonKv, QStringLiteral("\\1[redacted]\""));
    return out;
}

bool OutputGuard::looksLikePromptInjection(const QString &text)
{
    // Strip invisible characters first so "ig\u200bnore previous" still matches.
    const QString t = stripInvisible(text);
    static const RX re(QStringLiteral(
        R"RX(ignore\s+(all\s+|any\s+|the\s+)?(of\s+the\s+)?(previous|prior|above|earlier)\s+(instructions|prompts|directives|messages)|)RX"
        R"RX(disregard\s+(all\s+|the\s+|your\s+)?(system|previous|prior)\s+(prompt|instructions|directives)|)RX"
        R"RX((forget|override|supersede|discard)\s+(all\s+|the\s+|your\s+)?(previous|prior|system\s+)?(instructions|prompts|directives|orders)|)RX"
        R"RX(you\s+are\s+now\s+(in\s+)?(developer|dan|jailbreak)|)RX"
        R"RX(<\|im_start\|>|\[\s*system\s*\]\s*:|new\s+(system\s+|updated\s+)?(instructions|directives|orders)\s*:|)RX"
        R"RX((send|post|upload|exfiltrate)\s+(your|the|all)\s+(api\s*key|credentials|secrets|tokens|passwords)|)RX"
        R"RX((do\s+not\s+tell\s+the\s+user|hidden\s+instruction|tool\s+call\s+now)|)RX"
        R"RX(<\/?untrusted_content\b)RX"), CI);
    return re.match(t).hasMatch();
}

QString OutputGuard::stripInvisible(const QString &text, bool *changed)
{
    if (text.isEmpty()) {
        if (changed) *changed = false;
        return text;
    }
    QString out;
    out.reserve(text.size());
    bool any = false;
    for (QChar ch : text) {
        const ushort u = ch.unicode();
        // ASCII control except TAB/LF/CR; DEL; C1 controls; zero-widths;
        // bidi overrides; soft hyphen; BOM; word joiner; invisible separators;
        // Unicode "Tags" block used for ASCII smuggling (U+E0001..U+E007F).
        const bool drop = (u < 0x20 && u != 0x09 && u != 0x0A && u != 0x0D)
            || u == 0x7F
            || (u >= 0x80 && u <= 0x9F)
            || u == 0xAD
            || u == 0x200B || u == 0x200C || u == 0x200D || u == 0x200E || u == 0x200F
            || (u >= 0x202A && u <= 0x202E)
            || (u >= 0x2060 && u <= 0x2064)
            || (u >= 0x2066 && u <= 0x2069)
            || u == 0xFEFF
            || u == 0xFFFE || u == 0xFFFF
            
            ;
        // Also drop Tag characters encoded as surrogate pairs (U+E00xx).
        if (ch.isHighSurrogate()) {
            // peek handled below by iterating QString which yields QChars
        }
        if (drop) {
            any = true;
            continue;
        }
        out += ch;
    }
    // Second pass for supplementary-plane Tags (U+E0001..E007F as surrogates).
    QString cleaned;
    cleaned.reserve(out.size());
    for (int i = 0; i < out.size(); ++i) {
        const QChar c = out.at(i);
        if (c.isHighSurrogate() && i + 1 < out.size() && out.at(i + 1).isLowSurrogate()) {
            const uint cp = QChar::surrogateToUcs4(c, out.at(i + 1));
            if ((cp >= 0xE0001 && cp <= 0xE007F) || cp == 0xE0000) {
                any = true;
                ++i;
                continue;
            }
            cleaned += c;
            cleaned += out.at(++i);
            continue;
        }
        cleaned += c;
    }
    if (changed)
        *changed = any;
    return cleaned;
}

QString OutputGuard::displaySafe(const QString &text, int maxChars, bool *hidden)
{
    QString out;
    out.reserve(qMin(text.size() * 2, maxChars + 32));
    bool any = false;
    auto appendEsc = [&](const QString &label) {
        any = true;
        out += QLatin1Char('<') + label + QLatin1Char('>');
    };
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        const ushort u = c.unicode();
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            const uint cp = QChar::surrogateToUcs4(c, text.at(i + 1));
            ++i;
            if (cp >= 0xE0001 && cp <= 0xE007F) {
                appendEsc(QStringLiteral("tag:%1").arg(QChar(ushort(cp - 0xE0000))));
                continue;
            }
            out += QChar::highSurrogate(cp);
            out += QChar::lowSurrogate(cp);
            continue;
        }
        if (u == 0x200B) { appendEsc(QStringLiteral("zwsp")); continue; }
        if (u == 0x200C) { appendEsc(QStringLiteral("zwnj")); continue; }
        if (u == 0x200D) { appendEsc(QStringLiteral("zwj")); continue; }
        if (u == 0x200E) { appendEsc(QStringLiteral("lrm")); continue; }
        if (u == 0x200F) { appendEsc(QStringLiteral("rlm")); continue; }
        if (u == 0x202A) { appendEsc(QStringLiteral("lre")); continue; }
        if (u == 0x202B) { appendEsc(QStringLiteral("rle")); continue; }
        if (u == 0x202C) { appendEsc(QStringLiteral("pdf")); continue; }
        if (u == 0x202D) { appendEsc(QStringLiteral("lro")); continue; }
        if (u == 0x202E) { appendEsc(QStringLiteral("rlo")); continue; }
        if (u == 0x2066) { appendEsc(QStringLiteral("lri")); continue; }
        if (u == 0x2067) { appendEsc(QStringLiteral("rli")); continue; }
        if (u == 0x2068) { appendEsc(QStringLiteral("fsi")); continue; }
        if (u == 0x2069) { appendEsc(QStringLiteral("pdi")); continue; }
        if (u == 0xFEFF) { appendEsc(QStringLiteral("bom")); continue; }
        if (u < 0x20 && u != 0x09 && u != 0x0A && u != 0x0D) {
            appendEsc(QStringLiteral("u+%1").arg(u, 4, 16, QLatin1Char('0')));
            continue;
        }
        out += c;
    }
    if (hidden)
        *hidden = any;
    if (maxChars > 0 && out.size() > maxChars) {
        const int head = maxChars * 2 / 3;
        const int tail = maxChars - head;
        return out.left(head) + QStringLiteral(" …[+%1 chars]… ").arg(out.size() - maxChars) + out.right(tail);
    }
    return out;
}

QString OutputGuard::neutralizeFence(const QString &text)
{
    QString out = text;
    // Break any attempt to close or reopen the untrusted_content fence.
    static const RX re(QStringLiteral(R"RX(</?\s*untrusted_content\b[^>]*>)RX"), CI);
    out.replace(re, QStringLiteral("[untrusted_content-tag]"));
    return out;
}
