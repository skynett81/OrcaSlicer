#ifndef slic3r_ForgeAccount_hpp_
#define slic3r_ForgeAccount_hpp_

#include <string>

namespace Slic3r {

// Pure interpretation of the 3DPrintForge Server auth contract
// (POST /api/auth/login). Kept free of any HTTP/GUI dependency so it is unit
// testable; the network call and the dialog live in the GUI layer.
//
// Server contract:
//   200 { ok: true } + Set-Cookie: bambu_session=...   -> logged in
//   401 { error, totpRequired: true }                  -> need a TOTP code
//   401 { error: "Invalid credentials" }               -> wrong user/password
//   429 { error }                                       -> rate limited
enum class ForgeLoginStatus { Success, TotpRequired, BadCredentials, RateLimited, ServerError, Unknown };

struct ForgeLoginOutcome
{
    ForgeLoginStatus status = ForgeLoginStatus::Unknown;
    std::string      error;            // human-readable message from the server, if any
};

// Map an HTTP status + JSON body to a login outcome. Never throws.
ForgeLoginOutcome interpret_login_response(int http_status, const std::string& body);

// Extract the bambu_session token value from a Set-Cookie header value.
// Returns "" when not present. Tolerant of attribute order and casing of the
// "Set-Cookie" wrapper (pass just the header value, e.g.
// "bambu_session=abc123; Path=/; HttpOnly").
std::string extract_session_cookie(const std::string& set_cookie_header);

} // namespace Slic3r

#endif
