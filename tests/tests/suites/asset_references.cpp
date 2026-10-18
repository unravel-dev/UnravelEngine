/*
 * Validation suite for the reference scan a deploy uses to find what the game's scenes need.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "asset references"
 *
 * This pins:
 *   - quoted uuids are found under any key, in order, once each; nil and malformed ones are not;
 *   - quoted protocol paths are found in single and double quotes, other protocols and unquoted
 *     mentions are not;
 *   - UI documents and style sheets yield their href / src attributes, src properties, url() and
 *     image() arguments and @import targets, without fragments, inline data or web addresses; spaces
 *     around '=' and ':' are allowed, and data-src is not src;
 *   - C# verbatim and interpolated strings are found, and an escaped quote does not end a string.
 */

#include "../tests.h"

#include <engine/assets/impl/asset_references.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto contains(const std::vector<std::string>& values, const std::string& value) -> bool
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

void test_quoted_uids()
{
    const std::string scene = R"({
 "uid": "d1982e72-e1b0-ae4e-a8a7-dba56e8967b3",
 "prefab_uid": "7b2a12a5-b8bc-7749-9b5e-f9c9d0a9f337",
 "document": "6e59541b-a348-6248-9685-aba84bc78076",
 "again": "d1982e72-e1b0-ae4e-a8a7-dba56e8967b3",
 "nil_uid": "00000000-0000-0000-0000-000000000000",
 "nil_text": "nil",
 "short": "d1982e72-e1b0-ae4e-a8a7-dba56e8967b",
 "long": "d1982e72-e1b0-ae4e-a8a7-dba56e8967b3x",
 "bad": "d1982e72xe1b0-ae4e-a8a7-dba56e8967b3"
})";
    const auto uids = unravel::asset_references::find_quoted_uids(scene);
    check(uids.size() == 3, "three distinct uids are found");
    check(uids.size() == 3 && uids[0].to_string() == "d1982e72-e1b0-ae4e-a8a7-dba56e8967b3", "first uid first");
    check(uids.size() == 3 && uids[1].to_string() == "7b2a12a5-b8bc-7749-9b5e-f9c9d0a9f337", "uid under any key");
    check(uids.size() == 3 && uids[2].to_string() == "6e59541b-a348-6248-9685-aba84bc78076", "document uid");
    check(unravel::asset_references::find_quoted_uids("").empty(), "empty text has none");
}

void test_quoted_protocol_paths()
{
    const std::string script = R"(
        Assets.GetAsset<AudioClip>("app:/data/UI/Sounds/click.mp3");
        Scene.LoadScene('app:/data/Levels/Two');
        var engine = "engine:/data/fonts/a.ttf";
        // app:/data/not/quoted.png
        var again = "app:/data/UI/Sounds/click.mp3";
        var folder = "app:/data/Levels/" + name;
        var broken = "app:/data/x
        ";
    )";
    const auto paths = unravel::asset_references::find_quoted_protocol_paths(script, "app");
    check(paths.size() == 3, "three distinct app paths are found");
    check(contains(paths, "app:/data/UI/Sounds/click.mp3"), "double quoted path");
    check(contains(paths, "app:/data/Levels/Two"), "single quoted, extensionless path");
    check(contains(paths, "app:/data/Levels/"), "folder prefix of a built path");
    check(!contains(paths, "app:/data/not/quoted.png"), "unquoted mention skipped");
    check(!contains(paths, "engine:/data/fonts/a.ttf"), "other protocol skipped");
    const auto engine_paths = unravel::asset_references::find_quoted_protocol_paths(script, "engine");
    check(engine_paths.size() == 1 && engine_paths[0] == "engine:/data/fonts/a.ttf", "engine path by its protocol");
}

void test_ui_linked_paths()
{
    const std::string document = R"(<rml><head>
        <link type="text/rcss" href="engine:/data/ui/rml.rcss" />
        <link type="text/rcss" href='Common.rcss'/>
        </head><body>
        <img src="images/logo.png"/>
        <a href="#top">top</a>
        <img src="data:image/png;base64,AAAA"/>
        <a href="https://example.com">web</a>
        </body></rml>)";
    const auto document_paths = unravel::asset_references::find_ui_linked_paths(document);
    check(contains(document_paths, "engine:/data/ui/rml.rcss"), "protocol link");
    check(contains(document_paths, "Common.rcss"), "relative link in single quotes");
    check(contains(document_paths, "images/logo.png"), "image source");
    check(!contains(document_paths, "#top"), "fragment skipped");
    check(document_paths.size() == 3, "inline data and web addresses skipped");

    const std::string style = R"(
        @import "Base.rcss";
        @import url(Theme.rcss);
        @font-face { font-family: Orbitron; src: "fonts/Orbitron.ttf"; }
        @spritesheet icons { src: icons.tga; play: 0px 0px 16px 16px; }
        body { decorator: image( panel.png flip-horizontal ); }
        div { decorator: image(icons-play); background: url("bg.png"); }
        p { decorator: shader("creation"); }
    )";
    const auto style_paths = unravel::asset_references::find_ui_linked_paths(style);
    check(contains(style_paths, "Base.rcss"), "@import string");
    check(contains(style_paths, "Theme.rcss"), "@import url()");
    check(contains(style_paths, "fonts/Orbitron.ttf"), "font face src");
    check(contains(style_paths, "icons.tga"), "sprite sheet src");
    check(contains(style_paths, "panel.png"), "image() first argument");
    check(contains(style_paths, "bg.png"), "quoted url()");
    check(contains(style_paths, "icons-play"), "sprite name kept for the caller to resolve");
    check(!contains(style_paths, "creation"), "shader decorator skipped");
}

void test_scan_edge_cases()
{
    const std::string script = R"(
        var verbatim = @"app:/data/verbatim.png";
        var interpolated = $"app:/data/levels/{index}.spfb";
        var escaped = "app:/data/a\"b.png";
    )";
    const auto paths = unravel::asset_references::find_quoted_protocol_paths(script, "app");
    check(contains(paths, "app:/data/verbatim.png"), "a C# verbatim string is found");
    check(contains(paths, "app:/data/levels/{index}.spfb"), "an interpolated string is found as written");
    check(contains(paths, R"(app:/data/a\"b.png)"), "an escaped quote does not end the string");

    const std::string document = R"(<link href = "Spaced.rcss"/><img data-src="lazy.png"/><img src ="Late.png"/>)";
    const auto document_paths = unravel::asset_references::find_ui_linked_paths(document);
    check(contains(document_paths, "Spaced.rcss"), "spaces around '=' are allowed");
    check(contains(document_paths, "Late.png"), "a space before '=' is allowed");
    check(!contains(document_paths, "lazy.png"), "data-src is not src");
    const std::string style = "@font-face { src : \"fonts/Spaced.ttf\"; }";
    check(contains(unravel::asset_references::find_ui_linked_paths(style), "fonts/Spaced.ttf"),
          "a style property with a space before ':' is found");
}

auto run_asset_references_suite(rtti::context&) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_quoted_uids();
    test_quoted_protocol_paths();
    test_ui_linked_paths();
    test_scan_edge_cases();
    std::printf("asset references: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("asset references", run_asset_references_suite)
