#ifndef INFHOME_DASHBOARD_H
#define INFHOME_DASHBOARD_H

#include <stddef.h>
#include <stdint.h>

#define DASHBOARD_BODY_LIMIT 8192
#define DASHBOARD_HEADER_LIMIT 2048
#define DASHBOARD_SHOPPING_LIMIT 20
#define DASHBOARD_AGENDA_LIMIT 3

typedef struct {
    char title[97];
    char start[11];
    char end[11];
    char time[6];
} AgendaEvent;

typedef struct {
    double max;
    int code;
    char label[65];
} WeatherSlot;

typedef struct {
    int64_t unix_time;
    int64_t generated_at;
    int synced; /* -1: unknown, 0: unsynchronized, 1: synchronized */
    int has_content;
    int64_t published_at;
    int64_t content_fetched_at;
    int content_stale;
    int shopping_count;
    char shopping[DASHBOARD_SHOPPING_LIMIT][65];
    int agenda_count;
    AgendaEvent agenda[DASHBOARD_AGENDA_LIMIT];
    int has_weather;
    char weather_date[11];
    WeatherSlot morning;
    WeatherSlot evening;
    int64_t weather_fetched_at;
    int weather_stale;
} Dashboard;

typedef struct {
    int year, month, day, hour, minute, second, weekday;
} ParisTime;

/* All parsers leave output unchanged on failure. Text remains UTF-8 in the model. */
int dashboard_parse(const char *json, size_t length, Dashboard *out);
int dashboard_http_headers(const char *headers, size_t length, size_t *body_length);
void dashboard_paris_time(int64_t unix_time, ParisTime *out);
void dashboard_ascii(const char *utf8, char *out, size_t capacity);
void dashboard_demo(Dashboard *out);

#endif
