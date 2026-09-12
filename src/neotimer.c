#define _POSIX_C_SOURCE 200809L

#include "timer.h"
#include <errno.h>
#include <fcntl.h>
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

static const char *const digits[11][5] = {
    {" ### ", "#   #", "#   #", "#   #", " ### "},
    {"  #  ", " ##  ", "  #  ", "  #  ", "#####"},
    {" ### ", "#   #", "   # ", "  #  ", "#####"},
    {"#### ", "    #", " ### ", "    #", "#### "},
    {"#   #", "#   #", "#####", "    #", "    #"},
    {"#####", "#    ", "#### ", "    #", "#### "},
    {" ### ", "#    ", "#### ", "#   #", " ### "},
    {"#####", "    #", "   # ", "  #  ", "  #  "},
    {" ### ", "#   #", " ### ", "#   #", " ### "},
    {" ### ", "#   #", " ####", "    #", " ### "},
    {"     ", "  #  ", "     ", "  #  ", "     "}
};

static void centered(int row, int columns, const char *line, const char *color)
{
    int length = (int)strlen(line);
    int visible = length < columns - 1 ? length : columns - 1;
    if (visible < 1) return;
    int column = (columns - visible) / 2 + 1;
    printf("\033[%d;%dH%s%.*s\033[0m", row, column, color, visible, line);
}

static void render(const Timer *timer, int64_t total_ns, bool color)
{
    struct winsize size = {0};
    (void)ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);
    int columns = size.ws_col ? size.ws_col : 80;
    int rows = size.ws_row ? size.ws_row : 24;
    char time_text[32];
    format_time(timer_seconds(timer), time_text, sizeof(time_text));
    int width = (int)strlen(time_text) * 6 - 1;
    bool large = columns > width + 4 && rows >= 13;
    int height = large ? 11 : (rows >= 7 ? 5 : 1);
    int top = (rows - height) / 2 + 1;
    const char *accent = color ? (timer->paused ? "\033[33m" : "\033[36m") : "";
    const char *dim = color ? "\033[2m" : "";
    // Assemble a frame before flushing, avoiding visible partial redraws.
    fputs("\033[H\033[2J", stdout);
    if (height == 1) {
        char compact[64];
        snprintf(compact, sizeof(compact), "%s%s", time_text, timer->paused ? " [paused]" : "");
        centered(top, columns, compact, accent);
        fflush(stdout);
        return;
    }
    centered(top, columns, timer->paused ? "NEOTIMER / PAUSED" : "NEOTIMER", dim);
    if (large) {
        for (int row = 0; row < 5; ++row) {
            char line[192];
            size_t offset = 0;
            for (size_t i = 0; time_text[i]; ++i) {
                int digit = time_text[i] == ':' ? 10 : time_text[i] - '0';
                memcpy(line + offset, digits[digit][row], 5);
                offset += 5;
                if (time_text[i + 1]) line[offset++] = ' ';
            }
            line[offset] = '\0';
            centered(top + 2 + row, columns, line, accent);
        }
    } else centered(top + 1, columns, time_text, accent);
    int bar_width = columns - 8;
    if (bar_width > 42) bar_width = 42;
    if (bar_width >= 4) {
        char bar[48];
        double progress = 1.0 - (double)timer->remaining_ns / (double)total_ns;
        int filled = (int)(progress * bar_width);
        bar[0] = '[';
        for (int i = 0; i < bar_width; ++i) bar[i + 1] = i < filled ? '=' : '-';
        bar[bar_width + 1] = ']';
        bar[bar_width + 2] = '\0';
        centered(top + (large ? 8 : 3), columns, bar, accent);
    }
    centered(top + height - 1, columns, "Space pause/resume  |  Esc exit", dim);
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
    fputs("Usage: neotimer <N{s|m|h}>\n\n"
          "Start a countdown: neotimer 45m, neotimer 30s, neotimer 2h\n"
          "N must be a positive integer. Use one duration, without spaces.\n\n"
          "Space  Pause/resume\nEsc    Exit\nCtrl+C Exit\n\n"
          "Sends a silent desktop notification at completion when notify-send is available.\n"
          "Set NO_COLOR to disable colors.\n", stream);
}

int main(int argc, char **argv)
{
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        usage(stdout);
        return 0;
    }
    int64_t seconds;
    if (argc != 2 || !parse_duration(argv[1], &seconds)) {
        fputs("neotimer: expected a positive integer followed by s, m, or h (within supported range).\n", stderr);
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
        char initial[32];
        format_time(seconds, initial, sizeof(initial));
        printf("neotimer: started %s countdown.\n", initial);
        fflush(stdout);
    }
    while (!stopped) {
        timer_update(&timer, monotonic_ns());
        if (interactive) render(&timer, total_ns, color);
        if (timer.remaining_ns == 0) break;
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
                    if (keys[i] == ' ') timer_toggle(&timer, monotonic_ns());
                }
                if (cancelled) break;
            }
        }
    }
    restore_terminal();
    if (failed) { fputs("neotimer: terminal input failed.\n", stderr); return 1; }
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
