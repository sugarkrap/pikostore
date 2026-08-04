/*
 * Host-side tests for repostate.h -- the Packages-tab repo list.
 *
 * Same reasoning as tests/romstate-test.cxx: no FLTK, no X, no
 * /etc/zaurus needed, so this is the part of the change worth exercising
 * before it ever reaches the device.
 *
 *   g++ -O2 -Wall -Wextra -o repostate-test tests/repostate-test.cxx && ./repostate-test
 */

#include "../repostate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

using namespace repostate;

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL: %s\n", what);
    }
}

static void check_str(const std::string &got, const std::string &want,
                      const char *what)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL: %s\n        got  [%s]\n        want [%s]\n",
               what, got.c_str(), want.c_str());
    }
}

static std::string tmpdir;

static std::string write_file(const char *name, const char *content)
{
    std::string path = tmpdir + "/" + name;
    FILE *f = fopen(path.c_str(), "w");
    if (!f) { perror("fopen"); exit(2); }
    fputs(content, f);
    fclose(f);
    return path;
}

/* ---------------------------------------------------------------- */

static void test_missing_file_returns_default()
{
    printf("read_repos: missing file\n");
    std::vector<std::string> r = read_repos("/nonexistent/nope/pikostore-repos");
    check(r.size() == 1, "exactly one entry");
    if (r.size() == 1)
        check_str(r[0], DEFAULT_REPO, "default repo");
}

static void test_normal_file()
{
    printf("read_repos: a normal file\n");
    std::string p = write_file("repos1",
        "https://example.com/a/manifest.yaml\n"
        "https://example.com/b/manifest.yaml\n");

    std::vector<std::string> r = read_repos(p.c_str());
    check(r.size() == 2, "two entries");
    if (r.size() == 2) {
        check_str(r[0], "https://example.com/a/manifest.yaml", "first");
        check_str(r[1], "https://example.com/b/manifest.yaml", "second");
    }
}

static void test_comments_and_blank_lines_skipped()
{
    printf("read_repos: comments and blank lines\n");
    std::string p = write_file("repos2",
        "# a comment\n"
        "\n"
        "https://example.com/only.yaml\n"
        "   \n"
        "# trailing comment\n");

    std::vector<std::string> r = read_repos(p.c_str());
    check(r.size() == 1, "one entry survives");
    if (r.size() == 1)
        check_str(r[0], "https://example.com/only.yaml", "surviving entry");
}

static void test_lines_are_trimmed()
{
    printf("read_repos: surrounding whitespace trimmed\n");
    std::string p = write_file("repos3", "   https://example.com/x.yaml   \r\n");
    std::vector<std::string> r = read_repos(p.c_str());
    check(r.size() == 1, "one entry");
    if (r.size() == 1)
        check_str(r[0], "https://example.com/x.yaml", "trimmed entry");
}

static void test_write_then_read_round_trips()
{
    printf("write_repos + read_repos: round trip\n");
    std::string p = tmpdir + "/repos4";
    std::vector<std::string> in;
    in.push_back("https://example.com/one.yaml");
    in.push_back("https://example.com/two.yaml");

    check(write_repos(p.c_str(), in), "write succeeds");

    std::vector<std::string> out = read_repos(p.c_str());
    check(out.size() == 2, "two entries read back");
    if (out.size() == 2) {
        check_str(out[0], in[0], "first round-trips");
        check_str(out[1], in[1], "second round-trips");
    }
}

static void test_write_empty_list_then_read_gives_empty()
{
    /* An empty file exists but has no entries -- distinct from a MISSING
     * file. read_repos must not silently reintroduce the default just
     * because there happen to be zero lines: the user deliberately
     * deleted everything. */
    printf("write_repos: empty list writes an empty (not missing) file\n");
    std::string p = tmpdir + "/repos5";
    std::vector<std::string> empty;
    check(write_repos(p.c_str(), empty), "write succeeds");

    std::vector<std::string> out = read_repos(p.c_str());
    check(out.size() == 0, "reads back empty, not the default");
}

static void test_write_to_unwritable_path_fails()
{
    printf("write_repos: unwritable path reports failure\n");
    std::vector<std::string> repos;
    repos.push_back("https://example.com/x.yaml");
    check(!write_repos("/nonexistent-dir/whatever/pikostore-repos", repos),
          "write to a missing directory fails");
}

static void test_add_repo_appends()
{
    printf("add_repo: appends a new url\n");
    std::vector<std::string> repos;
    repos.push_back("https://example.com/a.yaml");

    bool changed = add_repo(repos, "https://example.com/b.yaml");
    check(changed, "reports changed");
    check(repos.size() == 2, "two entries now");
    if (repos.size() == 2)
        check_str(repos[1], "https://example.com/b.yaml", "appended entry");
}

static void test_add_repo_rejects_duplicate()
{
    printf("add_repo: exact duplicate is a no-op\n");
    std::vector<std::string> repos;
    repos.push_back("https://example.com/a.yaml");

    bool changed = add_repo(repos, "https://example.com/a.yaml");
    check(!changed, "reports unchanged");
    check(repos.size() == 1, "still one entry");
}

static void test_add_repo_rejects_blank()
{
    printf("add_repo: blank/whitespace-only is a no-op\n");
    std::vector<std::string> repos;

    check(!add_repo(repos, ""), "empty string rejected");
    check(!add_repo(repos, "   \t  "), "whitespace-only rejected");
    check(repos.size() == 0, "list still empty");
}

static void test_add_repo_trims_before_storing()
{
    printf("add_repo: stores the trimmed url\n");
    std::vector<std::string> repos;
    add_repo(repos, "  https://example.com/pad.yaml  ");
    check(repos.size() == 1, "one entry");
    if (repos.size() == 1)
        check_str(repos[0], "https://example.com/pad.yaml", "trimmed before storing");
}

/* ---------------------------------------------------------------- */

int main()
{
    char tmpl[] = "/tmp/repostate-test.XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    tmpdir = tmpl;

    test_missing_file_returns_default();
    test_normal_file();
    test_comments_and_blank_lines_skipped();
    test_lines_are_trimmed();
    test_write_then_read_round_trips();
    test_write_empty_list_then_read_gives_empty();
    test_write_to_unwritable_path_fails();

    test_add_repo_appends();
    test_add_repo_rejects_duplicate();
    test_add_repo_rejects_blank();
    test_add_repo_trims_before_storing();

    /* Leave nothing behind; this runs in CI. */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmpdir.c_str());
    if (system(cmd) != 0)
        printf("  (warning: could not remove %s)\n", tmpdir.c_str());

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    if (failures) {
        printf("REPOSTATE-TEST: FAIL\n");
        return 1;
    }
    printf("REPOSTATE-TEST: PASS\n");
    return 0;
}
