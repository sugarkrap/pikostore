/*
 * pikostore -- "Software Center", the ROM's own GUI for managing what is
 * installed on it. FLTK 1.3, X11, built against the staged libfltk that
 * tools/build-fltk.sh produces (see docs/HOWTO-FLTK.md in the piko repo).
 *
 * This is the first GUI in the ROM that does something real rather than
 * proving a library works. It is eventually an ipkg front end; today the
 * Packages tab is a stub and all the working parts are in System Update.
 *
 * System Update shows:
 *   - the running kernel (uname -r),
 *   - this ROM's identity and changelog, read from /etc/zaurus/manifest
 *     (generated per build by tools/gen-rom-manifest.sh),
 *   - every update ever installed with this tool, from
 *     /etc/zaurus/update-history,
 *   - and an Update button that picks a package off the SD card and runs
 *     piko-update against it with live output and a progress bar.
 *
 * DESIGN NOTES THAT ARE NOT OBVIOUS
 *
 * Why --no-reboot. piko-update's default is to reboot the moment it
 * finishes. That is right for a shell invocation and wrong here: a GUI
 * that yanks the machine out from under the window showing its own
 * progress gives the user no chance to read what happened, and no way to
 * tell a successful update from a crash. So the child is always run with
 * --no-reboot and the reboot becomes a button the user presses when they
 * have read the log.
 *
 * Why two pipes. piko-update writes human text to stdout and machine
 * records to --progress-fd. Keeping them apart means the console box can
 * show stdout verbatim -- no filtering, nothing hidden -- while the
 * progress bar reads a stream it can actually parse. Mixing them would
 * force a parser here that has to stay in step with every message
 * piko-update ever prints.
 *
 * Why Fl::add_fd and not a thread. FLTK 1.3 is not thread-safe without
 * lock()/unlock() discipline, and this device is a 400MHz PXA255. Both
 * pipes are watched by the event loop, so the UI keeps redrawing during
 * an update without a single thread being involved.
 *
 * Why the Revert buttons are drawn, not real widgets. They are always
 * disabled for now (reverting needs the old package kept somewhere, which
 * nothing does yet). Drawing them inside Fl_Table::draw_cell avoids
 * embedding and repositioning N real widgets for a feature that cannot be
 * clicked; making them live later means replacing that one draw call.
 *
 * Runs as root, because the X session is started from inittab (see
 * rootfs/etc/inittab) and inherits its uid. piko-update needs that.
 */

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Table.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_draw.H>
#include <FL/fl_ask.H>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "romstate.h"

using romstate::Manifest;
using romstate::HistoryRow;
using romstate::read_manifest;
using romstate::read_history;
using romstate::kernel_release;
using romstate::parse_progress;
using romstate::progress_percent;
using romstate::ProgressRecord;

/* Paths. All of these are contracts with the piko repo, not local
 * choices: piko-update writes the history and installs the manifest, and
 * tools/gen-rom-manifest.sh defines the manifest's format. */
static const char *MANIFEST_PATH = "/etc/zaurus/manifest";
static const char *HISTORY_PATH  = "/etc/zaurus/update-history";
static const char *PIKO_UPDATE   = "/usr/sbin/piko-update";
static const char *SOFTREBOOT    = "/usr/sbin/softreboot";
static const char *SD_CARD_DIR   = "/mnt/card";

/* The typo is deliberate and requested -- do not "fix" it. */
static const char *NO_MANIFEST_MSG = "oups, no changelog found, sowy";

/* ---------------------------------------------------------------------- *
 * History table                                                           *
 * ---------------------------------------------------------------------- */

class HistoryTable : public Fl_Table {
public:
    HistoryTable(int X, int Y, int W, int H, const char *L = 0)
        : Fl_Table(X, Y, W, H, L)
    {
        col_header(1);
        col_resize(1);
        row_header(0);
        row_height_all(24);
        col_header_height(24);
        cols(3);
        rows(0);
        end();
    }

    void set_rows(const std::vector<HistoryRow> &r)
    {
        rows_ = r;
        rows((int)rows_.size());
        /* Widths are set here rather than in the constructor so they can
         * follow a resize of the containing tab. */
        col_width(0, 110);
        col_width(1, 250);
        col_width(2, 110);
        redraw();
    }

protected:
    void draw_cell(TableContext ctx, int R = 0, int C = 0,
                   int X = 0, int Y = 0, int W = 0, int H = 0)
    {
        static const char *headers[3] = { "ROM version", "Installed", "Revert" };

        switch (ctx) {
        case CONTEXT_STARTPAGE:
            fl_font(FL_HELVETICA, 12);
            return;

        case CONTEXT_COL_HEADER:
            fl_push_clip(X, Y, W, H);
            fl_draw_box(FL_THIN_UP_BOX, X, Y, W, H, FL_BACKGROUND_COLOR);
            fl_color(FL_FOREGROUND_COLOR);
            fl_font(FL_HELVETICA_BOLD, 12);
            if (C >= 0 && C < 3)
                fl_draw(headers[C], X + 4, Y, W - 8, H, FL_ALIGN_LEFT);
            fl_pop_clip();
            return;

        case CONTEXT_CELL: {
            if (R < 0 || R >= (int)rows_.size())
                return;
            fl_push_clip(X, Y, W, H);

            fl_color(FL_WHITE);
            fl_rectf(X, Y, W, H);

            if (C == 2) {
                /* Always-disabled Revert. Drawn, not a widget -- see the
                 * file header. FL_INACTIVE_COLOR + an up box is exactly
                 * what a real deactivated Fl_Button looks like, so this
                 * reads as "not available yet", not as "broken". */
                int bw = W - 10, bh = H - 6;
                int bx = X + 5, by = Y + 3;
                fl_draw_box(FL_UP_BOX, bx, by, bw, bh, FL_BACKGROUND_COLOR);
                fl_color(FL_INACTIVE_COLOR);
                fl_font(FL_HELVETICA, 12);
                fl_draw("Revert", bx, by, bw, bh, FL_ALIGN_CENTER);
            } else {
                const std::string &txt = (C == 0) ? rows_[R].version
                                                  : rows_[R].date;
                fl_color(FL_FOREGROUND_COLOR);
                fl_font(FL_HELVETICA, 12);
                fl_draw(txt.c_str(), X + 4, Y, W - 8, H, FL_ALIGN_LEFT);
            }

            fl_color(FL_LIGHT2);
            fl_rect(X, Y, W, H);
            fl_pop_clip();
            return;
        }

        default:
            return;
        }
    }

private:
    std::vector<HistoryRow> rows_;
};

/* ---------------------------------------------------------------------- *
 * The update run: fork piko-update, watch both pipes from the event loop  *
 * ---------------------------------------------------------------------- */

class UpdateDialog {
public:
    UpdateDialog(const std::string &package)
        : package_(package), pid_(-1), out_fd_(-1), prog_fd_(-1),
          total_(0), done_seen_(false), exit_code_(-1), finished_(false),
          rebooted_requested_(false)
    {
        win_ = new Fl_Double_Window(520, 360, "Installing update");
        win_->begin();

        std::string hdr = "Installing " + basename_of(package_);
        header_ = new Fl_Box(10, 8, 500, 20);
        header_->copy_label(hdr.c_str());
        header_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        header_->labelfont(FL_HELVETICA_BOLD);

        bar_ = new Fl_Progress(10, 34, 500, 22);
        bar_->minimum(0);
        bar_->maximum(100);
        bar_->value(0);
        bar_->color(FL_BACKGROUND_COLOR);
        bar_->selection_color(FL_BLUE);
        bar_->labelcolor(FL_FOREGROUND_COLOR);
        bar_->label("starting...");

        buf_ = new Fl_Text_Buffer();
        console_ = new Fl_Text_Display(10, 66, 500, 250);
        console_->buffer(buf_);
        console_->color(FL_BLACK);
        console_->textcolor(fl_rgb_color(0xC0, 0xE0, 0xC0));
        console_->textfont(FL_COURIER);
        console_->textsize(11);
        console_->cursor_style(Fl_Text_Display::SIMPLE_CURSOR);
        console_->cursor_color(FL_BLACK);

        reboot_ = new Fl_Button(300, 324, 100, 26, "Reboot now");
        reboot_->callback(reboot_cb, this);
        reboot_->deactivate();

        close_ = new Fl_Button(410, 324, 100, 26, "Close");
        close_->callback(close_cb, this);
        close_->deactivate();   /* no closing mid-install */

        win_->end();
        win_->set_modal();
        /* No window-manager close either: the callback would have to
         * decide what to do with a running child, and killing piko-update
         * partway through installing files is the one thing this must
         * never make easy. */
        win_->callback(block_close_cb, this);
    }

    ~UpdateDialog()
    {
        cleanup_fds();
        if (win_) { Fl::delete_widget(win_); win_ = 0; }
        /* buf_ is owned by us, not by Fl_Text_Display. */
        delete buf_;
    }

    void run()
    {
        win_->show();
        if (!start_child())
            return;
        while (!finished_)
            Fl::wait();
    }

    bool rebooted() const { return rebooted_requested_; }

private:
    static std::string basename_of(const std::string &p)
    {
        std::string::size_type s = p.rfind('/');
        return (s == std::string::npos) ? p : p.substr(s + 1);
    }

    void append(const char *text)
    {
        buf_->append(text);
        /* Keep the newest output visible without stealing focus. */
        console_->insert_position(buf_->length());
        console_->show_insert_position();
        console_->redraw();
    }

    bool start_child()
    {
        int outp[2], progp[2];

        if (pipe(outp) != 0 || pipe(progp) != 0) {
            append("pikostore: could not create pipes\n");
            finish(-1);
            return false;
        }

        pid_ = fork();
        if (pid_ < 0) {
            append("pikostore: fork failed\n");
            finish(-1);
            return false;
        }

        if (pid_ == 0) {
            /* Child. stdout and stderr both go down the console pipe so
             * the box shows everything in the order it happened; the
             * progress stream gets fd 3 to itself. */
            close(outp[0]);
            close(progp[0]);
            dup2(outp[1], STDOUT_FILENO);
            dup2(outp[1], STDERR_FILENO);
            if (progp[1] != 3) {
                dup2(progp[1], 3);
                close(progp[1]);
            }
            close(outp[1]);

            execl(PIKO_UPDATE, "piko-update", package_.c_str(),
                  "--no-reboot", "--progress-fd", "3", (char *)NULL);
            /* Only reached if exec failed; the parent sees this text. */
            fprintf(stderr, "pikostore: cannot run %s: %s\n",
                    PIKO_UPDATE, strerror(errno));
            _exit(127);
        }

        close(outp[1]);
        close(progp[1]);
        out_fd_  = outp[0];
        prog_fd_ = progp[0];
        set_nonblock(out_fd_);
        set_nonblock(prog_fd_);

        Fl::add_fd(out_fd_,  FL_READ, out_ready, this);
        Fl::add_fd(prog_fd_, FL_READ, prog_ready, this);
        return true;
    }

    static void set_nonblock(int fd)
    {
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl >= 0)
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }

    static void out_ready(int fd, void *v)  { ((UpdateDialog *)v)->on_out(fd); }
    static void prog_ready(int fd, void *v) { ((UpdateDialog *)v)->on_prog(fd); }

    void on_out(int fd)
    {
        char buf[1024];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);

        if (n > 0) {
            buf[n] = '\0';
            append(buf);
            return;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return;

        Fl::remove_fd(fd);
        close(fd);
        out_fd_ = -1;
        maybe_finish();
    }

    void on_prog(int fd)
    {
        char buf[1024];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);

        if (n > 0) {
            buf[n] = '\0';
            pending_ += buf;
            std::string::size_type nl;
            while ((nl = pending_.find('\n')) != std::string::npos) {
                handle_record(pending_.substr(0, nl));
                pending_.erase(0, nl + 1);
            }
            return;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return;

        Fl::remove_fd(fd);
        close(fd);
        prog_fd_ = -1;
        maybe_finish();
    }

    /* One progress record. Unknown records are ignored by design, so
     * piko-update can add new ones without breaking this build. */
    void handle_record(const std::string &rec)
    {
        ProgressRecord p = parse_progress(rec);

        switch (p.kind) {
        case ProgressRecord::TOTAL:
            total_ = p.total;
            break;

        case ProgressRecord::PROGRESS:
            set_progress(p.phase, p.done, p.total);
            break;

        case ProgressRecord::STATUS:
            status_ = p.text;
            bar_->copy_label(status_.c_str());
            bar_->redraw();
            break;

        case ProgressRecord::DONE:
            done_seen_ = true;
            exit_code_ = p.code;
            break;

        case ProgressRecord::UNKNOWN:
        default:
            /* Ignored on purpose so a newer piko-update can add records. */
            break;
        }
    }

    /* Verify is weighted heaviest because it is: every file gets MD5'd,
     * including a ~1.2MB kernel, while installing is a rename per file. A
     * bar that raced to 90% during verify and then sat there would be
     * worse than no bar. */
    void set_progress(const std::string &phase, int done, int total)
    {
        double pct = progress_percent(phase, done, total);
        char lbl[128];

        if (pct < 0.0)
            return;             /* unknown phase or nonsense counts */

        bar_->value((float)pct);
        snprintf(lbl, sizeof(lbl), "%s  %d/%d  (%d%%)",
                 phase.c_str(), done, total, (int)(pct + 0.5));
        bar_->copy_label(lbl);
        bar_->redraw();
    }

    void maybe_finish()
    {
        if (out_fd_ >= 0 || prog_fd_ >= 0)
            return;                 /* still reading one of them */

        int status = 0;
        int rc = exit_code_;

        if (pid_ > 0) {
            while (waitpid(pid_, &status, 0) < 0 && errno == EINTR)
                ;
            /* The wait status is authoritative over the DONE record: DONE
             * says what piko-update believed, the status says what
             * actually happened to the process (a signal, say). */
            if (WIFEXITED(status))
                rc = WEXITSTATUS(status);
            else
                rc = -1;
            pid_ = -1;
        }
        finish(rc);
    }

    void finish(int rc)
    {
        if (finished_)
            return;
        finished_ = true;
        exit_code_ = rc;

        char msg[160];
        if (rc == 0) {
            bar_->value(100);
            bar_->selection_color(FL_DARK_GREEN);
            snprintf(msg, sizeof(msg), "done -- update installed");
            append("\n--- update installed. Reboot to run it. ---\n");
            reboot_->activate();
        } else {
            bar_->selection_color(FL_RED);
            snprintf(msg, sizeof(msg), "FAILED (exit %d) -- nothing was installed", rc);
            append("\n--- update FAILED. See the messages above. ---\n");
            if (!done_seen_)
                append("(piko-update did not report a result -- it may have been killed)\n");
        }
        bar_->copy_label(msg);
        bar_->redraw();
        close_->activate();
    }

    void cleanup_fds()
    {
        if (out_fd_  >= 0) { Fl::remove_fd(out_fd_);  close(out_fd_);  out_fd_  = -1; }
        if (prog_fd_ >= 0) { Fl::remove_fd(prog_fd_); close(prog_fd_); prog_fd_ = -1; }
    }

    static void close_cb(Fl_Widget *, void *v)
    {
        UpdateDialog *d = (UpdateDialog *)v;
        d->win_->hide();
        d->finished_ = true;
    }

    static void reboot_cb(Fl_Widget *, void *v)
    {
        UpdateDialog *d = (UpdateDialog *)v;
        if (fl_choice("Reboot now to start the new system?", "Cancel", "Reboot", 0) != 1)
            return;
        d->rebooted_requested_ = true;
        d->win_->hide();
        d->finished_ = true;
        execl(SOFTREBOOT, "softreboot", (char *)NULL);
        fl_alert("Could not run %s.\nReboot the device by hand.", SOFTREBOOT);
    }

    static void block_close_cb(Fl_Widget *, void *v)
    {
        UpdateDialog *d = (UpdateDialog *)v;
        if (d->finished_) {
            d->win_->hide();
            return;
        }
        fl_message("The update is still running.\n"
                   "Wait for it to finish before closing this window.");
    }

    std::string       package_;
    std::string       pending_;
    std::string       status_;
    pid_t             pid_;
    int               out_fd_;
    int               prog_fd_;
    int               total_;
    bool              done_seen_;
    int               exit_code_;
    bool              finished_;
    bool              rebooted_requested_;

    Fl_Double_Window *win_;
    Fl_Box           *header_;
    Fl_Progress      *bar_;
    Fl_Text_Display  *console_;
    Fl_Text_Buffer   *buf_;
    Fl_Button        *reboot_;
    Fl_Button        *close_;
};

/* ---------------------------------------------------------------------- *
 * System Update tab                                                       *
 * ---------------------------------------------------------------------- */

class SystemUpdateTab {
public:
    SystemUpdateTab(int X, int Y, int W, int H)
    {
        group_ = new Fl_Group(X, Y, W, H, "System Update");
        group_->begin();

        int m = 10;                 /* margin */
        int cx = X + m;
        int cw = W - 2 * m;
        int y  = Y + m;

        kernel_ = new Fl_Box(cx, y, cw, 18);
        kernel_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        y += 20;

        rom_ = new Fl_Box(cx, y, cw, 18);
        rom_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        rom_->labelfont(FL_HELVETICA_BOLD);
        y += 24;

        /* The changelog is a read-only text display rather than a Box so
         * a long paragraph scrolls instead of being clipped away. */
        chbuf_ = new Fl_Text_Buffer();
        changelog_ = new Fl_Text_Display(cx, y, cw, 92);
        changelog_->buffer(chbuf_);
        changelog_->textsize(12);
        changelog_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
        changelog_->cursor_style(Fl_Text_Display::SIMPLE_CURSOR);
        changelog_->cursor_color(FL_BACKGROUND_COLOR);
        y += 92 + 14;

        Fl_Box *hist = new Fl_Box(cx, y, cw / 2, 22, "Update history");
        hist->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        hist->labelfont(FL_HELVETICA_BOLD);

        update_ = new Fl_Button(X + W - m - 120, y, 120, 24, "Update...");
        update_->callback(update_cb, this);
        y += 26;

        table_ = new HistoryTable(cx, y, cw, (Y + H - m) - y);

        group_->end();
        group_->resizable(table_);

        refresh();
    }

    Fl_Group *group() { return group_; }

    /* Re-reads everything from disk. Called at startup and again after an
     * update, so the tab reflects the new ROM without a restart. */
    void refresh()
    {
        std::string k = "Kernel:  " + kernel_release();
        kernel_->copy_label(k.c_str());

        Manifest m = read_manifest(MANIFEST_PATH);
        if (m.found) {
            std::string r = "ROM:  " + m.version;
            if (!m.built.empty())
                r += "     built " + m.built;
            if (!m.commit.empty())
                r += "     (" + m.commit + ")";
            rom_->copy_label(r.c_str());

            if (m.changelog.empty())
                chbuf_->text(NO_MANIFEST_MSG);
            else
                chbuf_->text(m.changelog.c_str());
        } else {
            rom_->copy_label("ROM:  unknown");
            chbuf_->text(NO_MANIFEST_MSG);
        }

        table_->set_rows(read_history(HISTORY_PATH));
    }

private:
    static void update_cb(Fl_Widget *, void *v)
    {
        ((SystemUpdateTab *)v)->do_update();
    }

    void do_update()
    {
        /* Default to the SD card: that is where a package arrives from,
         * and typing a path on this keyboard is close to impossible (it
         * cannot produce '/', see AGENTS.md). */
        const char *start = SD_CARD_DIR;
        if (access(SD_CARD_DIR, R_OK) != 0)
            start = "/";

        const char *pick = fl_file_chooser("Choose an update package",
                                           "Update packages (*.tar)", start, 0);
        if (!pick)
            return;

        std::string package(pick);

        if (access(PIKO_UPDATE, X_OK) != 0) {
            fl_alert("%s is missing.\nThis ROM cannot update itself.", PIKO_UPDATE);
            return;
        }

        if (fl_choice("Install this update?\n\n%s\n\n"
                      "The package is verified before anything is changed.",
                      "Cancel", "Install", 0, package.c_str()) != 1)
            return;

        {
            UpdateDialog dlg(package);
            dlg.run();
        }

        /* The history and manifest have changed if it worked. */
        refresh();
    }

    Fl_Group        *group_;
    Fl_Box          *kernel_;
    Fl_Box          *rom_;
    Fl_Text_Display *changelog_;
    Fl_Text_Buffer  *chbuf_;
    Fl_Button       *update_;
    HistoryTable    *table_;
};

/* ---------------------------------------------------------------------- *
 * Packages tab (stub)                                                     *
 * ---------------------------------------------------------------------- */

static Fl_Group *make_packages_tab(int X, int Y, int W, int H)
{
    Fl_Group *g = new Fl_Group(X, Y, W, H, "Packages");
    g->begin();

    Fl_Box *title = new Fl_Box(X + 10, Y + 20, W - 20, 24,
                               "Package management is not built yet");
    title->labelfont(FL_HELVETICA_BOLD);
    title->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

    Fl_Box *body = new Fl_Box(X + 10, Y + 50, W - 20, 60,
                              "This tab will browse, install and remove ipkg\n"
                              "packages. For now, use the System Update tab to\n"
                              "install a full ROM update.");
    body->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

    g->end();
    g->resizable(body);
    return g;
}

/* ---------------------------------------------------------------------- *
 * main                                                                    *
 * ---------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    /* A child that dies while we are not in waitpid must not become a
     * zombie holding the pipe open. */
    signal(SIGPIPE, SIG_IGN);

    /* 640x480 is the panel. Matchbox may resize or fullscreen this; every
     * tab has a resizable() so that works out. */
    Fl_Double_Window win(640, 460, "Software Center");
    win.begin();

    Fl_Tabs tabs(0, 0, 640, 460);
    tabs.begin();

    /* Packages first, System Update last, as specified. */
    make_packages_tab(0, 24, 640, 436);
    SystemUpdateTab sysupd(0, 24, 640, 436);

    tabs.end();
    tabs.resizable(sysupd.group());

    win.end();
    win.resizable(tabs);
    win.show(argc, argv);

    return Fl::run();
}
