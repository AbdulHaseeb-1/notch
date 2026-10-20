#include "Transcriber.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QVariantMap>

#include "Moonshine.h"

namespace {

const QString kBundledVocab = QStringLiteral(":/qt/qml/noch/resources/moonshine/vocab.json");

class MoonshineTranscriber final : public Transcriber
{
public:
    explicit MoonshineTranscriber(const QString &folder) : m_folder(folder) {}

    QString name() const override { return QStringLiteral("Moonshine Tiny"); }

    QString problem() const override
    {
        if (modelDir().isEmpty())
            return QObject::tr("Voice input needs the Moonshine Tiny model (about 28 MB, not yet on this computer). "
                               "Build Noch with internet access once, or put encoder.onnx and decoder.onnx in %1.")
                .arg(QDir::toNativeSeparators(m_folder));
        if (vocabPath().isEmpty())
            return QObject::tr("The Moonshine vocabulary (vocab.json) is missing.");
        return QString();
    }

    QString prepare() override
    {
        if (const QString p = problem(); !p.isEmpty())
            return p;
        QString why;
        const QString appDir = QCoreApplication::applicationDirPath();
        if (!ortrt::load({ m_folder, appDir, appDir + QStringLiteral("/wakeword") }, &why))
            return why;
        const QString dir = modelDir();
        const QByteArray encoder = slurp(dir + QStringLiteral("/encoder.onnx"));
        const QByteArray decoder = slurp(dir + QStringLiteral("/decoder.onnx"));
        const QByteArray vocab = slurp(vocabPath());
        if (encoder.isEmpty() || decoder.isEmpty() || vocab.isEmpty())
            return QObject::tr("The Moonshine model files could not be read.");
        if (!m_model.load(encoder, decoder, vocab, &why))
            return why;
        return QString();
    }

    QString transcribe(const QByteArray &pcm16, QString *error) override
    {
        if (!m_model.loaded()) {
            if (const QString why = prepare(); !why.isEmpty()) {
                *error = why;
                return QString();
            }
        }
        return m_model.transcribe(pcm16, error);
    }

    QVariantList modelFiles() const override
    {
        // What settings shows: each needed file, whether it is there, and
        // where it was (or should be) found. encoder + decoder may sit in
        // the user folder or next to the executable; the vocabulary may
        // also come from the bundled resources.
        QVariantList out;
        auto describe = [&out](const QString &name, const QString &path) {
            const QFileInfo fi(path);
            out.append(QVariantMap{
                { QStringLiteral("name"), name },
                { QStringLiteral("path"), path },
                { QStringLiteral("found"), !path.isEmpty() && fi.exists() },
                { QStringLiteral("bytes"), qint64(!path.isEmpty() && fi.exists() ? fi.size() : 0) },
            });
        };
        const QString dir = modelDir();
        // When nothing is installed, point at the first folder the engine
        // looks in (the env override in tests, else the user's folder).
        const QString base = dir.isEmpty() ? searchDirs().value(0) : dir;
        describe(QStringLiteral("encoder.onnx"), base + QStringLiteral("/encoder.onnx"));
        describe(QStringLiteral("decoder.onnx"), base + QStringLiteral("/decoder.onnx"));
        describe(QStringLiteral("vocab.json"), vocabPath());
        return out;
    }

private:
    // The first folder that has both model files: NOCH_MOONSHINE_DIR (diagnostics,
    // tests), the user's folder, then models/moonshine-tiny next to noch.exe.
    QStringList searchDirs() const
    {
        QStringList dirs;
        if (const QString env = qEnvironmentVariable("NOCH_MOONSHINE_DIR"); !env.isEmpty())
            dirs << env;
        dirs << m_folder << QCoreApplication::applicationDirPath() + QStringLiteral("/models/moonshine-tiny");
        return dirs;
    }
    QString modelDir() const
    {
        for (const QString &dir : searchDirs())
            if (QFileInfo::exists(dir + QStringLiteral("/encoder.onnx")) && QFileInfo::exists(dir + QStringLiteral("/decoder.onnx")))
                return dir;
        return QString();
    }
    QString vocabPath() const
    {
        const QString dir = modelDir();
        if (!dir.isEmpty() && QFileInfo::exists(dir + QStringLiteral("/vocab.json")))
            return dir + QStringLiteral("/vocab.json");
        return QFile::exists(kBundledVocab) ? kBundledVocab : QString();
    }
    static QByteArray slurp(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    QString m_folder;
    Moonshine m_model;
};

} // namespace

std::unique_ptr<Transcriber> makeMoonshineTranscriber(const QString &folder)
{
    return std::make_unique<MoonshineTranscriber>(folder);
}

QString defaultMoonshineFolder()
{
    return QDir(QFileInfo(QSettings().fileName()).absolutePath()).filePath(QStringLiteral("models/moonshine-tiny"));
}
