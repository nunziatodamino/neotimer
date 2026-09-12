#ifndef NEOTIMER_TIMER_H
#define NEOTIMER_TIMER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define NS_PER_SECOND INT64_C(1000000000)

typedef struct {
    int64_t remaining_ns;
    int64_t last_ns;
    bool paused;
} Timer;

static bool parse_duration(const char *input, int64_t *seconds)
{
    const unsigned char *p = (const unsigned char *)input;
    int64_t value = 0;
    const int64_t limit = INT64_MAX / NS_PER_SECOND;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') {
        int digit = *p++ - '0';
        if (value > (limit - digit) / 10) return false;
        value = value * 10 + digit;
    }
    int64_t multiplier;
    switch (*p) {
    case 's': multiplier = 1; break;
    case 'm': multiplier = 60; break;
    case 'h': multiplier = 3600; break;
    default: return false;
    }
    if (p[1] != '\0' || value == 0 || value > limit / multiplier) return false;
    *seconds = value * multiplier;
    return true;
}

static void timer_update(Timer *timer, int64_t now)
{
    int64_t elapsed = now > timer->last_ns ? now - timer->last_ns : 0;
    if (!timer->paused) {
        timer->remaining_ns = elapsed >= timer->remaining_ns
            ? 0 : timer->remaining_ns - elapsed;
    }
    timer->last_ns = now;
}

static void timer_toggle(Timer *timer, int64_t now)
{
    timer_update(timer, now);
    timer->paused = !timer->paused;
}

static int64_t timer_seconds(const Timer *timer)
{
    return timer->remaining_ns / NS_PER_SECOND
        + (timer->remaining_ns % NS_PER_SECOND != 0);
}

static void format_time(int64_t seconds, char *buffer, size_t capacity)
{
    long long hours = (long long)(seconds / 3600);
    long long minutes = (long long)(seconds / 60 % 60);
    long long secs = (long long)(seconds % 60);
    if (hours > 0) snprintf(buffer, capacity, "%02lld:%02lld:%02lld", hours, minutes, secs);
    else if (minutes > 0) snprintf(buffer, capacity, "%02lld:%02lld", minutes, secs);
    else snprintf(buffer, capacity, "%02lld", secs);
}

#endif
