#include "SpeechOut.h"

#include <QStandardPaths>

#include "WakeWord.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

SpeechOut::SpeechOut(QObject *parent)
    : QObject(parent)
{
    defaultCommand(&m_program, &m_args);
#ifdef Q_OS_WIN
    // PowerShell is a console program: do not flash a console window.
    m_proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
#endif
    connect(&m_proc, &QProcess::started, this, &SpeechOut::speakingChanged);
    connect(&m_proc, &QProcess::finished, this, &SpeechOut::speakingChanged);
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit speakingChanged();
    });
}

SpeechOut::~SpeechOut()
{
    stop();
}

void SpeechOut::defaultCommand(QString *program, QStringList *args)
{
    program->clear();
    args->clear();
#ifdef Q_OS_WIN
    *program = QStringLiteral("powershell.exe");
    *args = { QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
              QStringLiteral("[Console]::InputEncoding=[System.Text.Encoding]::UTF8;"
                             "Add-Type -AssemblyName System.Speech;"
                             "$t=[Console]::In.ReadToEnd();"
                             "$s=New-Object System.Speech.Synthesis.SpeechSynthesizer;"
                             "$s.Speak($t)") };
#else
    for (const char *name : { "espeak-ng", "espeak" }) {
        const QString found = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!found.isEmpty()) {
            *program = found;
            *args = { QStringLiteral("--stdin") };
            return;
        }
    }
#endif
}

void SpeechOut::setCommand(const QString &program, const QStringList &args)
{
    stop();
    m_program = program;
    m_args = args;
    emit configChanged();
}

void SpeechOut::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (!on)
        stop();
    emit configChanged();
}

void SpeechOut::speakReport(const QString &reply)
{
    const QString text = wake::spokenSummary(reply);
    if (!text.isEmpty())
        start(text);
}

void SpeechOut::say(const QString &line)
{
    const QString t = line.simplified().left(200);
    if (!t.isEmpty())
        start(t);
}

void SpeechOut::start(const QString &text)
{
    if (!m_enabled || m_program.isEmpty())
        return;
    stop();
    m_proc.setProgram(m_program);
    m_proc.setArguments(m_args);
    m_proc.start();
    emit speakingChanged();   // starting counts as speaking: the listener must not hear it
    // Written once and closed: the program reads until the end of its input.
    m_proc.write(text.toUtf8());
    m_proc.closeWriteChannel();
}

void SpeechOut::stop()
{
    if (m_proc.state() == QProcess::NotRunning)
        return;
    m_proc.kill();
    m_proc.waitForFinished(500);
    emit speakingChanged();
}
