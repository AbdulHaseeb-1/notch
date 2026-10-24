#include "BuiltinOAuth.h"

#include <QProcessEnvironment>

namespace builtinoauth {

namespace {
// Noch's own OAuth client IDs. Public identifiers, safe to ship in source (see the
// header). Empty until the apps are registered once by the developer.
constexpr const char *kGoogleClientId = "";
constexpr const char *kGithubClientId = "";

QString pick(const char *envName, const char *builtIn)
{
    const QString fromEnv = QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(envName)).trimmed();
    return fromEnv.isEmpty() ? QString::fromLatin1(builtIn) : fromEnv;
}
} // namespace

QString googleClientId()
{
    return pick("NOCH_GOOGLE_CLIENT_ID", kGoogleClientId);
}

QString githubClientId()
{
    return pick("NOCH_GITHUB_CLIENT_ID", kGithubClientId);
}

} // namespace builtinoauth
