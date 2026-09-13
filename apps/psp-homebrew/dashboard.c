#include "dashboard.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A bounded recursive-descent JSON tokenizer. Subtrees use half-open ranges. */
enum { OBJECT, ARRAY, STRING, NUMBER, BOOLEAN, NIL };
typedef struct { int type, start, end, next, count; } Token;
typedef struct {
    const char *text;
    size_t length, position;
    int used;
    Token tokens[512];
} Parser;

static int hex_digit(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int unicode_escape(const char *s, size_t n, size_t *p, unsigned *code)
{
    int i, digit;
    unsigned value = 0;
    if (*p + 4 > n) return 0;
    for (i = 0; i < 4; ++i) {
        digit = hex_digit((unsigned char)s[(*p)++]);
        if (digit < 0) return 0;
        value = value * 16 + (unsigned)digit;
    }
    *code = value;
    return 1;
}

/* Decode both literal UTF-8 and JSON escapes, rejecting malformed Unicode. */
static int codepoint(const char *s, size_t n, size_t *p, unsigned *out)
{
    unsigned c, value, low, minimum;
    int remaining;
    if (*p >= n) return 0;
    c = (unsigned char)s[(*p)++];
    if (c == '\\') {
        if (*p >= n) return 0;
        c = (unsigned char)s[(*p)++];
        switch (c) {
            case '"': case '\\': case '/': *out = c; return 1;
            case 'b': *out = 8; return 1;
            case 'f': *out = 12; return 1;
            case 'n': *out = 10; return 1;
            case 'r': *out = 13; return 1;
            case 't': *out = 9; return 1;
            case 'u':
                if (!unicode_escape(s, n, p, &value)) return 0;
                if (value >= 0xd800 && value <= 0xdbff) {
                    if (*p + 2 > n || s[*p] != '\\' || s[*p + 1] != 'u') return 0;
                    *p += 2;
                    if (!unicode_escape(s, n, p, &low) || low < 0xdc00 || low > 0xdfff) return 0;
                    value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
                } else if (value >= 0xdc00 && value <= 0xdfff) return 0;
                *out = value;
                return 1;
            default: return 0;
        }
    }
    if (c < 0x20 || c == '"') return 0;
    if (c < 0x80) { *out = c; return 1; }
    if (c >= 0xc2 && c <= 0xdf) { value = c & 31; remaining = 1; minimum = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { value = c & 15; remaining = 2; minimum = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { value = c & 7; remaining = 3; minimum = 0x10000; }
    else return 0;
    while (remaining--) {
        if (*p >= n) return 0;
        c = (unsigned char)s[(*p)++];
        if ((c & 0xc0) != 0x80) return 0;
        value = (value << 6) | (c & 63);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return 0;
    *out = value;
    return 1;
}

static void whitespace(Parser *p)
{
    while (p->position < p->length && strchr(" \t\r\n", p->text[p->position])) ++p->position;
}

static int value(Parser *p, int depth)
{
    int index, child;
    Token *t;
    char c, close;
    unsigned code;
    const char *literal;
    size_t length;
    if (depth > 12 || p->used == 512) return -1;
    whitespace(p);
    if (p->position == p->length) return -1;
    index = p->used++;
    t = &p->tokens[index];
    t->start = (int)p->position;
    t->count = 0;
    c = p->text[p->position++];
    if (c == '{' || c == '[') {
        t->type = c == '{' ? OBJECT : ARRAY;
        close = c == '{' ? '}' : ']';
        whitespace(p);
        if (p->position < p->length && p->text[p->position] == close) ++p->position;
        else {
            for (;;) {
                child = value(p, depth + 1);
                if (child < 0) return -1;
                if (t->type == OBJECT) {
                    if (p->tokens[child].type != STRING) return -1;
                    whitespace(p);
                    if (p->position == p->length || p->text[p->position++] != ':') return -1;
                    if (value(p, depth + 1) < 0) return -1;
                }
                ++t->count;
                whitespace(p);
                if (p->position == p->length) return -1;
                c = p->text[p->position++];
                if (c == close) break;
                if (c != ',') return -1;
            }
        }
    } else if (c == '"') {
        t->type = STRING;
        t->start = (int)p->position;
        while (p->position < p->length && p->text[p->position] != '"') {
            if (!codepoint(p->text, p->length, &p->position, &code)) return -1;
        }
        if (p->position == p->length) return -1;
        t->end = (int)p->position++;
        t->next = p->used;
        return index;
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        t->type = NUMBER;
        p->position = (size_t)t->start;
        if (p->text[p->position] == '-') ++p->position;
        if (p->position == p->length) return -1;
        c = p->text[p->position++];
        if (c != '0') {
            if (c < '1' || c > '9') return -1;
            while (p->position < p->length && isdigit((unsigned char)p->text[p->position])) ++p->position;
        }
        if (p->position < p->length && p->text[p->position] == '.') {
            ++p->position;
            if (p->position == p->length || !isdigit((unsigned char)p->text[p->position])) return -1;
            while (p->position < p->length && isdigit((unsigned char)p->text[p->position])) ++p->position;
        }
        if (p->position < p->length && (p->text[p->position] == 'e' || p->text[p->position] == 'E')) {
            ++p->position;
            if (p->position < p->length && (p->text[p->position] == '+' || p->text[p->position] == '-')) ++p->position;
            if (p->position == p->length || !isdigit((unsigned char)p->text[p->position])) return -1;
            while (p->position < p->length && isdigit((unsigned char)p->text[p->position])) ++p->position;
        }
    } else {
        if (c == 't') { literal = "true"; t->type = BOOLEAN; }
        else if (c == 'f') { literal = "false"; t->type = BOOLEAN; }
        else if (c == 'n') { literal = "null"; t->type = NIL; }
        else return -1;
        length = strlen(literal);
        if ((size_t)t->start + length > p->length || memcmp(p->text + t->start, literal, length)) return -1;
        p->position = (size_t)t->start + length;
    }
    t->end = (int)p->position;
    t->next = p->used;
    return index;
}

static int string_value(Parser *p, int index, char *out, size_t capacity)
{
    Token *t;
    size_t pos, used = 0;
    unsigned code;
    unsigned char bytes[4];
    int n;
    if (index < 0 || p->tokens[index].type != STRING) return 0;
    t = &p->tokens[index];
    pos = (size_t)t->start;
    while (pos < (size_t)t->end) {
        if (!codepoint(p->text, (size_t)t->end, &pos, &code)) return 0;
        if (code < 32 || (code >= 127 && code <= 159)) return 0;
        if (code < 0x80) { n = 1; bytes[0] = (unsigned char)code; }
        else if (code < 0x800) { n = 2; bytes[0] = 0xc0 | (code >> 6); bytes[1] = 0x80 | (code & 63); }
        else if (code < 0x10000) {
            n = 3; bytes[0] = 0xe0 | (code >> 12); bytes[1] = 0x80 | ((code >> 6) & 63); bytes[2] = 0x80 | (code & 63);
        } else {
            n = 4; bytes[0] = 0xf0 | (code >> 18); bytes[1] = 0x80 | ((code >> 12) & 63);
            bytes[2] = 0x80 | ((code >> 6) & 63); bytes[3] = 0x80 | (code & 63);
        }
        if (used + (size_t)n >= capacity) return 0;
        memcpy(out + used, bytes, (size_t)n);
        used += (size_t)n;
    }
    out[used] = 0;
    return 1;
}

/* Exact field sets reject duplicate, unknown and missing members, including escaped keys. */
static int fields(Parser *p, int object, const char *const *names, int count, int *indices)
{
    int i, j, key;
    char name[40];
    if (object < 0 || p->tokens[object].type != OBJECT || p->tokens[object].count != count) return 0;
    for (i = 0; i < count; ++i) indices[i] = -1;
    key = object + 1;
    for (i = 0; i < count; ++i) {
        if (!string_value(p, key, name, sizeof(name))) return 0;
        for (j = 0; j < count && strcmp(names[j], name); ++j) {}
        if (j == count || indices[j] >= 0) return 0;
        indices[j] = key + 1;
        key = p->tokens[key + 1].next;
    }
    return 1;
}

static int integer(Parser *p, int index, int64_t maximum, int64_t *out)
{
    Token *t = &p->tokens[index];
    int i;
    int64_t result = 0;
    if (t->type != NUMBER) return 0;
    for (i = t->start; i < t->end; ++i) {
        int digit = p->text[i] - '0';
        if (digit < 0 || digit > 9 || result > (maximum - digit) / 10 || digit > maximum) return 0;
        result = result * 10 + digit;
    }
    *out = result;
    return 1;
}

static int timestamp(Parser *p, int index, int64_t *out)
{
    /* Last second of 2099 UTC; explicit 64-bit arithmetic also works on PSP. */
    return integer(p, index, INT64_C(4102444799), out);
}

static int boolean(Parser *p, int index, int *out)
{
    if (p->tokens[index].type != BOOLEAN) return 0;
    *out = p->text[p->tokens[index].start] == 't';
    return 1;
}

static int leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
static int month_days(int year, int month)
{
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month - 1] + (month == 2 && leap(year));
}

static int date(Parser *p, int index, char *out)
{
    int y, m, d, i;
    if (!string_value(p, index, out, 11) || strlen(out) != 10 || out[4] != '-' || out[7] != '-') return 0;
    for (i = 0; i < 10; ++i) if (i != 4 && i != 7 && !isdigit((unsigned char)out[i])) return 0;
    y = atoi(out); m = atoi(out + 5); d = atoi(out + 8);
    return y >= 2000 && y <= 2099 && m >= 1 && m <= 12 && d >= 1 && d <= month_days(y, m);
}

static int slot(Parser *p, int index, WeatherSlot *out)
{
    static const char *const names[] = {"max", "code", "label"};
    int f[3], length;
    int64_t code;
    char number[48], *end;
    if (!fields(p, index, names, 3, f) || p->tokens[f[0]].type != NUMBER) return 0;
    length = p->tokens[f[0]].end - p->tokens[f[0]].start;
    if (length >= (int)sizeof(number)) return 0;
    memcpy(number, p->text + p->tokens[f[0]].start, (size_t)length);
    number[length] = 0;
    out->max = strtod(number, &end);
    if (*end || !isfinite(out->max) || out->max < -100 || out->max > 100) return 0;
    if (!integer(p, f[1], 99, &code)) return 0;
    out->code = (int)code;
    return string_value(p, f[2], out->label, sizeof(out->label)) && out->label[0];
}

int dashboard_parse(const char *json, size_t length, Dashboard *out)
{
    static const char *const root_names[] = {"version", "generatedAt", "time", "weather", "content", "contentSync"};
    static const char *const time_names[] = {"unix", "timezone", "synced"};
    static const char *const sync_names[] = {"fetchedAt", "stale"};
    static const char *const weather_names[] = {"date", "morning", "evening", "fetchedAt", "stale"};
    static const char *const content_names[] = {"version", "updatedAt", "shopping", "agenda"};
    static const char *const event_names[] = {"title", "startDate", "endDate", "time"};
    Parser p;
    Dashboard d;
    int root[6], f[5], e[4], index, i;
    int64_t version;
    char zone[32];
    if (!json || !out || !length || length > DASHBOARD_BODY_LIMIT || memchr(json, 0, length)) return 0;
    memset(&p, 0, sizeof(p));
    memset(&d, 0, sizeof(d));
    p.text = json; p.length = length;
    if (value(&p, 0) != 0) return 0;
    whitespace(&p);
    if (p.position != length || !fields(&p, 0, root_names, 6, root)) return 0;
    if (!integer(&p, root[0], 1, &version) || version != 1 || !timestamp(&p, root[1], &d.generated_at)) return 0;
    if (!fields(&p, root[2], time_names, 3, f) || !timestamp(&p, f[0], &d.unix_time)) return 0;
    if (!string_value(&p, f[1], zone, sizeof(zone)) || strcmp(zone, "Europe/Paris")) return 0;
    d.synced = -1;
    if (p.tokens[f[2]].type != NIL && !boolean(&p, f[2], &d.synced)) return 0;
    if (!fields(&p, root[5], sync_names, 2, f) || !boolean(&p, f[1], &d.content_stale)) return 0;
    d.content_fetched_at = -1;
    if (p.tokens[f[0]].type != NIL && !timestamp(&p, f[0], &d.content_fetched_at)) return 0;
    if (p.tokens[root[3]].type != NIL) {
        if (!fields(&p, root[3], weather_names, 5, f) || !date(&p, f[0], d.weather_date)) return 0;
        if (!slot(&p, f[1], &d.morning) || !slot(&p, f[2], &d.evening)) return 0;
        if (!timestamp(&p, f[3], &d.weather_fetched_at) || !boolean(&p, f[4], &d.weather_stale)) return 0;
        d.has_weather = 1;
    }
    if (p.tokens[root[4]].type != NIL) {
        if (!fields(&p, root[4], content_names, 4, f) || !integer(&p, f[0], 1, &version) || version != 1) return 0;
        if (!timestamp(&p, f[1], &d.published_at)) return 0;
        if (p.tokens[f[2]].type != ARRAY || p.tokens[f[2]].count > DASHBOARD_SHOPPING_LIMIT) return 0;
        d.shopping_count = p.tokens[f[2]].count;
        index = f[2] + 1;
        for (i = 0; i < d.shopping_count; ++i) {
            if (!string_value(&p, index, d.shopping[i], sizeof(d.shopping[i])) || !d.shopping[i][0]) return 0;
            index = p.tokens[index].next;
        }
        if (p.tokens[f[3]].type != ARRAY || p.tokens[f[3]].count > DASHBOARD_AGENDA_LIMIT) return 0;
        d.agenda_count = p.tokens[f[3]].count;
        index = f[3] + 1;
        for (i = 0; i < d.agenda_count; ++i) {
            AgendaEvent *event = &d.agenda[i];
            if (!fields(&p, index, event_names, 4, e)) return 0;
            if (!string_value(&p, e[0], event->title, sizeof(event->title)) || !event->title[0]) return 0;
            if (!date(&p, e[1], event->start)) return 0;
            if (p.tokens[e[2]].type != NIL && (!date(&p, e[2], event->end) || strcmp(event->end, event->start) < 0)) return 0;
            if (p.tokens[e[3]].type != NIL) {
                if (!string_value(&p, e[3], event->time, sizeof(event->time)) || strlen(event->time) != 5) return 0;
                if (!isdigit((unsigned char)event->time[0]) || !isdigit((unsigned char)event->time[1]) || event->time[2] != ':' ||
                    !isdigit((unsigned char)event->time[3]) || !isdigit((unsigned char)event->time[4]) || atoi(event->time) > 23 || atoi(event->time + 3) > 59) return 0;
            }
            index = p.tokens[index].next;
        }
        d.has_content = 1;
    }
    if (d.has_content != (d.content_fetched_at >= 0) || (!d.has_content && !d.content_stale)) return 0;
    *out = d;
    return 1;
}

static int equal_case(const char *a, size_t n, const char *b)
{
    size_t i;
    if (strlen(b) != n) return 0;
    for (i = 0; i < n; ++i) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}

int dashboard_http_headers(const char *h, size_t length, size_t *body_length)
{
    size_t pos = 0, end, colon, start, stop, size = 0, i;
    int has_length = 0, has_type = 0;
    if (!h || !body_length || length < 19 || length > DASHBOARD_HEADER_LIMIT || memchr(h, 0, length)) return 0;
    if (memcmp(h, "HTTP/1.0 200", 12) && memcmp(h, "HTTP/1.1 200", 12)) return 0;
    if (h[12] != ' ' && h[12] != '\r') return 0;
    while (pos + 1 < length && !(h[pos] == '\r' && h[pos + 1] == '\n')) ++pos;
    if (pos + 1 >= length) return 0;
    pos += 2;
    while (pos + 1 < length) {
        if (h[pos] == '\r' && h[pos + 1] == '\n') {
            if (pos + 2 != length || !has_length || !has_type || !size) return 0;
            *body_length = size;
            return 1;
        }
        end = pos;
        while (end + 1 < length && !(h[end] == '\r' && h[end + 1] == '\n')) ++end;
        if (end + 1 >= length) return 0;
        colon = pos;
        while (colon < end && h[colon] != ':') {
            unsigned char c = (unsigned char)h[colon];
            if (!isalnum(c) && !strchr("!#$%&'*+-.^_`|~", c)) return 0;
            ++colon;
        }
        if (colon == pos || colon == end) return 0;
        start = colon + 1;
        while (start < end && (h[start] == ' ' || h[start] == '\t')) ++start;
        stop = end;
        while (stop > start && (h[stop - 1] == ' ' || h[stop - 1] == '\t')) --stop;
        for (i = start; i < stop; ++i) if ((unsigned char)h[i] < 32 && h[i] != '\t') return 0;
        if (equal_case(h + pos, colon - pos, "content-length")) {
            if (has_length++ || start == stop) return 0;
            for (i = start; i < stop; ++i) {
                if (h[i] < '0' || h[i] > '9' || size > DASHBOARD_BODY_LIMIT / 10) return 0;
                size = size * 10 + (size_t)(h[i] - '0');
                if (size > DASHBOARD_BODY_LIMIT) return 0;
            }
        } else if (equal_case(h + pos, colon - pos, "content-type")) {
            size_t media_end = start;
            if (has_type++) return 0;
            while (media_end < stop && h[media_end] != ';' && h[media_end] != ' ') ++media_end;
            if (!equal_case(h + start, media_end - start, "application/json")) return 0;
        } else if (equal_case(h + pos, colon - pos, "transfer-encoding")) return 0;
        else if (equal_case(h + pos, colon - pos, "content-encoding") && !equal_case(h + start, stop - start, "identity")) return 0;
        pos = end + 2;
    }
    return 0;
}

static int64_t days_before(int year, int month, int day)
{
    int y, m;
    int64_t days = day - 1;
    for (y = 1970; y < year; ++y) days += 365 + leap(y);
    for (m = 1; m < month; ++m) days += month_days(year, m);
    return days;
}

static void civil_time(int64_t unix_time, ParisTime *out)
{
    int64_t days = unix_time / 86400;
    int remaining = (int)(unix_time % 86400);
    out->weekday = (int)((days + 4) % 7);
    out->year = 1970;
    while (days >= 365 + leap(out->year)) days -= 365 + leap(out->year++);
    out->month = 1;
    while (days >= month_days(out->year, out->month)) days -= month_days(out->year, out->month++);
    out->day = (int)days + 1;
    out->hour = remaining / 3600;
    out->minute = remaining / 60 % 60;
    out->second = remaining % 60;
}

void dashboard_paris_time(int64_t unix_time, ParisTime *out)
{
    ParisTime utc;
    int march_sunday, october_sunday;
    int64_t start, end;
    if (unix_time < 0) unix_time = 0;
    civil_time(unix_time, &utc);
    march_sunday = 31 - (int)((days_before(utc.year, 3, 31) + 4) % 7);
    october_sunday = 31 - (int)((days_before(utc.year, 10, 31) + 4) % 7);
    start = days_before(utc.year, 3, march_sunday) * 86400 + 3600;
    end = days_before(utc.year, 10, october_sunday) * 86400 + 3600;
    civil_time(unix_time + ((unix_time >= start && unix_time < end) ? 7200 : 3600), out);
}

void dashboard_ascii(const char *utf8, char *out, size_t capacity)
{
    size_t p = 0, used = 0, length = strlen(utf8);
    unsigned c;
    const char *replacement;
    if (!capacity) return;
    while (p < length && used + 1 < capacity) {
        /* Model strings are decoded JSON; a literal backslash is ordinary text here. */
        if (utf8[p] == '\\' || utf8[p] == '"') c = (unsigned char)utf8[p++];
        else if (!codepoint(utf8, length, &p, &c)) c = '?';
        replacement = NULL;
        if (c >= 0xc0 && c <= 0xc5) c = 'A';
        else if (c >= 0xe0 && c <= 0xe5) c = 'a';
        else if (c == 0xc7) c = 'C'; else if (c == 0xe7) c = 'c';
        else if (c >= 0xc8 && c <= 0xcb) c = 'E'; else if (c >= 0xe8 && c <= 0xeb) c = 'e';
        else if (c >= 0xcc && c <= 0xcf) c = 'I'; else if (c >= 0xec && c <= 0xef) c = 'i';
        else if (c == 0xd1) c = 'N'; else if (c == 0xf1) c = 'n';
        else if ((c >= 0xd2 && c <= 0xd6) || c == 0xd8) c = 'O';
        else if ((c >= 0xf2 && c <= 0xf6) || c == 0xf8) c = 'o';
        else if (c >= 0xd9 && c <= 0xdc) c = 'U'; else if (c >= 0xf9 && c <= 0xfc) c = 'u';
        else if (c == 0xfd || c == 0xff) c = 'y'; else if (c == 0xdd || c == 0x178) c = 'Y';
        else if (c == 0x153) replacement = "oe"; else if (c == 0x152) replacement = "OE";
        else if (c == 0xe6) replacement = "ae"; else if (c == 0xc6) replacement = "AE";
        else if (c == 0x2019 || c == 0x2018) c = '\'';
        else if (c == 0x201c || c == 0x201d || c == 0xab || c == 0xbb) c = '"';
        else if (c == 0x2013 || c == 0x2014) c = '-';
        else if (c == 0xa0 || c == 0x202f) c = ' ';
        else if (c == 0x2026) replacement = "...";
        else if (c >= 0x300 && c <= 0x36f) continue;
        if (replacement) {
            while (*replacement && used + 1 < capacity) out[used++] = *replacement++;
        } else out[used++] = c >= 32 && c < 127 ? (char)c : '?';
    }
    out[used] = 0;
}

void dashboard_demo(Dashboard *out)
{
    static const char *const items[] = {
        "Lait", "Pain complet", "Tomates", "Caf\303\251 moulu", "Pommes de terre",
        "Huile d'olive vierge extra et vinaigre balsamique", "Yaourts nature", "Fromage",
        "P\303\242tes", "Riz", "Oeufs", "Chocolat noir", "Farine", "Sucre", "Beurre",
        "Carottes", "Courgettes", "Savon", "Lessive", "Papier cuisson"
    };
    int i;
    memset(out, 0, sizeof(*out));
    out->unix_time = out->generated_at = INT64_C(1789300800);
    out->synced = 1;
    out->has_content = out->has_weather = 1;
    out->content_fetched_at = out->weather_fetched_at = out->published_at = out->unix_time;
    strcpy(out->weather_date, "2026-09-13");
    out->morning.max = 23.4; out->morning.code = 3; strcpy(out->morning.label, "Couvert");
    out->evening.max = 28.1; out->evening.code = 61; strcpy(out->evening.label, "Pluvieux");
    out->shopping_count = 20;
    for (i = 0; i < 20; ++i) strcpy(out->shopping[i], items[i]);
    out->agenda_count = 3;
    strcpy(out->agenda[0].title, "Week-end en famille avec toute la famille");
    strcpy(out->agenda[0].start, "2026-09-12"); strcpy(out->agenda[0].end, "2026-09-14");
    strcpy(out->agenda[0].time, "10:00");
    strcpy(out->agenda[1].title, "Dentiste"); strcpy(out->agenda[1].start, "2026-09-23"); strcpy(out->agenda[1].time, "18:30");
    strcpy(out->agenda[2].title, "Anniversaire de L\303\251a"); strcpy(out->agenda[2].start, "2026-10-01");
}
