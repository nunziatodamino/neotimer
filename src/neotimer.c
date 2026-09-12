#define _POSIX_C_SOURCE 200809L

#include "timer.h"
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
static volatile sig_atomic_t stopped;
static struct termios original_terminal;
static bool terminal_active;

static void on_signal(int signal_number) { stopped = signal_number; }

static int64_t monotonic_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        perror("neotimer: clock_gettime");
        exit(1);
    }
    return (int64_t)now.tv_sec * NS_PER_SECOND + now.tv_nsec;
}

static void restore_terminal(void)
{
    if (!terminal_active) return;
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &original_terminal);
    fputs("\033[0m\033[?25h\033[?1049l", stdout);
    fflush(stdout);
    terminal_active = false;
}

static bool setup_terminal(void)
{
    const char *term = getenv("TERM");
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)
        || !term || !strcmp(term, "dumb")) return false;
    if (tcgetattr(STDIN_FILENO, &original_terminal) == -1) return false;
    struct termios raw = original_terminal;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) return false;
    terminal_active = true;
    fputs("\033[?1049h\033[?25l\033[2J", stdout);
    return true;
}

static const char *const digits[11][7] = {
    {" #### ", "##  ##", "##  ##", "##  ##", "##  ##", "##  ##", " #### "},
    {"  ##  ", " ###  ", "####  ", "  ##  ", "  ##  ", "  ##  ", "######"},
    {" #### ", "##  ##", "    ##", "  ### ", " ##   ", "##    ", "######"},
    {"##### ", "    ##", "    ##", " #### ", "    ##", "    ##", "##### "},
    {"##  ##", "##  ##", "##  ##", "######", "    ##", "    ##", "    ##"},
    {"######", "##    ", "##    ", "##### ", "    ##", "    ##", "##### "},
    {" #### ", "##    ", "##    ", "##### ", "##  ##", "##  ##", " #### "},
    {"######", "    ##", "   ## ", "  ##  ", " ##   ", " ##   ", " ##   "},
    {" #### ", "##  ##", "##  ##", " #### ", "##  ##", "##  ##", " #### "},
    {" #### ", "##  ##", "##  ##", " #####", "    ##", "    ##", " #### "},
    {"  ", "##", "##", "  ", "##", "##", "  "}
};

static void centered(int row, int columns, const char *line, const char *color)
{
    int length = (int)strlen(line);
    int visible = length < columns - 1 ? length : columns - 1;
    if (visible < 1) return;
    int column = (columns - visible) / 2 + 1;
    printf("\033[%d;%dH%s%.*s\033[0m", row, column, color, visible, line);
}

static const char *rainbow_color(size_t offset)
{
    static const char *const colors[] = {
        "\033[31m", "\033[33m", "\033[32m", "\033[36m", "\033[34m", "\033[35m"
    };
    return colors[offset % (sizeof(colors) / sizeof(colors[0]))];
}

static void centered_rainbow(int row, int columns, const char *line, size_t phase, bool color)
{
    if (!color) { centered(row, columns, line, ""); return; }
    int length = (int)strlen(line);
    int visible = length < columns - 1 ? length : columns - 1;
    if (visible < 1) return;
    printf("\033[%d;%dH", row, (columns - visible) / 2 + 1);
    for (int i = 0; i < visible; ++i)
        printf("%s%c", rainbow_color(phase + (size_t)i), line[i]);
    fputs("\033[0m", stdout);
}

static void render(const Timer *timer, int64_t total_ns, bool color, bool unicode, bool easter_egg)
{
    struct winsize size = {0};
    (void)ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);
    int columns = size.ws_col ? size.ws_col : 80;
    int rows = size.ws_row ? size.ws_row : 24;
    char time_text[32];
    if (easter_egg) snprintf(time_text, sizeof(time_text), "00:00");
    else format_time(timer_seconds(timer), time_text, sizeof(time_text));
    size_t phase = easter_egg ? (size_t)((monotonic_ns() / 200000000) % 6) : 0;
    int width = -2;
    for (size_t i = 0; time_text[i]; ++i) width += (time_text[i] == ':' ? 2 : 6) + 2;
    bool large = columns >= width + 4 && rows >= 15;
    int height = large ? 13 : (rows >= 7 ? 5 : (easter_egg && rows >= 3 ? 3 : 1));
    int top = (rows - height) / 2 + 1;
    const char *accent = color ? (timer->paused ? "\033[33m" : "\033[36m") : "";
    const char *dim = color ? "\033[2m" : "";
    // Assemble a frame before flushing, avoiding visible partial redraws.
    fputs("\033[H\033[2J", stdout);
    if (height == 1) {
        char compact[64];
        snprintf(compact, sizeof(compact), "%s%s", time_text, timer->paused ? " [paused]" : "");
        if (easter_egg) centered_rainbow(top, columns, time_text, phase, color);
        else centered(top, columns, compact, accent);
        fflush(stdout);
        return;
    }
    centered(top, columns, easter_egg ? "Time i can stay without you"
                                    : (timer->paused ? "NEOTIMER / PAUSED" : "NEOTIMER"), dim);
    if (large) {
        for (int row = 0; row < 7; ++row) {
            // Position by terminal cells, not UTF-8 byte length.
            printf("\033[%d;%dH%s", top + 2 + row, (columns - width) / 2 + 1, accent);
            for (size_t i = 0; time_text[i]; ++i) {
                if (easter_egg && color) fputs(rainbow_color(phase + i + (size_t)row), stdout);
                int digit = time_text[i] == ':' ? 10 : time_text[i] - '0';
                for (const char *pixel = digits[digit][row]; *pixel; ++pixel)
                    fputs(*pixel == '#' ? (unicode ? "\xe2\x96\x88" : "#") : " ", stdout);
                if (time_text[i + 1]) fputs("  ", stdout);
            }
            fputs("\033[0m", stdout);
        }
    } else if (easter_egg) centered_rainbow(top + 1, columns, time_text, phase, color);
    else centered(top + 1, columns, time_text, accent);
    int bar_width = columns - 8;
    if (bar_width > 42) bar_width = 42;
    if (!easter_egg && bar_width >= 4) {
        char bar[48];
        double progress = 1.0 - (double)timer->remaining_ns / (double)total_ns;
        int filled = (int)(progress * bar_width);
        bar[0] = '[';
        for (int i = 0; i < bar_width; ++i) bar[i + 1] = i < filled ? '=' : '-';
        bar[bar_width + 1] = ']';
        bar[bar_width + 2] = '\0';
        centered(top + (large ? 10 : 3), columns, bar, accent);
    }
    centered(top + height - 1, columns,
             easter_egg ? "Esc exit  |  Ctrl+C exit" : "Space pause/resume  |  Esc exit", dim);
    fflush(stdout);
}

static void notify_completion(const char *duration)
{
    char body[96];
    snprintf(body, sizeof(body), "Your %s countdown is complete.", duration);
    char *args[] = {"notify-send", "--app-name=neotimer", "--hint=boolean:suppress-sound:true",
                    "Timer complete", body, NULL};
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return;
    int error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (!error) error = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    if (!error) error = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t child;
    if (!error) (void)posix_spawnp(&child, "notify-send", &actions, NULL, args, environ);
    (void)posix_spawn_file_actions_destroy(&actions);
}

static void usage(FILE *stream)
{
    fputs("Usage: neotimer <N{s|m|h}> [N{s|m|h} ...]\n\n"
          "Examples: neotimer 45m, neotimer 1h 40m, neotimer 1h40m30s\n"
          "Combine integer hours, minutes, and seconds, with or without spaces.\n"
          "Components must be nonnegative; the total must be positive.\n\n"
          "Space  Pause/resume\nEsc    Exit\nCtrl+C Exit\n\n"
          "Sends a silent desktop notification at completion when notify-send is available.\n"
          "Set NO_COLOR to disable colors.\n", stream);
}

int main(int argc, char **argv)
{
    (void)setlocale(LC_CTYPE, "");
    bool unicode = strcmp(nl_langinfo(CODESET), "UTF-8") == 0;
    // A full frame fits in this buffer, including UTF-8 block characters.
    (void)setvbuf(stdout, NULL, _IOFBF, 8192);
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        usage(stdout);
        return 0;
    }
    // Hidden dedication: a rainbow zero that stays on screen until the user exits.
    bool easter_egg = argc == 2 && !strcmp(argv[1], "pipi");
    int64_t seconds = 0;
    if (!easter_egg && !parse_duration(argc - 1, argv + 1, &seconds)) {
        fputs("neotimer: invalid duration. Use e.g. 45m, 1h 40m, or 1h40m30s; "
              "the total must be positive and at most 9223372036 seconds.\n", stderr);
        usage(stderr);
        return 2;
    }
    if (atexit(restore_terminal) != 0) return 1;
    struct sigaction action = {0};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGPIPE};
    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        if (sigaction(signals[i], &action, NULL) == -1) {
            perror("neotimer: sigaction");
            return 1;
        }
    }
    bool interactive = setup_terminal();
    bool color = getenv("NO_COLOR") == NULL;
    Timer timer = {seconds * NS_PER_SECOND, monotonic_ns(), false};
    int64_t total_ns = timer.remaining_ns;
    bool cancelled = false;
    bool failed = false;
    if (!interactive) {
        if (easter_egg) {
            puts("Time i can stay without you\n00:00");
            return 0;
        }
        char initial[32];
        format_time(seconds, initial, sizeof(initial));
        printf("neotimer: started %s countdown.\n", initial);
        fflush(stdout);
    }
    while (!stopped) {
        if (!easter_egg) timer_update(&timer, monotonic_ns());
        if (interactive) render(&timer, total_ns, color, unicode, easter_egg);
        if (!easter_egg && timer.remaining_ns == 0) break;
        struct pollfd input = {STDIN_FILENO, POLLIN, 0};
        int ready = poll(interactive ? &input : NULL, interactive ? 1 : 0, 100);
        if (ready < 0 && errno != EINTR) { failed = true; break; }
        if (interactive && ready > 0) {
            if (input.revents & (POLLERR | POLLHUP | POLLNVAL)) { cancelled = true; break; }
            if (input.revents & POLLIN) {
                char keys[64];
                ssize_t count = read(STDIN_FILENO, keys, sizeof(keys));
                if (count < 0 && errno != EINTR) { failed = true; break; }
                for (ssize_t i = 0; i < count; ++i) {
                    if (keys[i] == '\033') { cancelled = true; break; }
                    if (!easter_egg && keys[i] == ' ') timer_toggle(&timer, monotonic_ns());
                }
                if (cancelled) break;
            }
        }
    }
    restore_terminal();
    if (failed) { fputs("neotimer: terminal input failed.\n", stderr); return 1; }
    if (easter_egg) return stopped ? 128 + stopped : 0;
    if (stopped || cancelled) {
        puts("neotimer: cancelled.");
        return stopped ? 128 + stopped : 0;
    }
    puts("neotimer: countdown complete.");
    char duration[32];
    format_time(seconds, duration, sizeof(duration));
    notify_completion(duration);
    return 0;
}
