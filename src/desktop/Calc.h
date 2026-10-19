#pragma once

#include <QChar>
#include <QString>
#include <QtGlobal>

#include <cmath>

// The command palette's inline calculator: + - * / ^, parentheses, unary
// minus, sqrt() and trailing percent, evaluated by a small recursive-descent
// parser. No QML eval, no surprises.
namespace nochcalc {

class Parser
{
public:
    explicit Parser(const QString &text)
        : m_text(text)
    {
    }

    bool parse(double *out)
    {
        m_pos = 0;
        m_depth = 0;
        m_ok = true;
        skipSpace();
        const double value = expression();
        skipSpace();
        if (m_pos != m_text.size() || !m_ok || !std::isfinite(value))
            return false;
        *out = value;
        return true;
    }

private:
    void skipSpace()
    {
        while (m_pos < m_text.size() && m_text.at(m_pos).isSpace())
            ++m_pos;
    }

    bool eat(QChar c)
    {
        skipSpace();
        if (m_pos < m_text.size() && m_text.at(m_pos) == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    QChar peek()
    {
        skipSpace();
        return m_pos < m_text.size() ? m_text.at(m_pos) : QChar();
    }

    double expression()
    {
        double value = term();
        for (;;) {
            if (eat(QLatin1Char('+')))
                value += term();
            else if (eat(QLatin1Char('-')))
                value -= term();
            else
                return value;
        }
    }

    double term()
    {
        double value = unary();
        for (;;) {
            if (eat(QLatin1Char('*')))
                value *= unary();
            else if (eat(QLatin1Char('/'))) {
                const double divisor = unary();
                if (divisor == 0.0) {
                    m_ok = false;
                    return 0.0;
                }
                value /= divisor;
            } else {
                return value;
            }
        }
    }

    // Unary minus binds looser than ^, the way maths books and common
    // calculators treat it: -2^2 = -(2^2) = -4, while (-2)^2 = 4.
    double unary()
    {
        DepthGuard guard(this);
        if (!m_ok)
            return 0.0;
        if (eat(QLatin1Char('-')))
            return -unary();
        if (eat(QLatin1Char('+')))
            return unary();
        return power();
    }

    double power()
    {
        const double base = postfix();
        if (eat(QLatin1Char('^')))
            return std::pow(base, unary()); // right-assoc; unary admits 2^-3
        return base;
    }

    double postfix()
    {
        double value = primary();
        while (eat(QLatin1Char('%')))
            value /= 100.0;
        return value;
    }

    double primary()
    {
        skipSpace();
        if (eat(QLatin1Char('('))) {
            const double value = expression();
            if (!eat(QLatin1Char(')')))
                m_ok = false;
            return value;
        }

        // sqrt(...) — the one function worth having.
        if (m_text.mid(m_pos).startsWith(QLatin1String("sqrt"), Qt::CaseInsensitive)) {
            m_pos += 4;
            skipSpace();
            if (!eat(QLatin1Char('('))) {
                m_ok = false;
                return 0.0;
            }
            const double value = expression();
            if (!eat(QLatin1Char(')'))) {
                m_ok = false;
                return 0.0;
            }
            return value < 0 ? (m_ok = false, 0.0) : std::sqrt(value);
        }

        int start = m_pos;
        while (m_pos < m_text.size()
               && (m_text.at(m_pos).isDigit() || m_text.at(m_pos) == QLatin1Char('.')))
            ++m_pos;
        bool numberOk = false;
        const double value = m_text.mid(start, m_pos - start).toDouble(&numberOk);
        if (!numberOk) {
            m_ok = false;
            m_pos = m_text.size();
            return 0.0;
        }
        return value;
    }

    // Recursion guard: every parsing cycle (parens, sqrt, chained unary
    // minus) passes through unary(), so counting invocations there bounds
    // the whole descent and a pasted "()()()..." cannot eat the stack.
    struct DepthGuard
    {
        int &depth;
        bool &ok;

        explicit DepthGuard(Parser *parser)
            : depth(parser->m_depth)
            , ok(parser->m_ok)
        {
            if (++depth > 100)
                ok = false;
        }
        ~DepthGuard() { --depth; }
    };

    QString m_text;
    int m_pos = 0;
    int m_depth = 0;
    bool m_ok = true;
};

// True when the text at least looks like a sum: a digit and an operator.
// The one word the parser knows — "sqrt" — is allowed too and counts as an
// operator, so sqrt(9) reaches the evaluator; any other letters keep prose
// out of the maths row.
inline bool looksLikeExpression(const QString &text)
{
    bool hasDigit = false;
    bool hasOperator = false;
    int i = 0;
    while (i < text.size()) {
        const QChar c = text.at(i);
        if (c.isDigit()) {
            hasDigit = true;
        } else if (QStringLiteral("+-*/^%()").contains(c)) {
            hasOperator = true;
        } else if (c.isSpace()) {
            // Whitespace between tokens is fine.
        } else if (text.mid(i, 4).compare(QLatin1String("sqrt"), Qt::CaseInsensitive) == 0) {
            hasOperator = true;
            i += 4;
            continue;
        } else {
            return false;
        }
        ++i;
    }
    return hasDigit && hasOperator;
}

// Formats the result: plain integers stay plain, everything else keeps up to
// six decimals, trimmed. Empty string when the text does not compute.
inline QString evaluate(const QString &text)
{
    Parser parser(text);
    double value = 0.0;
    if (!parser.parse(&value))
        return {};

    if (std::abs(value) < 1e15 && std::abs(value - std::round(value)) < 1e-9)
        return QString::number(static_cast<qlonglong>(std::llround(value)));

    QString formatted = QString::number(value, 'f', 6);
    while (formatted.contains(QLatin1Char('.')) && formatted.endsWith(QLatin1Char('0')))
        formatted.chop(1);
    if (formatted.endsWith(QLatin1Char('.')))
        formatted.chop(1);
    return formatted;
}

} // namespace nochcalc
