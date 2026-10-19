// Every shipped SVG icon must parse into Shape path data.
#include <QtTest>
#include <QDir>
#include "IconSet.h"

class TestIconSet : public QObject
{
    Q_OBJECT
private slots:
    void allIconsParse()
    {
        QDir dir(QStringLiteral(NOCH_ICON_DIR));
        const QStringList files = dir.entryList({ "*.svg" }, QDir::Files);
        QVERIFY(files.size() > 60);
        for (const QString &f : files) {
            QFile file(dir.filePath(f));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QString d;
            bool filled = true;
            QVERIFY2(IconSet::parseSvg(file.readAll(), &d, &filled), qPrintable(f));
            QVERIFY2(!d.isEmpty(), qPrintable(f));
            QVERIFY2(d.at(0) == QLatin1Char('M'), qPrintable(f));
        }
    }
    void rejectsRelativeStart()
    {
        QVERIFY(!IconSet::parseSvg("<svg><path d=\"m1 1 2 2\"/></svg>", nullptr, nullptr));
        QVERIFY(!IconSet::parseSvg("<svg></svg>", nullptr, nullptr));
        bool filled = false;
        QVERIFY(IconSet::parseSvg("<svg fill=\"currentColor\"><path d=\"M1 1h2\"/></svg>", nullptr, &filled));
        QVERIFY(filled);
    }
};

QTEST_GUILESS_MAIN(TestIconSet)
#include "tst_iconset.moc"
