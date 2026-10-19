#pragma once

#include <QHash>
#include <QObject>
#include <QString>

// The SVG icon set. Every qml/icons/*.svg (24x24, stroke="currentColor",
// <path> elements only) is compiled into the resources; IconSet reads them
// once at start-up and hands the path data to Glyph.qml, which draws it with
// a QML Shape so the icon stays vector-sharp at any size and DPI and can be
// tinted by state without re-rasterising anything.
//
// Lookups are Q_INVOKABLE string returns rather than a QVariantMap property
// on purpose: a map property would be converted to a JS object for every
// Glyph binding evaluation.
class IconSet : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count CONSTANT)
public:
    explicit IconSet(QObject *parent = nullptr);

    // Path data for an icon, or an empty string for an unknown name.
    Q_INVOKABLE QString path(const QString &name) const;
    // True when the SVG root is fill="currentColor" (solid transport icons).
    Q_INVOKABLE bool isFilled(const QString &name) const;
    Q_INVOKABLE bool has(const QString &name) const { return m_paths.contains(name); }
    Q_INVOKABLE QStringList names() const;

    int count() const { return int(m_paths.size()); }

    // Parses one SVG document; exposed for tests.
    static bool parseSvg(const QByteArray &svg, QString *pathData, bool *filled);

private:
    void load(const QString &dir);
    QHash<QString, QString> m_paths;
    QHash<QString, bool> m_filled;
};
