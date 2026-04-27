/*
 * img_conv_ui.c — ncurses front-end for img_conv
 * Scans assets/ for .png files; configure flags and run conversions.
 *
 * LIST:  Up/Down=select  Enter=settings  C=convert  A=convert all  Q=quit
 * EDIT:  Up/Down=field   +/-=change  Enter=edit path  C=convert  Bksp=back
 */
#include <ncurses.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PNGS 256
#define MAX_PATH 256

typedef struct {
    char input[MAX_PATH];
    char output[MAX_PATH];
    int  tile_mode;         /* 0=.bin, 1=.til */
    int  colors;            /* 0=auto, 1-127=k-means, -1..-3=built-in palette */
    char status[48];
} Entry;

static Entry entries[MAX_PNGS];
static int   n_entries = 0;

/* ---- directory scan ---- */

/* longest dir prefix is "assets/sprites/" = 15 chars; leave 16 to be safe */
#define MAX_NAME (MAX_PATH - 16)

static void set_output(Entry *e) {
    const char *base = strrchr(e->input, '/');
    base = base ? base + 1 : e->input;
    const char *dir = e->tile_mode ? "assets/tiles" : "assets/sprites";
    char name[MAX_NAME];
    strncpy(name, base, MAX_NAME - 1);
    name[MAX_NAME - 1] = '\0';
    char *dot = strrchr(name, '.');
    if (dot) strcpy(dot, e->tile_mode ? ".til" : ".bin");
    snprintf(e->output, MAX_PATH, "%s/%s", dir, name);
}

static void scan_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    char path[MAX_PATH * 2];
    while ((ent = readdir(d)) != NULL && n_entries < MAX_PNGS) {
        if (ent->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_dir(path);
        } else {
            size_t len = strlen(ent->d_name);
            if (len > 4 && strcmp(ent->d_name + len - 4, ".png") == 0) {
                Entry *e = &entries[n_entries++];
                strncpy(e->input, path, MAX_PATH - 1);
                e->tile_mode = 0;
                set_output(e);
                e->colors    = 0;
                e->status[0] = '\0';
            }
        }
    }
    closedir(d);
}

static int entry_cmp(const void *a, const void *b) {
    return strcmp(((const Entry *)a)->input, ((const Entry *)b)->input);
}

/* ---- helpers ---- */

static const char *colors_str(int c) {
    static char buf[48];
    switch (c) {
        case  0: return "auto (all unique colors)";
        case -1: return "-1  Gameboy DMG (4 colors)";
        case -2: return "-2  Grayscale   (4 colors)";
        case -3: return "-3  CGA         (16 colors)";
        default:
            snprintf(buf, sizeof(buf), "%d  (k-means quantize)", c);
            return buf;
    }
}

/* ---- conversion ---- */

static void run_convert(Entry *e) {
    char cmd[600];
    int n = snprintf(cmd, sizeof(cmd), "./build/img_conv");
    if (e->tile_mode) n += snprintf(cmd+n, sizeof(cmd)-n, " -t");
    n += snprintf(cmd+n, sizeof(cmd)-n, " \"%s\" \"%s\"", e->input, e->output);
    if (e->colors != 0) snprintf(cmd+n, sizeof(cmd)-n, " %d", e->colors);
    int ret = system(cmd);
    snprintf(e->status, sizeof(e->status), ret == 0 ? "OK" : "Error %d", ret);
}

/* ---- inline string edit ---- */

static int edit_str(int row, int col, char *buf, int maxLen) {
    int len = (int)strlen(buf);
    echo(); curs_set(1);
    while (1) {
        mvhline(row, col, ' ', maxLen);
        mvprintw(row, col, "%.*s", len, buf);
        move(row, col + len);
        refresh();
        int ch = getch();
        if (ch == '\n' || ch == KEY_ENTER) break;
        if (ch == 27) { noecho(); curs_set(0); return 0; }
        if ((ch == KEY_BACKSPACE || ch == 127) && len > 0) { buf[--len] = '\0'; continue; }
        if (ch >= 32 && ch < 127 && len < maxLen - 1) { buf[len++] = (char)ch; buf[len] = '\0'; }
    }
    noecho(); curs_set(0);
    return 1;
}

/* ---- edit screen ---- */

typedef enum { F_OUTPUT = 0, F_TILE, F_COLORS, F_COUNT } Field;

static void render_edit(Entry *e, int sel, const char *msg) {
    clear();
    mvprintw(0, 0, "IMG CONVERTER — %s", e->input);
    mvprintw(1, 0, "Up/Down=field  +/-=change  Enter=edit path  C=convert  Bksp=back");
    if (msg) mvprintw(2, 0, "%s", msg);

    const char *labels[F_COUNT] = { "Output", "Tile mode", "Colors" };
    for (int i = 0; i < F_COUNT; i++) {
        if (i == sel) attron(A_REVERSE);
        switch (i) {
            case F_OUTPUT: mvprintw(i+4, 2, "%-12s  %s",  labels[i], e->output);                    break;
            case F_TILE:   mvprintw(i+4, 2, "%-12s  %s",  labels[i], e->tile_mode ? "[X]" : "[ ]"); break;
            case F_COLORS: mvprintw(i+4, 2, "%-12s  %s",  labels[i], colors_str(e->colors));         break;
        }
        if (i == sel) attroff(A_REVERSE);
    }
    refresh();
}

static void screen_edit(int idx) {
    Entry *e = &entries[idx];
    int sel = 0;
    const char *msg = e->status[0] ? e->status : NULL;

    while (1) {
        render_edit(e, sel, msg);
        msg = NULL;
        int ch = getch();
        switch (ch) {
            case KEY_UP:        if (sel > 0) sel--;          break;
            case KEY_DOWN:      if (sel < F_COUNT-1) sel++;  break;
            case KEY_BACKSPACE:
            case 127:           return;

            case '\n': case KEY_ENTER:
                if (sel == F_OUTPUT)
                    edit_str(sel+4, 16, e->output, MAX_PATH - 1);
                break;

            case '+': case '=':
                if (sel == F_TILE)   { e->tile_mode ^= 1; set_output(e); }
                else if (sel == F_COLORS && e->colors < 127) e->colors++;
                break;

            case '-':
                if (sel == F_TILE)   { e->tile_mode ^= 1; set_output(e); }
                else if (sel == F_COLORS && e->colors > -3) e->colors--;
                break;

            case 'c': case 'C':
                endwin();
                printf("\n--- converting %s ---\n", e->input);
                run_convert(e);
                printf("--- %s ---  press any key\n", e->status);
                fflush(stdout); getchar();
                initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0);
                msg = e->status;
                break;
        }
    }
}

/* ---- list screen ---- */

static void render_list(int sel, int scroll, const char *msg) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;
    clear();
    mvprintw(0, 0, "IMG CONVERTER — %d PNG(s) in assets/pngs/", n_entries);
    mvprintw(1, 0, "Up/Down=select  Enter=settings  C=convert  A=convert all  Q=quit");
    if (msg) mvprintw(2, 0, "%s", msg);

    int vis = rows - 4;
    for (int i = 0; i < vis && scroll + i < n_entries; i++) {
        int idx = scroll + i;
        Entry *e = &entries[idx];
        if (idx == sel) attron(A_REVERSE);
        const char *in  = strncmp(e->input,  "assets/pngs/", 12) == 0 ? e->input  + 12 : e->input;
        const char *out = strncmp(e->output, "assets/",       7) == 0 ? e->output +  7 : e->output;
        mvprintw(i+4, 2, "%-28s -> %-28s %s  %s",
            in, out,
            e->tile_mode ? "[til]" : "[bin]",
            e->status);
        if (idx == sel) attroff(A_REVERSE);
    }
    if (n_entries == 0)
        mvprintw(4, 2, "(no .png files found in assets/)");
    refresh();
}

/* ---- main ---- */

int main(void) {
    scan_dir("assets/pngs");
    qsort(entries, (size_t)n_entries, sizeof(Entry), entry_cmp);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    int sel = 0, scroll = 0;
    const char *msg = NULL;
    int running = 1;

    while (running) {
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        (void)cols;
        int vis = rows - 4;
        if (sel < scroll)        scroll = sel;
        if (sel >= scroll + vis) scroll = sel - vis + 1;

        render_list(sel, scroll, msg);
        msg = NULL;
        int ch = getch();

        switch (ch) {
            case KEY_UP:   if (sel > 0) sel--;              break;
            case KEY_DOWN: if (sel < n_entries-1) sel++;    break;

            case '\n': case KEY_ENTER:
                if (n_entries > 0) screen_edit(sel);
                break;

            case 'c': case 'C':
                if (n_entries > 0) {
                    endwin();
                    printf("\n--- converting %s ---\n", entries[sel].input);
                    run_convert(&entries[sel]);
                    printf("--- %s ---  press any key\n", entries[sel].status);
                    fflush(stdout); getchar();
                    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0);
                    msg = entries[sel].status;
                }
                break;

            case 'a': case 'A':
                endwin();
                printf("\n--- converting all %d file(s) ---\n", n_entries);
                for (int i = 0; i < n_entries; i++)
                    run_convert(&entries[i]);
                printf("--- done ---  press any key\n");
                fflush(stdout); getchar();
                initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0);
                msg = "All done.";
                break;

            case 'q': case 'Q':
                running = 0;
                break;
        }
    }

    endwin();
    return 0;
}
