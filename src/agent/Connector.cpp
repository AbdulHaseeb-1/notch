#include "Connector.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace connectors {

QString stateName(State s)
{
    switch (s) {
    case State::Disconnected: return QStringLiteral("disconnected");
    case State::Connecting: return QStringLiteral("connecting");
    case State::Connected: return QStringLiteral("connected");
    case State::Error: return QStringLiteral("error");
    }
    return QStringLiteral("disconnected");
}

ToolSchema toToolSchema(const ToolDef &def)
{
    ToolSchema s;
    s.name = def.name;
    s.description = def.description;
    const QJsonObject props = def.inputSchema.value(QStringLiteral("properties")).toObject();
    QStringList required;
    for (const QJsonValue &r : def.inputSchema.value(QStringLiteral("required")).toArray())
        required << r.toString();
    for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
        const QJsonObject p = it.value().toObject();
        ToolArgDef a;
        a.name = it.key();
        // Only a plain type is checked; a union ("type": [...]) or anyOf is left to the server.
        const QJsonValue type = p.value(QStringLiteral("type"));
        a.type = type.isString() ? type.toString() : QString();
        a.description = p.value(QStringLiteral("description")).toString();
        a.required = required.contains(a.name);
        for (const QJsonValue &e : p.value(QStringLiteral("enum")).toArray())
            if (e.isString())
                a.enumValues << e.toString();
        s.args << a;
    }
    // Models often send numbers as strings; accept them where the schema says number.
    for (const ToolArgDef &a : std::as_const(s.args)) {
        if (a.type == QLatin1String("integer"))
            s.coercions.insert(a.name, QStringLiteral("int"));
        else if (a.type == QLatin1String("number"))
            s.coercions.insert(a.name, QStringLiteral("double"));
        else if (a.type == QLatin1String("boolean"))
            s.coercions.insert(a.name, QStringLiteral("bool"));
    }
    return s;
}

QString sanitizeText(const QString &text, int maxChars)
{
    QString out;
    out.reserve(qMin<qsizetype>(text.size(), maxChars + 16));
    for (QChar c : text) {
        const ushort u = c.unicode();
        // Zero-width, bidi controls, BOM, and the tag block used to smuggle text.
        if ((u >= 0x200B && u <= 0x200F) || (u >= 0x202A && u <= 0x202E) || (u >= 0x2060 && u <= 0x2064)
                || u == 0xFEFF || u >= 0xFFF0)
            continue;
        if (c.isHighSurrogate() || c.isLowSurrogate()) {
            out.append(c);
            continue;
        }
        if (u < 0x20 && u != '\n' && u != '\t') {
            if (u == '\r')
                continue;
            out.append(QLatin1Char(' '));
            continue;
        }
        out.append(c);
    }
    // Tag characters (U+E0000..U+E007F) are surrogate pairs in UTF-16: drop them.
    QString cleaned;
    cleaned.reserve(out.size());
    const QList<uint> cps = out.toUcs4();
    for (uint cp : cps) {
        const char32_t c32 = cp;
        if (!(cp >= 0xE0000 && cp <= 0xE007F))
            cleaned.append(QString::fromUcs4(&c32, 1));
    }
    if (cleaned.size() > maxChars)
        cleaned = cleaned.left(maxChars - 1) + QStringLiteral("…");
    return cleaned;
}

QString prettyToolName(const QString &name)
{
    QString n = name;
    if (n.startsWith(QLatin1String("mcp_")))
        n = n.mid(4);
    const int cut = n.indexOf(QLatin1Char('_'));
    if (cut <= 0)
        return n;
    QString head = n.left(cut);
    const QString rest = n.mid(cut + 1).replace(QLatin1Char('_'), QLatin1Char(' '));
    if (name.startsWith(QLatin1String("gmail_")) && !head.isEmpty())
        head[0] = head[0].toUpper();
    return head + QStringLiteral(": ") + rest;
}

QString iconForConnectorTool(const QString &name)
{
    if (name.startsWith(QLatin1String("gmail_")))
        return QStringLiteral("mail");
    return QStringLiteral("layers");
}

QString describeArguments(const QJsonObject &input, int maxValue)
{
    QStringList lines;
    for (auto it = input.constBegin(); it != input.constEnd(); ++it) {
        QString v = it.value().isString()
            ? it.value().toString()
            : QString::fromUtf8(QJsonDocument(QJsonArray{ it.value() }).toJson(QJsonDocument::Compact));
        if (!it.value().isString())
            v = v.mid(1, v.size() - 2);
        v = v.trimmed();
        if (v.size() > maxValue)
            v = v.left(maxValue - 1) + QStringLiteral("…");
        lines << QStringLiteral("%1: %2").arg(it.key(), v);
    }
    return lines.join(QLatin1Char('\n'));
}

bool isLoopbackHost(const QString &host)
{
    const QString h = host.toLower();
    return h == QLatin1String("localhost") || h == QLatin1String("127.0.0.1") || h == QLatin1String("::1")
        || h == QLatin1String("[::1]") || h.endsWith(QLatin1String(".localhost"));
}

QString slugify(const QString &name)
{
    static const QRegularExpression bad(QStringLiteral("[^a-z0-9]+"));
    QString s = name.toLower().trimmed();
    s.replace(bad, QStringLiteral("_"));
    while (s.startsWith(QLatin1Char('_')))
        s.remove(0, 1);
    while (s.endsWith(QLatin1Char('_')))
        s.chop(1);
    if (s.isEmpty())
        s = QStringLiteral("server");
    return s.left(24);
}

} // namespace connectors

void Connector::setState(connectors::State state, const QString &status, const QString &account)
{
    m_state = state;
    m_status = status;
    m_account = account;
    emit changed();
}
