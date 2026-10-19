#include "ComputerUse.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace cu {

namespace {

struct Def { const char *name; Action action; };
const Def kActions[] = {
    { "screenshot", Action::Screenshot },
    { "zoom", Action::Zoom },
    { "cursor_position", Action::CursorPosition },
    { "wait", Action::Wait },
    { "mouse_move", Action::MouseMove },
    { "left_click", Action::LeftClick },
    { "right_click", Action::RightClick },
    { "middle_click", Action::MiddleClick },
    { "double_click", Action::DoubleClick },
    { "triple_click", Action::TripleClick },
    { "left_click_drag", Action::LeftClickDrag },
    { "scroll", Action::Scroll },
    { "type", Action::Type },
    { "key", Action::Key },
    { "left_mouse_down", Action::LeftMouseDown },
    { "left_mouse_up", Action::LeftMouseUp },
    { "hold_key", Action::HoldKey },
};

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

// "shift", "ctrl+shift": keys held for the duration of one click / scroll / drag.
bool parseModifierList(const QString &text, QStringList *out, QString *error)
{
    out->clear();
    const QString s = text.trimmed().toLower();
    if (s.isEmpty())
        return true;
    for (const QString &part : s.split(QLatin1Char('+'))) {
        const QString mod = canonicalModifier(part.trimmed());
        if (mod.isEmpty()) {
            *error = QStringLiteral("'text' on a click, drag or scroll holds modifier keys only "
                                    "(shift, ctrl, alt, super), not '%1'").arg(part.trimmed());
            return false;
        }
        if (!out->contains(mod))
            out->append(mod);
    }
    return true;
}

bool readPoint(const QJsonValue &v, QPoint *out)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 2 || !a.at(0).isDouble() || !a.at(1).isDouble())
        return false;
    *out = QPoint(qRound(a.at(0).toDouble()), qRound(a.at(1).toDouble()));
    return true;
}

} // namespace

QString KeyCombo::toString() const
{
    QStringList parts = modifiers;
    parts << key;
    return parts.join(QLatin1Char('+'));
}

QStringList actionNames()
{
    QStringList out;
    for (const Def &d : kActions)
        out << QString::fromLatin1(d.name);
    return out;
}

bool isInputAction(Action a)
{
    switch (a) {
    case Action::MouseMove: case Action::LeftClick: case Action::RightClick: case Action::MiddleClick:
    case Action::DoubleClick: case Action::TripleClick: case Action::LeftClickDrag: case Action::Scroll:
    case Action::Type: case Action::Key:
    case Action::LeftMouseDown: case Action::LeftMouseUp: case Action::HoldKey:
        return true;
    default:
        return false;
    }
}

ImageLimits standardLimits()
{
    return ImageLimits{};
}

ImageLimits sharpLimits()
{
    ImageLimits l;
    l.box = QSize(1920, 1080);
    l.matchOrientation = true;
    l.maxLongEdge = 2576;
    l.maxVisualTokens = 4784;
    return l;
}

ImageLimits legacySharpLimits()
{
    ImageLimits l;
    l.box = QSize(1568, 1568);
    l.matchOrientation = true;
    l.maxLongEdge = 1568;
    l.maxPixels = 1150000;
    return l;
}

int visualTokens(const QSize &size)
{
    return ((size.width() + 27) / 28) * ((size.height() + 27) / 28);
}

QSize fitToLimits(const QSize &logical, const ImageLimits &lim)
{
    QSize box = lim.box;
    if (lim.matchOrientation && logical.height() > logical.width())
        box.transpose();
    const QSize base = CoordinateMapper::agentSizeFor(logical, box);
    const auto over = [&lim](const QSize &s) {
        return (lim.maxLongEdge > 0 && qMax(s.width(), s.height()) > lim.maxLongEdge)
            || (lim.maxPixels > 0 && qint64(s.width()) * s.height() > lim.maxPixels)
            || (lim.maxVisualTokens > 0 && visualTokens(s) > lim.maxVisualTokens);
    };
    if (!over(base))
        return base;
    // Scale straight to the pixel / edge limits, then step down until the
    // token count (a ceiling per axis) fits too.
    double f = 1.0;
    if (lim.maxLongEdge > 0)
        f = qMin(f, double(lim.maxLongEdge) / qMax(base.width(), base.height()));
    if (lim.maxPixels > 0)
        f = qMin(f, std::sqrt(double(lim.maxPixels) / (double(base.width()) * base.height())));
    QSize cur = base;
    for (int i = 0; i < 400 && f > 0.05; ++i, f *= 0.99) {
        cur = QSize(qMax(1, int(std::floor(base.width() * f))), qMax(1, int(std::floor(base.height() * f))));
        if (!over(cur))
            break;
    }
    return cur;
}

bool ParsedAction::isInput() const { return isInputAction(action); }

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
        const QString mod = canonicalModifier(t);
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
        const QString key = canonicalKey(t);
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
    const QString s = c.toString();
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
    Q_UNUSED(s);
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

ParsedAction parseAction(const QJsonObject &input, const QSize &agentSize)
{
    ParsedAction p;
    p.name = input.value(QStringLiteral("action")).toString().trimmed().toLower();
    for (const Def &d : kActions)
        if (p.name == QLatin1String(d.name))
            p.action = d.action;
    if (p.action == Action::Invalid) {
        p.error = p.name.isEmpty() ? QStringLiteral("'action' is required")
                                   : QStringLiteral("unsupported action '%1'").arg(p.name);
        return p;
    }
    auto inSpace = [&](const QPoint &pt) {
        return pt.x() >= 0 && pt.y() >= 0 && pt.x() < agentSize.width() && pt.y() < agentSize.height();
    };
    const QString spaceText = QStringLiteral("%1x%2").arg(agentSize.width()).arg(agentSize.height());
    if (input.contains(QStringLiteral("coordinate"))) {
        if (!readPoint(input.value(QStringLiteral("coordinate")), &p.coord)) {
            p.error = QStringLiteral("'coordinate' must be [x, y] numbers");
            return p;
        }
        if (!inSpace(p.coord)) {
            p.error = QStringLiteral("coordinate [%1, %2] is outside the %3 screenshot")
                          .arg(p.coord.x()).arg(p.coord.y()).arg(spaceText);
            return p;
        }
        p.hasCoord = true;
    }
    if (input.contains(QStringLiteral("start_coordinate"))) {
        if (!readPoint(input.value(QStringLiteral("start_coordinate")), &p.start) || !inSpace(p.start)) {
            p.error = QStringLiteral("'start_coordinate' must be [x, y] inside the %1 screenshot").arg(spaceText);
            return p;
        }
        p.hasStart = true;
    }

    // Modifier keys (the toolset's `text` on clicks, drags and scrolls).
    switch (p.action) {
    case Action::LeftClick: case Action::RightClick: case Action::MiddleClick:
    case Action::DoubleClick: case Action::TripleClick: case Action::LeftClickDrag: case Action::Scroll: {
        QString merr;
        if (!parseModifierList(input.value(QStringLiteral("text")).toString(), &p.modifiers, &merr)) {
            p.error = merr;
            return p;
        }
        break;
    }
    default:
        break;
    }

    switch (p.action) {
    case Action::Screenshot:
        if (input.contains(QStringLiteral("display")))
            p.display = input.value(QStringLiteral("display")).toInt(-1);
        break;
    case Action::Zoom: {
        const QJsonArray r = input.value(QStringLiteral("region")).toArray();
        if (r.size() != 4) {
            p.error = QStringLiteral("'region' must be [x0, y0, x1, y1]");
            break;
        }
        const QPoint a(qRound(r.at(0).toDouble()), qRound(r.at(1).toDouble()));
        const QPoint b(qRound(r.at(2).toDouble()), qRound(r.at(3).toDouble()));
        // [x0, y0, x1, y1] with x1/y1 exclusive, like Claude's zoom region.
        const QRect rect(qMin(a.x(), b.x()), qMin(a.y(), b.y()), qAbs(b.x() - a.x()), qAbs(b.y() - a.y()));
        if (!inSpace(rect.topLeft()) || rect.x() + rect.width() > agentSize.width()
                || rect.y() + rect.height() > agentSize.height()
                || rect.width() < 8 || rect.height() < 8)
            p.error = QStringLiteral("'region' must be at least 8x8 and inside the %1 screenshot").arg(spaceText);
        else
            p.region = rect;
        break;
    }
    case Action::Wait: {
        const double asked = input.value(QStringLiteral("duration")).toDouble(1.0);
        p.duration = qBound(0.1, asked, kMaxWaitSeconds);
        p.durationClamped = asked > kMaxWaitSeconds;
        break;
    }
    case Action::LeftMouseDown:
    case Action::LeftMouseUp:
        break;
    case Action::HoldKey: {
        p.combo = parseKeyCombo(input.value(QStringLiteral("text")).toString());
        if (!p.combo.valid()) {
            p.error = p.combo.error;
            break;
        }
        const double asked = input.value(QStringLiteral("duration")).toDouble(1.0);
        p.duration = qBound(0.1, asked, kMaxWaitSeconds);
        p.durationClamped = asked > kMaxWaitSeconds;
        break;
    }
    case Action::MouseMove:
        if (!p.hasCoord)
            p.error = QStringLiteral("'coordinate' [x, y] is required for mouse_move");
        break;
    case Action::LeftClickDrag:
        if (!p.hasCoord)
            p.error = QStringLiteral("'coordinate' [x, y] (the drop point) is required for left_click_drag");
        break;
    case Action::Scroll: {
        p.scrollDirection = input.value(QStringLiteral("scroll_direction")).toString(QStringLiteral("down")).toLower();
        static const QStringList dirs{ "up", "down", "left", "right" };
        if (!dirs.contains(p.scrollDirection))
            p.error = QStringLiteral("'scroll_direction' must be up, down, left or right");
        p.scrollAmount = qBound(1, input.value(QStringLiteral("scroll_amount")).toInt(3), kMaxScrollAmount);
        break;
    }
    case Action::Type:
        p.text = input.value(QStringLiteral("text")).toString();
        if (p.text.isEmpty())
            p.error = QStringLiteral("'text' is required for type");
        else if (p.text.size() > kMaxTypeChars)
            p.error = QStringLiteral("'text' is longer than %1 characters; type it in parts").arg(kMaxTypeChars);
        break;
    case Action::Key:
        p.combo = parseKeyCombo(input.value(QStringLiteral("text")).toString());
        if (!p.combo.valid())
            p.error = p.combo.error;
        p.repeat = qBound(1, input.value(QStringLiteral("repeat")).toInt(1), kMaxKeyRepeat);
        break;
    default:
        break;
    }
    return p;
}

} // namespace cu
