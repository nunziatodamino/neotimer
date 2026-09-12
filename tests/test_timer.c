#include "../src/timer.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    int64_t seconds = 0;
    const struct { char *input; int64_t expected; } valid[] = {
        {"45m", 2700}, {"30s", 30}, {"2h", 7200}, {"001s", 1},
        {"9223372036s", 9223372036LL}, {"1h40m", 6000}, {"1h 40m 30s", 6030},
        {" 1h\t40m ", 6000}, {"0h1s", 1}, {"40m1h", 6000}, {"1m1m", 120},
        {"2562047h47m16s", 9223372036LL}
    };
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        char *input[] = {valid[i].input};
        assert(parse_duration(1, input, &seconds) && seconds == valid[i].expected);
    }
    char *combined[] = {"1h", "40m", "30s"};
    assert(parse_duration(3, combined, &seconds) && seconds == 6030);
    char *zero_component[] = {"0h", "1s"};
    assert(parse_duration(2, zero_component, &seconds) && seconds == 1);
    char *overflow[] = {"9223372036s", "1s"};
    assert(!parse_duration(2, overflow, &seconds));
    assert(!parse_duration(0, NULL, &seconds));
    char *invalid[] = {"", " ", "0s", "0h0m", "-1m", "+1s", "1.5h", "1", "s", "1S", "1ms",
        "1h40", "1h-40m", "1 h", "9223372037s", "153722868m", "2562048h",
        "2562047h47m17s", "99999999999999999999999999999999999999999s"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(!parse_duration(1, &invalid[i], &seconds));
    const struct { int64_t seconds; const char *expected; } cases[] = {
        {0, "00"}, {1, "01"}, {59, "59"}, {60, "01:00"}, {61, "01:01"},
        {3599, "59:59"}, {3600, "01:00:00"}, {360000, "100:00:00"},
        {9223372036LL, "2562047:47:16"}
    };
    char buffer[32];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        format_time(cases[i].seconds, buffer, sizeof(buffer));
        assert(strcmp(buffer, cases[i].expected) == 0);
    }
    Timer timer = {2 * NS_PER_SECOND, 0, false};
    timer_update(&timer, 1);
    assert(timer_seconds(&timer) == 2);
    timer_toggle(&timer, NS_PER_SECOND);
    assert(timer.paused && timer_seconds(&timer) == 1);
    timer_update(&timer, 10 * NS_PER_SECOND);
    assert(timer.remaining_ns == NS_PER_SECOND);
    timer_toggle(&timer, 20 * NS_PER_SECOND);
    assert(!timer.paused && timer_seconds(&timer) == 1);
    timer_update(&timer, 21 * NS_PER_SECOND - 1);
    assert(timer_seconds(&timer) == 1);
    timer_update(&timer, 21 * NS_PER_SECOND);
    assert(timer_seconds(&timer) == 0);
    timer_update(&timer, 30 * NS_PER_SECOND);
    assert(timer.remaining_ns == 0);
    puts("Timer unit tests passed.");
    return 0;
}
