#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>

#include <functional>

#include "OnlineTrack.h"

class QNetworkReply;
class QTimer;

// Finds songs on YouTube and gets a direct audio address for Noch's player, with no
// browser: it runs yt-dlp (https://github.com/yt-dlp/yt-dlp) with fixed arguments and
// reads its JSON. Nothing the model or a web page says is ever put into a command line
// except as one search term after "--".
//
// yt-dlp is found, in this order, at: the path set in Settings, tools/yt-dlp(.exe) next
// to noch.exe, tools/ in Noch's data folder, then the PATH. When there is none,
// install() downloads the official release from GitHub into the data folder (https only,
// checked against the SHA-256 that the release publishes) - on request, never by itself.
class YtDlp : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString configuredPath READ configuredPath WRITE setConfiguredPath NOTIFY changed)
    Q_PROPERTY(QString version READ version NOTIFY changed)
    // "idle" | "searching" | "installing" | "updating"
    Q_PROPERTY(QString state READ state NOTIFY changed)
    // The last thing worth telling the user: a failure, or "Installed 2026.08.19".
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY changed)
    Q_PROPERTY(QString toolsFolder READ toolsFolder CONSTANT)

public:
    using Done = std::function<void(const OnlineTrack &track, const QString &error)>;

    explicit YtDlp(QObject *parent = nullptr);
    ~YtDlp() override;

    bool available() const { return !m_path.isEmpty(); }
    QString path() const { return m_path; }
    QString configuredPath() const { return m_configured; }
    void setConfiguredPath(const QString &path);
    QString version() const { return m_version; }
    QString state() const { return m_state; }
    QString message() const { return m_message; }
    bool messageIsError() const { return m_messageIsError; }
    QString toolsFolder() const;

    // The first result for `query` (words, or a youtube.com / youtu.be address), with a
    // stream address. `done` runs once, on this thread, unless the object is destroyed.
    void search(const QString &query, Done done);
    // A fresh stream address for a page found earlier (the old one expires).
    void resolve(const QString &webUrl, Done done);

    Q_INVOKABLE void refresh();
    // Downloads yt-dlp into toolsFolder() and checks it. Does nothing if it is already running.
    Q_INVOKABLE void install();
    // yt-dlp's own self-update (the standalone program updates itself).
    Q_INVOKABLE void update();
    Q_INVOKABLE void openToolsFolder() const;

    // Tests: where install() downloads from, how long a search may take.
    void setReleaseBase(const QString &url) { m_releaseBase = url; }
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    // Tests: never look at the PATH or the folders next to the program.
    void setSearchPath(bool on) { m_searchPath = on; refresh(); }

    // ---- pure helpers (unit tested) ----------------------------------------------------------
    // Why this query cannot be used ("" if it can): empty, or an address that is not YouTube's.
    static QString queryProblem(const QString &query);
    // What yt-dlp is asked to fetch for a query: "ytsearch1:words" or the YouTube address.
    static QString target(const QString &query);
    // The complete argument list for one lookup. (yt-dlp's own choice of clients is left alone:
    // forcing the one that answers fastest gives stream addresses that Qt's player is refused,
    // with HTTP 403, because they are tied to that client's headers.)
    static QStringList lookupArguments(const QString &target);
    // The first usable result of yt-dlp's JSON (a single video, or a search "playlist").
    static OnlineTrack parse(const QByteArray &json, QString *error);
    // The SHA-256 (hex) listed for `file` in a SHA2-256SUMS text ("" if absent).
    static QString checksumFor(const QByteArray &sums, const QString &file);

signals:
    void changed();

private:
    void run(const QStringList &args, int timeoutMs, std::function<void(int exitCode, const QByteArray &out, const QByteArray &err, bool timedOut)> done);
    void lookup(const QString &target, Done done);
    void setState(const QString &state);
    void setMessage(const QString &message, bool isError);
    QString locate() const;
    void finishInstall(bool ok, const QString &message);
    void fetch(const QUrl &url, std::function<void(const QByteArray &body, const QString &error)> done);

    QString m_path;
    QString m_configured;
    QString m_version;
    QString m_state = QStringLiteral("idle");
    QString m_message;
    bool m_messageIsError = false;
    bool m_searchPath = true;
    int m_timeoutMs = 60000;
    QString m_releaseBase = QStringLiteral("https://github.com/yt-dlp/yt-dlp/releases/latest/download");
    QNetworkAccessManager m_nam;
    QList<QPointer<QProcess>> m_procs;
    QList<QPointer<QNetworkReply>> m_replies;
    int m_busy = 0;
};
