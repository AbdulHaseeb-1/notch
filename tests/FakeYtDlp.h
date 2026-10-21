#pragma once
// A stand-in for yt-dlp that is the test program itself: YtDlp is pointed at the test
// executable, which (when NOCH_FAKE_YTDLP is set) behaves like yt-dlp for one call. It
// records the arguments it was given and answers with what the test put in the environment.
//
//   NOCH_FAKE_ARGS   file the arguments are appended to (one per line, one call per block)
//   NOCH_FAKE_JSON   file whose content is printed as the answer
//   NOCH_FAKE_EXIT   exit code (and NOCH_FAKE_STDERR is printed to stderr)
//   NOCH_FAKE_SLEEP  milliseconds to hang before answering (for the timeout)
#include <QByteArray>
#include <QFile>
#include <QStringList>
#include <QThread>

#include <cstdio>

inline bool fakeYtDlpRequested()
{
    return qEnvironmentVariableIsSet("NOCH_FAKE_YTDLP");
}

inline int runFakeYtDlp(int argc, char **argv)
{
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    const QString argsPath = qEnvironmentVariable("NOCH_FAKE_ARGS");
    if (!argsPath.isEmpty()) {
        QFile f(argsPath);
        if (f.open(QIODevice::Append | QIODevice::Text))
            f.write(args.join(QLatin1Char('\n')).toUtf8() + "\n--call--\n");
    }
    if (args.contains(QLatin1String("--version"))) {
        std::fputs("2099.01.01\n", stdout);
        return 0;
    }
    if (args.contains(QLatin1String("-U"))) {
        std::fputs("yt-dlp is up to date (2099.01.01)\n", stdout);
        return 0;
    }
    const int sleepMs = qEnvironmentVariableIntValue("NOCH_FAKE_SLEEP");
    if (sleepMs > 0)
        QThread::msleep(unsigned(sleepMs));
    const int code = qEnvironmentVariableIntValue("NOCH_FAKE_EXIT");
    if (code != 0) {
        const QByteArray err = qgetenv("NOCH_FAKE_STDERR");
        std::fwrite(err.constData(), 1, size_t(err.size()), stderr);
        return code;
    }
    QFile json(qEnvironmentVariable("NOCH_FAKE_JSON"));
    if (json.open(QIODevice::ReadOnly)) {
        const QByteArray body = json.readAll();
        std::fwrite(body.constData(), 1, size_t(body.size()), stdout);
    }
    return 0;
}
