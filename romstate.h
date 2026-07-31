/*
 * romstate.h -- reading what the ROM says about itself.
 *
 * Split out of pikostore.cxx deliberately: this is the part with actual
 * parsing in it, and it depends on nothing but libc and libstdc++. That
 * means it can be compiled and tested on the build host, where there is
 * no FLTK and no /etc/zaurus, which is the only way any of this gets
 * exercised before it reaches the one spare Zaurus.
 *
 * See tests/romstate-test.cxx.
 *
 * Both file formats are defined in the piko repo, not here:
 *   /etc/zaurus/manifest        tools/gen-rom-manifest.sh
 *   /etc/zaurus/update-history  userspace/src/piko-update.c (record_history)
 */

#ifndef PIKOSTORE_ROMSTATE_H
#define PIKOSTORE_ROMSTATE_H

#include <stdio.h>
#include <sys/utsname.h>

#include <string>
#include <vector>

namespace romstate {

inline std::string trim(const std::string &s)
{
    std::string::size_type a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return std::string();
    std::string::size_type b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

struct Manifest {
    bool        found;
    std::string version;
    std::string built;
    std::string commit;
    std::string changelog;

    Manifest() : found(false) {}
};

/* Parses the manifest: "key: value" header lines, one blank line, then
 * the changelog as free prose. The blank-line split is the whole contract
 * -- it lets a changelog paragraph contain colons and punctuation without
 * any escaping (see gen-rom-manifest.sh).
 *
 * A manifest with no version is reported as not-found: there is nothing
 * useful to show from it, and the caller's "no changelog" message is a
 * better answer than a half-filled screen. */
inline Manifest read_manifest(const char *path)
{
    Manifest m;
    FILE *f = fopen(path, "r");
    char line[1024];
    bool in_body = false;

    if (!f)
        return m;

    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s[s.size() - 1] == '\n' || s[s.size() - 1] == '\r'))
            s.erase(s.size() - 1);

        if (!in_body) {
            if (s.empty()) { in_body = true; continue; }
            if (s.size() >= 8 && s.compare(0, 8, "version:") == 0)
                m.version = trim(s.substr(8));
            else if (s.size() >= 6 && s.compare(0, 6, "built:") == 0)
                m.built = trim(s.substr(6));
            else if (s.size() >= 7 && s.compare(0, 7, "commit:") == 0)
                m.commit = trim(s.substr(7));
            /* Unrecognised header keys are ignored on purpose, so the
             * generator can add fields without breaking older builds. */
        } else {
            if (!m.changelog.empty())
                m.changelog += "\n";
            m.changelog += s;
        }
    }
    fclose(f);

    m.changelog = trim(m.changelog);
    m.found = !m.version.empty();
    return m;
}

struct HistoryRow {
    std::string version;
    std::string date;
    std::string package;
};

/* Reads "<version>|<iso-date>|<package>" lines and returns them NEWEST
 * FIRST, which is display order -- the file itself is append-only so the
 * newest entry is last.
 *
 * A malformed line is skipped, not fatal: this file accumulates across
 * every update the device ever takes, and one bad line must not hide the
 * rest of the history or refuse to draw the table. */
inline std::vector<HistoryRow> read_history(const char *path)
{
    std::vector<HistoryRow> rows;
    FILE *f = fopen(path, "r");
    char line[1024];

    if (!f)
        return rows;

    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s[s.size() - 1] == '\n' || s[s.size() - 1] == '\r'))
            s.erase(s.size() - 1);
        if (s.empty())
            continue;

        std::string::size_type p1 = s.find('|');
        if (p1 == std::string::npos) continue;
        std::string::size_type p2 = s.find('|', p1 + 1);
        if (p2 == std::string::npos) continue;

        HistoryRow r;
        r.version = trim(s.substr(0, p1));
        r.date    = trim(s.substr(p1 + 1, p2 - p1 - 1));
        r.package = trim(s.substr(p2 + 1));
        if (!r.version.empty())
            rows.push_back(r);
    }
    fclose(f);

    std::vector<HistoryRow> out;
    for (std::vector<HistoryRow>::reverse_iterator it = rows.rbegin();
         it != rows.rend(); ++it)
        out.push_back(*it);
    return out;
}

inline std::string kernel_release()
{
    struct utsname u;
    if (uname(&u) != 0)
        return std::string("unknown");
    return std::string(u.release);
}

/* One record off piko-update's --progress-fd stream, already split on
 * newlines by the caller. Kept here rather than in the dialog so the
 * protocol can be tested without a running child or an X display. */
struct ProgressRecord {
    enum Kind { UNKNOWN, TOTAL, PROGRESS, STATUS, DONE };

    Kind        kind;
    std::string phase;      /* PROGRESS only */
    int         done;       /* PROGRESS */
    int         total;      /* PROGRESS, TOTAL */
    int         code;       /* DONE */
    std::string text;       /* STATUS */

    ProgressRecord() : kind(UNKNOWN), done(0), total(0), code(0) {}
};

inline ProgressRecord parse_progress(const std::string &rec)
{
    ProgressRecord p;
    char phase[64];
    int a = 0, b = 0;

    if (sscanf(rec.c_str(), "TOTAL %d", &a) == 1) {
        p.kind = ProgressRecord::TOTAL;
        p.total = a;
        return p;
    }
    if (sscanf(rec.c_str(), "PROGRESS %63s %d %d", phase, &a, &b) == 3) {
        p.kind = ProgressRecord::PROGRESS;
        p.phase = phase;
        p.done = a;
        p.total = b;
        return p;
    }
    if (rec.size() >= 7 && rec.compare(0, 7, "STATUS ") == 0) {
        p.kind = ProgressRecord::STATUS;
        p.text = rec.substr(7);
        return p;
    }
    if (sscanf(rec.c_str(), "DONE %d", &a) == 1) {
        p.kind = ProgressRecord::DONE;
        p.code = a;
        return p;
    }
    /* Deliberately UNKNOWN rather than an error: piko-update must be free
     * to add records without breaking an older pikostore. */
    return p;
}

/* Maps a PROGRESS record onto a 0-100 bar.
 *
 * Verify is weighted heaviest because it is the slow phase -- every file
 * gets MD5'd, including a ~1.2MB kernel -- while installing is a rename
 * per file. A bar that raced to 90% during verify and then sat there
 * would be worse than no bar at all. */
inline double progress_percent(const std::string &phase, int done, int total)
{
    if (total <= 0)
        return -1.0;
    double frac = (double)done / (double)total;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;

    if (phase == "verify")  return frac * 60.0;
    if (phase == "install") return 60.0 + frac * 35.0;
    if (phase == "smf")     return 95.0 + frac * 5.0;
    return -1.0;
}

} /* namespace romstate */

#endif /* PIKOSTORE_ROMSTATE_H */
