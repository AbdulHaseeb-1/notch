#include "ComputerUseKeys.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace cu {

namespace detail {

QString canonicalModifier(const QString &t)
{
    if (t == QLatin1String("ctrl") || t == QLatin1String("control") || t == QLatin1String("ctl"))
        return QStringLiteral("ctrl");
    if (t == QLatin1String("alt") || t == QLatin1String("option") || t == QLatin1String("menu"))
        return QStringLiteral("alt");
    if (t == QLatin1String("shift"))
        return QStringLiteral("shift");
    if (t == QLatin1String("super") || t == QLatin1String("win") || t == QLatin1String("windows")
            || t == QLatin1String("meta") || t == QLatin1String("cmd") || t == QLatin1String("command"))
        return QStringLiteral("super");
    return QString();
}

QString canonicalKey(const QString &t)
{
    static const QHash<QString, QString> names{
        { "return", "return" }, { "enter", "return" }, { "kp_enter", "return" },
        { "escape", "escape" }, { "esc", "escape" },
        { "tab", "tab" }, { "space", "space" }, { "spacebar", "space" },
        { "backspace", "backspace" }, { "bksp", "backspace" },
        { "delete", "delete" }, { "del", "delete" }, { "insert", "insert" }, { "ins", "insert" },
        { "up", "up" }, { "arrowup", "up" }, { "down", "down" }, { "arrowdown", "down" },
        { "left", "left" }, { "arrowleft", "left" }, { "right", "right" }, { "arrowright", "right" },
        { "home", "home" }, { "end", "end" },
        { "pageup", "page_up" }, { "page_up", "page_up" }, { "prior", "page_up" }, { "pgup", "page_up" },
        { "pagedown", "page_down" }, { "page_down", "page_down" }, { "next", "page_down" }, { "pgdn", "page_down" },
        { "capslock", "caps_lock" }, { "caps_lock", "caps_lock" }, { "printscreen", "print" }, { "print", "print" },
        { "minus", "-" }, { "plus", "+" }, { "equal", "=" }, { "comma", "," }, { "period", "." },
        { "slash", "/" }, { "backslash", "\\" }, { "semicolon", ";" }, { "apostrophe", "'" },
        { "grave", "`" }, { "bracketleft", "[" }, { "bracketright", "]" },
    };
    const auto it = names.constFind(t);
    if (it != names.cend())
        return it.value();
    if (t.size() >= 2 && t.size() <= 3 && t.at(0) == QLatin1Char('f')) {
        bool ok = false;
        const int n = t.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 24)
            return t;
    }
    if (t.size() == 1 && t.at(0).unicode() > 32 && t.at(0).unicode() < 127)
        return t;
    return QString();
}

} // namespace detail

QString KeyCombo::toString() const
{
    QStringList parts = modifiers;
    parts << key;
    return parts.join(QLatin1Char('+'));
}

KeyCombo parseKeyCombo(const QString &combo)
{
    KeyCombo k;
    const QString s = combo.trimmed().toLower();
    if (s.isEmpty()) {
        k.error = QStringLiteral("empty key combo");
        return k;
    }
    // A literal "+" key: "ctrl+plus" or "ctrl++".
    QStringList parts = s.split(QLatin1Char('+'));
    if (s.endsWith(QLatin1String("++"))) {
        parts = s.left(s.size() - 2).split(QLatin1Char('+'), Qt::SkipEmptyParts);
        parts << QStringLiteral("+");
    } else if (s == QLatin1String("+")) {
        parts = { QStringLiteral("+") };
    }
    for (int i = 0; i < parts.size(); ++i) {
        const QString t = parts.at(i).trimmed();
        if (t.isEmpty()) {
            k.error = QStringLiteral("malformed key combo '%1'").arg(combo);
            return k;
        }
        const bool last = i == parts.size() - 1;
        const QString mod = detail::canonicalModifier(t);
        if (!last) {
            if (mod.isEmpty()) {
                k.error = QStringLiteral("'%1' is not a modifier (ctrl, alt, shift, super)").arg(t);
                return k;
            }
            if (!k.modifiers.contains(mod))
                k.modifiers << mod;
            continue;
        }
        if (!mod.isEmpty() && parts.size() == 1) {   // a lone modifier, e.g. "super"
            k.key = mod;
            continue;
        }
        const QString key = detail::canonicalKey(t);
        if (key.isEmpty()) {
            k.error = QStringLiteral("unknown key '%1'").arg(t);
            return k;
        }
        k.key = key;
    }
    if (k.key.isEmpty())
        k.error = QStringLiteral("no key in '%1'").arg(combo);
    return k;
}

bool isRiskyKeyCombo(const KeyCombo &c)
{
    static const QStringList risky{
        "super+r", "super+x", "super+l", "ctrl+shift+escape", "ctrl+alt+delete", "alt+f4",
        "ctrl+shift+return", "super",
    };
    QStringList mods = c.modifiers;
    std::sort(mods.begin(), mods.end());
    QStringList sortedParts = mods;
    sortedParts << c.key;
    const QString norm = sortedParts.join(QLatin1Char('+'));
    for (const QString &r : risky) {
        KeyCombo rk = parseKeyCombo(r);
        QStringList rm = rk.modifiers;
        std::sort(rm.begin(), rm.end());
        rm << rk.key;
        if (rm.join(QLatin1Char('+')) == norm)
            return true;
    }
    return false;
}

bool looksLikeShellText(const QString &text)
{
    static const QRegularExpression rx(
        QStringLiteral("(\\b(powershell|pwsh|cmd(\\.exe)?|bash|wsl|curl|wget|invoke-webrequest|iwr|iex|"
                       "invoke-expression|start-process|certutil|mshta|rundll32|regsvr32|bitsadmin|"
                       "reg\\s+(add|delete)|schtasks|net\\s+user|set-mppreference)\\b)"
                       "|(-enc(odedcommand)?\\s)|(\\|\\s*(sh|bash|iex)\\b)|(rm\\s+-rf)|(del\\s+/[sfq])|(format\\s+[a-z]:)"),
        QRegularExpression::CaseInsensitiveOption);
    return rx.match(text).hasMatch();
}

bool comboTypesText(const KeyCombo &combo)
{
    if (!combo.valid() || combo.key.size() != 1)
        return false;
    const uint u = combo.key.at(0).unicode();
    if (u < 32 || u > 126)
        return false;
    for (const QString &m : combo.modifiers) {
        if (m != QLatin1String("shift"))
            return false;
    }
    return true;
}

} // namespace cu
