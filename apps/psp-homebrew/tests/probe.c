#include "dashboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    char input[DASHBOARD_BODY_LIMIT + 2], ascii[200];
    size_t length, body_length = 12345;
    Dashboard result, previous;
    ParisTime time;
    int i;
    if (argc != 2) return 2;
    length = fread(input, 1, sizeof(input), stdin);
    if (!strcmp(argv[1], "http")) {
        if (!dashboard_http_headers(input, length, &body_length)) return body_length == 12345 ? 1 : 3;
        printf("%zu\n", body_length);
        return 0;
    }
    if (!strcmp(argv[1], "time")) {
        if (length >= sizeof(input)) return 2;
        input[length] = 0;
        dashboard_paris_time(strtoll(input, NULL, 10), &time);
        printf("%04d-%02d-%02d %02d:%02d:%02d %d\n", time.year, time.month, time.day, time.hour, time.minute, time.second, time.weekday);
        return 0;
    }
    memset(&result, 0x5a, sizeof(result));
    memcpy(&previous, &result, sizeof(result));
    if (!dashboard_parse(input, length, &result)) return !memcmp(&result, &previous, sizeof(result)) ? 1 : 3;
    printf("%d %d %d %d %d %.1f %.1f\n", result.has_content, result.has_weather, result.shopping_count, result.agenda_count,
           result.synced, result.morning.max, result.evening.max);
    for (i = 0; i < result.shopping_count; ++i) {
        dashboard_ascii(result.shopping[i], ascii, sizeof(ascii));
        puts(ascii);
    }
    for (i = 0; i < result.agenda_count; ++i) {
        dashboard_ascii(result.agenda[i].title, ascii, sizeof(ascii));
        puts(ascii);
        printf("%s|%s|%s\n", result.agenda[i].start, result.agenda[i].end, result.agenda[i].time);
    }
    return 0;
}
