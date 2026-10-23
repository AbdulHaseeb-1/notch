// OAuth for a desktop app: PKCE, the loopback redirect, the code exchange and refresh,
// against a fake sign-in service. The "browser" is the test itself.
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "FakeHttp.h"
#include "OAuthClient.h"

class TstOAuth : public QObject
{
    Q_OBJECT
    QNetworkAccessManager nam;
    FakeHttpServer token;
    QUrl authUrl;   // what the client asked the browser to open

    OAuthClient::Config config() const {
        OAuthClient::Config c;
        c.authUrl = QUrl("https://accounts.example.com/auth");
        c.tokenUrl = QUrl(token.base() + "/token");
        c.clientId = "my-client.apps.example.com";
        c.scopes = { "scope.a", "scope.b" };
        c.extraAuthParams = { { "access_type", "offline" } };
        return c;
    }
    // The browser: remembers the URL, then (after the event loop turns) follows the redirect.
    OAuthClient::Opener browser() {
        return [this](const QUrl &url) {
            authUrl = url;
            const QUrlQuery q(url);
            const QString redirect = q.queryItemValue("redirect_uri", QUrl::FullyDecoded);
            const QString state = q.queryItemValue("state");
            QTimer::singleShot(0, this, [this, redirect, state] {
                QNetworkReply *r = nam.get(QNetworkRequest(QUrl(redirect + "?code=the-code&state=" + state)));
                connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
            });
            return true;
        };
    }

private slots:
    void initTestCase() { QVERIFY(token.start()); }
    void init() { token.requests.clear(); authUrl.clear(); }

    void pkceMatchesTheRfcExample() {
        // RFC 7636 appendix B.
        QCOMPARE(oauth::pkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
                 QStringLiteral("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
        const QString a = oauth::randomUrlSafe(48), b = oauth::randomUrlSafe(48);
        QVERIFY(a != b);
        QVERIFY(a.size() >= 43);
        QVERIFY(!a.contains('=') && !a.contains('+') && !a.contains('/'));
    }

    void authUrlCarriesTheRequiredParameters() {
        oauth::AuthRequest r;
        r.endpoint = QUrl("https://accounts.example.com/auth");
        r.clientId = "cid";
        r.redirectUri = "http://127.0.0.1:5555/oauth/callback";
        r.scopes = { "s1", "s2" };
        r.state = "st";
        r.challenge = "ch";
        r.extra = { { "access_type", "offline" } };
        const QUrlQuery q(oauth::buildAuthUrl(r));
        QCOMPARE(q.queryItemValue("response_type"), QStringLiteral("code"));
        QCOMPARE(q.queryItemValue("client_id"), QStringLiteral("cid"));
        QCOMPARE(q.queryItemValue("code_challenge_method"), QStringLiteral("S256"));
        QCOMPARE(q.queryItemValue("code_challenge"), QStringLiteral("ch"));
        QCOMPARE(q.queryItemValue("scope", QUrl::FullyDecoded), QStringLiteral("s1 s2"));
        QCOMPARE(q.queryItemValue("access_type"), QStringLiteral("offline"));
    }

    void tokenResponseParsing() {
        QString err;
        oauth::Tokens prev;
        prev.refresh = "old-refresh";
        const oauth::Tokens t = oauth::parseTokenResponse(R"({"access_token":"a1","expires_in":120,"scope":"x"})", prev, 1000, &err);
        QVERIFY(t.valid());
        QCOMPARE(t.refresh, QStringLiteral("old-refresh"));   // a refresh answer does not repeat it
        QCOMPARE(t.expiresAtMs, qint64(1000 + 120000));
        QVERIFY(t.expiresSoon(1000 + 119000));
        QVERIFY(!t.expiresSoon(1000));
        const oauth::Tokens bad = oauth::parseTokenResponse(R"({"error":"invalid_grant","error_description":"Token revoked"})", {}, 0, &err);
        QVERIFY(!bad.valid());
        QCOMPARE(err, QStringLiteral("Token revoked"));
        QCOMPARE(oauth::errorCode(R"({"error":"invalid_grant"})"), QStringLiteral("invalid_grant"));
    }

    void signInRunsThroughTheBrowserAndExchangesTheCode() {
        QString challenge;
        token.handler = [&](const HttpRequest &r) {
            const QUrlQuery f(QString::fromUtf8(r.body));
            // The verifier proves the exchange comes from whoever started the flow.
            const bool pkceOk = oauth::pkceChallenge(f.queryItemValue("code_verifier")) == challenge;
            if (f.queryItemValue("grant_type") != "authorization_code" || f.queryItemValue("code") != "the-code" || !pkceOk)
                return HttpResponse::json(R"({"error":"invalid_grant"})", 400);
            return HttpResponse::json(R"({"access_token":"at-1","refresh_token":"rt-1","expires_in":3600,"scope":"scope.a scope.b"})");
        };
        OAuthClient c(&nam);
        c.setConfig(config());
        const OAuthClient::Opener real = browser();
        c.setOpener([&](const QUrl &url) {
            challenge = QUrlQuery(url).queryItemValue("code_challenge");
            return real(url);
        });
        QSignalSpy done(&c, &OAuthClient::finished);
        c.start();
        QVERIFY(c.busy());
        QVERIFY(done.wait(5000));
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(2).toString()));
        const auto t = done.first().at(1).value<oauth::Tokens>();
        QCOMPARE(t.access, QStringLiteral("at-1"));
        QCOMPARE(t.refresh, QStringLiteral("rt-1"));
        QVERIFY(!c.busy());   // the listener closed
        // The redirect is on this computer's loopback address.
        const QString redirect = QUrlQuery(authUrl).queryItemValue("redirect_uri", QUrl::FullyDecoded);
        QVERIFY(redirect.startsWith("http://127.0.0.1:"));
        QCOMPARE(QUrlQuery(authUrl).queryItemValue("code_challenge_method"), QStringLiteral("S256"));
    }

    void clientSecretIsSentOnlyWhenGiven() {
        token.handler = [&](const HttpRequest &) { return HttpResponse::json(R"({"access_token":"a","refresh_token":"r"})"); };
        OAuthClient c(&nam);
        OAuthClient::Config cfg = config();
        c.setConfig(cfg);
        c.setOpener(browser());
        QSignalSpy done(&c, &OAuthClient::finished);
        c.start();
        QVERIFY(done.wait(5000));
        QVERIFY(!QString::fromUtf8(token.requests.last().body).contains("client_secret"));
        token.requests.clear();
        cfg.clientSecret = "GOCSPX-test";
        c.setConfig(cfg);
        done.clear();
        c.start();
        QVERIFY(done.wait(5000));
        QVERIFY(QString::fromUtf8(token.requests.last().body).contains("client_secret=GOCSPX-test"));
    }

    void aForgedStateIsIgnoredAndTheRealAnswerStillWorks() {
        token.handler = [&](const HttpRequest &) { return HttpResponse::json(R"({"access_token":"a","refresh_token":"r"})"); };
        OAuthClient c(&nam);
        c.setConfig(config());
        QString redirect, state;
        c.setOpener([&](const QUrl &url) {
            redirect = QUrlQuery(url).queryItemValue("redirect_uri", QUrl::FullyDecoded);
            state = QUrlQuery(url).queryItemValue("state");
            return true;
        });
        QSignalSpy done(&c, &OAuthClient::finished);
        c.start();
        // Another page on this computer tries to complete the sign-in with its own code.
        QNetworkReply *bad = nam.get(QNetworkRequest(QUrl(redirect + "?code=evil&state=forged")));
        QSignalSpy badDone(bad, &QNetworkReply::finished);
        QVERIFY(badDone.wait(5000));
        QCOMPARE(bad->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 400);
        bad->deleteLater();
        QCOMPARE(done.size(), 0);
        QVERIFY(token.requests.isEmpty());
        QVERIFY(c.busy());
        // Unknown paths (favicon) change nothing either.
        QNetworkReply *fav = nam.get(QNetworkRequest(QUrl(redirect.left(redirect.indexOf("/oauth")) + "/favicon.ico")));
        QSignalSpy favDone(fav, &QNetworkReply::finished);
        QVERIFY(favDone.wait(5000));
        fav->deleteLater();
        QVERIFY(c.busy());
        // The real answer.
        QNetworkReply *ok = nam.get(QNetworkRequest(QUrl(redirect + "?code=the-code&state=" + state)));
        connect(ok, &QNetworkReply::finished, ok, &QObject::deleteLater);
        QVERIFY(done.wait(5000));
        QVERIFY(done.first().at(0).toBool());
    }

    void declinedConsentFailsCleanly() {
        OAuthClient c(&nam);
        c.setConfig(config());
        c.setOpener([this](const QUrl &url) {
            const QString redirect = QUrlQuery(url).queryItemValue("redirect_uri", QUrl::FullyDecoded);
            const QString state = QUrlQuery(url).queryItemValue("state");
            QTimer::singleShot(0, this, [this, redirect, state] {
                QNetworkReply *r = nam.get(QNetworkRequest(QUrl(redirect + "?error=access_denied&state=" + state)));
                connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
            });
            return true;
        });
        QSignalSpy done(&c, &OAuthClient::finished);
        c.start();
        QVERIFY(done.wait(5000));
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY(done.first().at(2).toString().contains("declined"));
        QVERIFY(token.requests.isEmpty());
        QVERIFY(!c.busy());
    }

    void exchangeFailureIsReported() {
        token.handler = [&](const HttpRequest &) {
            return HttpResponse::json(R"({"error":"invalid_client","error_description":"The OAuth client was not found."})", 401);
        };
        OAuthClient c(&nam);
        c.setConfig(config());
        c.setOpener(browser());
        QSignalSpy done(&c, &OAuthClient::finished);
        c.start();
        QVERIFY(done.wait(5000));
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY(done.first().at(2).toString().contains("OAuth client was not found"));
    }

    void timeoutAndCancelCloseTheListener() {
        OAuthClient c(&nam);
        c.setConfig(config());
        c.signInTimeoutMs = 150;
        c.setOpener([](const QUrl &) { return true; });
        QSignalSpy done(&c, &OAuthClient::finished);
        c.start();
        QVERIFY(c.busy());
        QVERIFY(done.wait(3000));
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY(done.first().at(2).toString().contains("timed out"));
        QVERIFY(!c.busy());

        done.clear();
        c.signInTimeoutMs = 60000;
        c.start();
        QVERIFY(c.busy());
        c.cancel();
        QVERIFY(!c.busy());
        QTest::qWait(50);
        QCOMPARE(done.size(), 0);   // cancelled: no answer
    }

    void missingClientIdAndUnopenableBrowser() {
        OAuthClient c(&nam);
        OAuthClient::Config cfg = config();
        cfg.clientId.clear();
        c.setConfig(cfg);
        QSignalSpy done(&c, &OAuthClient::finished);
        c.start();
        QVERIFY(done.wait(3000));
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY(done.first().at(2).toString().contains("client ID"));

        done.clear();
        c.setConfig(config());
        c.setOpener([](const QUrl &) { return false; });
        c.start();
        QVERIFY(done.wait(3000));
        QVERIFY(done.first().at(2).toString().contains("browser"));
        QVERIFY(!c.busy());
    }

    void refreshKeepsTheRefreshTokenAndFlagsRevocation() {
        OAuthClient c(&nam);
        c.setConfig(config());
        oauth::Tokens old;
        old.access = "old";
        old.refresh = "rt-1";
        token.handler = [&](const HttpRequest &r) {
            const QUrlQuery f(QString::fromUtf8(r.body));
            if (f.queryItemValue("refresh_token") == "rt-1")
                return HttpResponse::json(R"({"access_token":"at-2","expires_in":3600})");
            return HttpResponse::json(R"({"error":"invalid_grant","error_description":"Token has been expired or revoked."})", 400);
        };
        bool ok = false, permanent = true, called = false;
        oauth::Tokens got;
        QString err;
        c.refresh(old, [&](bool o, const oauth::Tokens &t, const QString &e, bool p) { ok = o; got = t; err = e; permanent = p; called = true; });
        QTRY_VERIFY(called);
        QVERIFY(ok);
        QVERIFY(!permanent);
        QCOMPARE(got.access, QStringLiteral("at-2"));
        QCOMPARE(got.refresh, QStringLiteral("rt-1"));
        QVERIFY(QString::fromUtf8(token.requests.last().body).contains("grant_type=refresh_token"));

        called = false;
        oauth::Tokens revoked;
        revoked.refresh = "rt-dead";
        c.refresh(revoked, [&](bool o, const oauth::Tokens &, const QString &e, bool p) { ok = o; err = e; permanent = p; called = true; });
        QTRY_VERIFY(called);
        QVERIFY(!ok);
        QVERIFY(permanent);
        QVERIFY(err.contains("revoked"));

        // A server outage is not permanent: the sign-in stays.
        token.handler = [&](const HttpRequest &) { return HttpResponse::text(503, "down"); };
        called = false;
        c.refresh(old, [&](bool o, const oauth::Tokens &, const QString &, bool p) { ok = o; permanent = p; called = true; });
        QTRY_VERIFY(called);
        QVERIFY(!ok);
        QVERIFY(!permanent);
    }
};

QTEST_MAIN(TstOAuth)
#include "tst_oauth.moc"
