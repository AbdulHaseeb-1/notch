#include "Transcriber.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

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
