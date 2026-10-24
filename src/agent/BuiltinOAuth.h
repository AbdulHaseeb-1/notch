// The OAuth client IDs Noch itself ships with, one per built-in app.
//
// Connecting an app must be one click for the user — press Connect, approve in the
// browser, done — so the client ID belongs to the app, never to a form in Settings.
// A client ID is a public identifier, not a credential:
//
//   * Google: the ID of a "Desktop app" OAuth client. Desktop clients are public
//     clients that cannot keep a secret, so sign-in is PKCE-only (RFC 7636) and
//     Noch never sends a client secret at all.
//   * GitHub: the ID of an OAuth App with "Enable Device Flow" ticked. The device
//     flow (RFC 8628) needs no client secret either; GitHub's docs say so outright.
//
// One-time developer step (not per user): register the two apps once and paste the
// two IDs into BuiltinOAuth.cpp — Google Cloud Console > Credentials > OAuth client
// ID (Desktop app, Gmail API enabled), and github.com > Settings > Developer
// settings > OAuth Apps > New OAuth App (device flow on). See docs/connectors.md.
// Until then the constants are empty and Connect says so plainly instead of opening
// a sign-in that cannot work.
//
// Developers can override either ID without recompiling (tests, trying a build
// before the IDs are baked in): NOCH_GOOGLE_CLIENT_ID / NOCH_GITHUB_CLIENT_ID in
// the environment win over the constants.
#pragma once

#include <QString>

namespace builtinoauth {

QString googleClientId();
QString githubClientId();

} // namespace builtinoauth
