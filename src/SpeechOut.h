#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

// Reads Buddy's report aloud, for when the work was started by voice and you are
// not looking at the screen. Off until switched on in Settings.
//
// It speaks a short summary (wake::spokenSummary: no code, links or Markdown,
// a couple of sentences), never the whole reply. The text goes to the speech
// program on its standard input, never into a command line, so nothing in a
// reply can be run as a command. Windows uses its built-in voice
// (System.Speech through PowerShell); elsewhere espeak-ng/espeak when installed.
class SpeechOut : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY configChanged)
    // A voice exists on this computer.
    Q_PROPERTY(bool available READ available NOTIFY configChanged)
    Q_PROPERTY(bool speaking READ speaking NOTIFY speakingChanged)

public:
    explicit SpeechOut(QObject *parent = nullptr);
    ~SpeechOut() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    bool available() const { return !m_program.isEmpty(); }
    bool speaking() const { return m_proc.state() != QProcess::NotRunning; }

    // The program that reads text from stdin. Tests point it at something harmless.
    void setCommand(const QString &program, const QStringList &args);
    static void defaultCommand(QString *program, QStringList *args);

    // Say a reply (summarised). Does nothing when off or unavailable. A new one replaces the old.
    Q_INVOKABLE void speakReport(const QString &reply);
    // Say exactly this short line ("I need your approval").
    Q_INVOKABLE void say(const QString &line);
    Q_INVOKABLE void stop();

signals:
    void configChanged();
    void speakingChanged();

private:
    void start(const QString &text);

    QProcess m_proc;
    QString m_program;
    QStringList m_args;
    bool m_enabled = false;
};
