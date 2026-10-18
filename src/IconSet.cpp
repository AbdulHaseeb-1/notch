#include "IconSet.h"

#include <QDebug>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

#include <algorithm>

namespace {
const char *kIconDir = ":/qt/qml/noch/qml/icons";
}

IconSet::IconSet(QObject *parent)
    : QObject(parent)
{
    load(QString::fromLatin1(kIconDir));
    if (m_paths.isEmpty())
        qWarning() << "IconSet: no icons found under" << kIconDir;
}

bool IconSet::parseSvg(const QByteArray &svg, QString *pathData, bool *filled)
{
    QXmlStreamReader xml(svg);
    QStringList parts;
    bool fill = false;
    bool sawRoot = false;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        const auto name = xml.name();
        if (name == QLatin1String("svg")) {
            sawRoot = true;
            const auto f = xml.attributes().value(QLatin1String("fill"));
            fill = !f.isEmpty() && f != QLatin1String("none");
        } else if (name == QLatin1String("path")) {
            const auto d = xml.attributes().value(QLatin1String("d")).toString().trimmed();
            if (d.isEmpty())
                continue;
            // Paths are joined into one string, so a leading relative moveto
            // would be resolved against the previous subpath. The generator
            // (tools/build_icons.py) writes absolute starts; reject otherwise.
            if (d.startsWith(QLatin1Char('m')))
                return false;
            parts << d;
        }
    }
    if (xml.hasError() || !sawRoot || parts.isEmpty())
        return false;
    if (pathData)
        *pathData = parts.join(QLatin1Char(' '));
    if (filled)
        *filled = fill;
    return true;
}

void IconSet::load(const QString &dir)
{
    QDirIterator it(dir, {QStringLiteral("*.svg")}, QDir::Files);
    while (it.hasNext()) {
        const QString file = it.next();
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QString d;
        bool filled = false;
        if (!parseSvg(f.readAll(), &d, &filled)) {
            qWarning() << "IconSet: could not parse" << file;
            continue;
        }
        const QString key = QFileInfo(file).completeBaseName();
        m_paths.insert(key, d);
        m_filled.insert(key, filled);
    }
}

QString IconSet::path(const QString &name) const
{
    return m_paths.value(name);
}

bool IconSet::isFilled(const QString &name) const
{
    return m_filled.value(name, false);
}

QStringList IconSet::names() const
{
    QStringList keys = m_paths.keys();
    std::sort(keys.begin(), keys.end());
    return keys;
}
