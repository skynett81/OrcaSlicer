#include <catch2/catch_all.hpp>

#include "libslic3r/ForgeAccount.hpp"

using namespace Slic3r;

TEST_CASE("interpret_login_response maps the auth contract", "[ForgeAccount]")
{
    REQUIRE(interpret_login_response(200, R"({"ok":true})").status == ForgeLoginStatus::Success);

    auto totp = interpret_login_response(401, R"({"error":"TOTP code required","totpRequired":true})");
    REQUIRE(totp.status == ForgeLoginStatus::TotpRequired);
    REQUIRE(totp.error == "TOTP code required");

    auto bad = interpret_login_response(401, R"({"error":"Invalid credentials"})");
    REQUIRE(bad.status == ForgeLoginStatus::BadCredentials);
    REQUIRE(bad.error == "Invalid credentials");

    REQUIRE(interpret_login_response(429, R"({"error":"Too many login attempts"})").status == ForgeLoginStatus::RateLimited);
    REQUIRE(interpret_login_response(500, "boom").status == ForgeLoginStatus::ServerError);
    REQUIRE(interpret_login_response(418, "").status == ForgeLoginStatus::Unknown);
}

TEST_CASE("interpret_login_response tolerates non-JSON bodies", "[ForgeAccount]")
{
    REQUIRE(interpret_login_response(200, "not json").status == ForgeLoginStatus::Success);
    auto bad = interpret_login_response(401, "<html>nope</html>");
    REQUIRE(bad.status == ForgeLoginStatus::BadCredentials);
}

TEST_CASE("extract_session_cookie pulls the token", "[ForgeAccount]")
{
    REQUIRE(extract_session_cookie("bambu_session=abc123; Path=/; HttpOnly; SameSite=Strict") == "abc123");
    REQUIRE(extract_session_cookie("Path=/; bambu_session=XYZ.tok-9; Secure") == "XYZ.tok-9");
    REQUIRE(extract_session_cookie("bambu_session=onlytoken") == "onlytoken");
    REQUIRE(extract_session_cookie("other_cookie=foo; Path=/").empty());
    REQUIRE(extract_session_cookie("").empty());
}
