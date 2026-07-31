/*
 * Host-side tests for romstate.h -- the manifest/history parsing and the
 * piko-update progress protocol.
 *
 * These run on the BUILD machine, with no FLTK, no X and no /etc/zaurus.
 * That is the point: this is the only part of pikostore that can be
 * exercised before it reaches the one spare Zaurus, so it is the part
 * worth making testable. Everything here writes its fixtures to a temp
 * directory and reads them back through the real parser.
 *
 *   g++ -O2 -Wall -Wextra -o romstate-test tests/romstate-test.cxx && ./romstate-test
 */

#include "../romstate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

using namespace romstate;

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

static void test_manifest_normal()
{
    printf("manifest: a normal generated file\n");
    std::string p = write_file("m1",
        "PIKO-ROM-MANIFEST 1\n"
        "version: r148\n"
        "built: 2026-07-31T21:04:00Z\n"
        "commit: 224c038\n"
        "\n"
        "Software Center arrives, with a system update tab.\n");

    Manifest m = read_manifest(p.c_str());
    check(m.found, "found");
    check_str(m.version, "r148", "version");
    check_str(m.built, "2026-07-31T21:04:00Z", "built");
    check_str(m.commit, "224c038", "commit");
    check_str(m.changelog,
              "Software Center arrives, with a system update tab.",
              "changelog");
}

static void test_manifest_missing_file()
{
    printf("manifest: file does not exist\n");
    Manifest m = read_manifest("/nonexistent/nope/manifest");
    check(!m.found, "not found");
    check(m.changelog.empty(), "no changelog");
}

static void test_manifest_no_version()
{
    printf("manifest: header without a version is not usable\n");
    std::string p = write_file("m2",
        "PIKO-ROM-MANIFEST 1\n"
        "built: 2026-07-31T21:04:00Z\n"
        "\n"
        "Some prose.\n");

    Manifest m = read_manifest(p.c_str());
    check(!m.found, "not found without version");
}

static void test_manifest_unknown_key_ignored()
{
    printf("manifest: unknown header keys are ignored, not fatal\n");
    std::string p = write_file("m3",
        "PIKO-ROM-MANIFEST 1\n"
        "version: r200\n"
        "flavour: experimental\n"      /* future field */
        "commit: deadbee\n"
        "\n"
        "Body.\n");

    Manifest m = read_manifest(p.c_str());
    check(m.found, "still found");
    check_str(m.version, "r200", "version survives unknown key");
    check_str(m.commit, "deadbee", "later known key still parsed");
}

static void test_manifest_multiline_changelog()
{
    printf("manifest: multi-line changelog with punctuation survives\n");
    std::string p = write_file("m4",
        "PIKO-ROM-MANIFEST 1\n"
        "version: r5\n"
        "\n"
        "First line: with a colon.\n"
        "Second line, and a blank one follows.\n"
        "\n"
        "Third paragraph still belongs to the body.\n");

    Manifest m = read_manifest(p.c_str());
    check(m.found, "found");
    /* A colon in the body must NOT be re-parsed as a header key. */
    check(m.changelog.find("First line: with a colon.") != std::string::npos,
          "colon in body preserved");
    check(m.changelog.find("Third paragraph") != std::string::npos,
          "text after an internal blank line preserved");
}

static void test_manifest_dirty_version()
{
    printf("manifest: dirty-tree marker is preserved verbatim\n");
    std::string p = write_file("m5",
        "PIKO-ROM-MANIFEST 1\nversion: r80+\n\nBody.\n");
    Manifest m = read_manifest(p.c_str());
    check_str(m.version, "r80+", "r80+ kept, + not stripped");
}

/* ---------------------------------------------------------------- */

static void test_history_order_and_skips()
{
    printf("history: newest first, malformed lines skipped\n");
    std::string p = write_file("h1",
        "r10|2026-01-01T00:00:00Z|first.tar\n"
        "this line is garbage\n"
        "r11|2026-02-01T00:00:00Z|second.tar\n"
        "\n"
        "|2026-03-01T00:00:00Z|no-version.tar\n"
        "r12|2026-03-01T00:00:00Z|third.tar\n");

    std::vector<HistoryRow> h = read_history(p.c_str());
    check(h.size() == 3, "three usable rows");
    if (h.size() == 3) {
        check_str(h[0].version, "r12", "newest first");
        check_str(h[1].version, "r11", "then r11");
        check_str(h[2].version, "r10", "oldest last");
        check_str(h[0].package, "third.tar", "package field");
        check_str(h[0].date, "2026-03-01T00:00:00Z", "date field");
    }
}

static void test_history_missing()
{
    printf("history: missing file is empty, not an error\n");
    std::vector<HistoryRow> h = read_history("/nonexistent/nope/history");
    check(h.empty(), "empty");
}

static void test_history_package_with_spaces()
{
    printf("history: a package name containing spaces survives\n");
    std::string p = write_file("h2",
        "r1|2026-01-01T00:00:00Z|my update file.tar\n");
    std::vector<HistoryRow> h = read_history(p.c_str());
    check(h.size() == 1, "one row");
    if (h.size() == 1)
        check_str(h[0].package, "my update file.tar", "spaces kept");
}

/* ---------------------------------------------------------------- */

static void test_progress_records()
{
    printf("progress: each record type parses\n");

    ProgressRecord t = parse_progress("TOTAL 33");
    check(t.kind == ProgressRecord::TOTAL && t.total == 33, "TOTAL");

    ProgressRecord p = parse_progress("PROGRESS verify 5 33");
    check(p.kind == ProgressRecord::PROGRESS, "PROGRESS kind");
    check_str(p.phase, "verify", "PROGRESS phase");
    check(p.done == 5 && p.total == 33, "PROGRESS counts");

    ProgressRecord s = parse_progress("STATUS installing");
    check(s.kind == ProgressRecord::STATUS, "STATUS kind");
    check_str(s.text, "installing", "STATUS text");

    ProgressRecord s2 = parse_progress("STATUS installed ROM r148");
    check_str(s2.text, "installed ROM r148", "STATUS keeps trailing spaces/words");

    ProgressRecord d = parse_progress("DONE 0");
    check(d.kind == ProgressRecord::DONE && d.code == 0, "DONE 0");

    ProgressRecord d1 = parse_progress("DONE 1");
    check(d1.kind == ProgressRecord::DONE && d1.code == 1, "DONE 1");
}

static void test_progress_unknown_ignored()
{
    printf("progress: unrecognised records are UNKNOWN, never fatal\n");
    check(parse_progress("FUTURE thing 1 2").kind == ProgressRecord::UNKNOWN,
          "future record");
    check(parse_progress("").kind == ProgressRecord::UNKNOWN, "empty line");
    check(parse_progress("garbage").kind == ProgressRecord::UNKNOWN, "garbage");
}

static void test_progress_percent()
{
    printf("progress: bar mapping is monotonic and bounded\n");

    check(progress_percent("verify", 0, 33) == 0.0, "verify starts at 0");
    check(progress_percent("verify", 33, 33) == 60.0, "verify ends at 60");
    check(progress_percent("install", 0, 33) == 60.0, "install starts at 60");
    check(progress_percent("install", 33, 33) == 95.0, "install ends at 95");
    check(progress_percent("smf", 0, 1) == 95.0, "smf starts at 95");
    check(progress_percent("smf", 1, 1) == 100.0, "smf ends at 100");

    /* Phases must not go backwards where they meet. */
    check(progress_percent("verify", 33, 33) <= progress_percent("install", 0, 33),
          "verify->install does not regress");
    check(progress_percent("install", 33, 33) <= progress_percent("smf", 0, 1),
          "install->smf does not regress");

    /* Nonsense input must be rejected, not turned into a bogus bar. */
    check(progress_percent("verify", 1, 0) < 0.0, "zero total rejected");
    check(progress_percent("nonsense", 1, 2) < 0.0, "unknown phase rejected");

    /* Counts beyond the total are clamped, not extrapolated past 100. */
    check(progress_percent("smf", 5, 1) == 100.0, "overshoot clamped to 100");
}

/* ---------------------------------------------------------------- */

int main()
{
    char tmpl[] = "/tmp/romstate-test.XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    tmpdir = tmpl;

    test_manifest_normal();
    test_manifest_missing_file();
    test_manifest_no_version();
    test_manifest_unknown_key_ignored();
    test_manifest_multiline_changelog();
    test_manifest_dirty_version();

    test_history_order_and_skips();
    test_history_missing();
    test_history_package_with_spaces();

    test_progress_records();
    test_progress_unknown_ignored();
    test_progress_percent();

    /* Leave nothing behind; this runs in CI. */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmpdir.c_str());
    if (system(cmd) != 0)
        printf("  (warning: could not remove %s)\n", tmpdir.c_str());

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    if (failures) {
        printf("ROMSTATE-TEST: FAIL\n");
        return 1;
    }
    printf("ROMSTATE-TEST: PASS\n");
    return 0;
}
