#include "../src/timer.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    int64_t seconds = 0;
    assert(parse_duration("45m", &seconds) && seconds == 2700);
    assert(parse_duration("30s", &seconds) && seconds == 30);
    assert(parse_duration("2h", &seconds) && seconds == 7200);
    assert(parse_duration("001s", &seconds) && seconds == 1);
    assert(parse_duration("9223372036s", &seconds) && seconds == 9223372036LL);
    const char *invalid[] = {"", "0s", "-1m", "+1s", "1.5h", "1", "s", "1S", "1ms",
        "1h30m", " 1s", "1s ", "9223372037s", "153722868m", "2562048h",
        "99999999999999999999999999999999999999999s"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(!parse_duration(invalid[i], &seconds));
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
