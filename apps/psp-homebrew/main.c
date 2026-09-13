#include <pspuser.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspkernel.h>
#include <psppower.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dashboard.h"
#include "network.h"

PSP_MODULE_INFO("Infhome", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(1024);

#define COLOR_BG 0xFF17120F
#define COLOR_LINE 0xFF5B4938
#define COLOR_TEXT 0xFFE6D8C5
#define COLOR_MUTED 0xFF9F8D79
#define COLOR_AMBER 0xFF49A7D7
#define COLOR_GREEN 0xFF79BC8A
#define COLOR_RED 0xFF6666C8
#define COLOR_SQUARE 0xFFDE78D4
#define COLOR_TRIANGLE 0xFF6FC477
#define COLOR_CIRCLE 0xFF625DD8
#define COLOR_CROSS 0xFFD88D4F
#define FRAME_BYTES (512 * 272 * 4)
#define CONTENT_WIDTH 42
#define SHOPPING_PER_PAGE 6

typedef enum { PAGE_HOME, PAGE_SHOPPING, PAGE_AGENDA, PAGE_SYSTEM } Page;
static Page current_page = PAGE_HOME;
static int demo_mode, shopping_page, back_buffer = 1;
static volatile int running = 1;
static u32 *pixels;
static NetworkState network;
static Dashboard demo;
static uint64_t demo_received_us;

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1; (void)arg2; (void)common;
    running = 0;
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    int callback;
    (void)args; (void)argp;
    callback = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    if (callback >= 0) sceKernelRegisterExitCallback(callback);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thread = sceKernelCreateThread("callback_thread", callback_thread, 0x11, 0x1000, 0, NULL);
    if (thread >= 0) sceKernelStartThread(thread, 0, NULL);
}

static void text(int x, int y, u32 color, const char *value)
{
    char safe[69];
    size_t length;
    int width = 68 - x;
    if (x < 0 || width <= 0 || y < 0 || y >= 34) return;
    length = strlen(value);
    if (length > (size_t)width) length = (size_t)width;
    memcpy(safe, value, length); safe[length] = 0;
    pspDebugScreenSetXY(x, y);
    pspDebugScreenSetTextColor(color);
    pspDebugScreenPrintf("%s", safe);
}

static void rectangle(int x, int y, int width, int height, u32 color)
{
    int row, column;
    for (row = y; row < y + height && row < 272; ++row)
        for (column = x; column < x + width && column < 480; ++column)
            if (row >= 0 && column >= 0) pixels[row * 512 + column] = color;
}

static void button(int y, Page page)
{
    int i;
    int x = 10, top = y * 8;
    if (page == PAGE_HOME) {
        rectangle(x, top, 10, 1, COLOR_SQUARE); rectangle(x, top + 9, 10, 1, COLOR_SQUARE);
        rectangle(x, top, 1, 10, COLOR_SQUARE); rectangle(x + 9, top, 1, 10, COLOR_SQUARE);
    } else if (page == PAGE_SHOPPING) {
        for (i = 0; i < 9; ++i) {
            rectangle(x + 4 - i / 2, top + i, 1, 1, COLOR_TRIANGLE);
            rectangle(x + 5 + i / 2, top + i, 1, 1, COLOR_TRIANGLE);
        }
        rectangle(x, top + 8, 10, 1, COLOR_TRIANGLE);
    } else {
        rectangle(x + 3, top, 4, 1, COLOR_CIRCLE); rectangle(x + 3, top + 9, 4, 1, COLOR_CIRCLE);
        rectangle(x, top + 3, 1, 4, COLOR_CIRCLE); rectangle(x + 9, top + 3, 1, 4, COLOR_CIRCLE);
        for (i = 1; i < 3; ++i) {
            rectangle(x + 3 - i, top + i, 1, 1, COLOR_CIRCLE); rectangle(x + 6 + i, top + i, 1, 1, COLOR_CIRCLE);
            rectangle(x + 3 - i, top + 9 - i, 1, 1, COLOR_CIRCLE); rectangle(x + 6 + i, top + 9 - i, 1, 1, COLOR_CIRCLE);
        }
    }
}

static void menu(void)
{
    static const char *const labels[] = {"Accueil", "Courses", "Agenda"};
    int i;
    rectangle(147, 32, 1, 240, COLOR_LINE);
    text(1, 5, COLOR_MUTED, "PAGES");
    for (i = 0; i < 3; ++i) {
        button(8 + i * 4, (Page)i);
        text(5, 8 + i * 4, current_page == (Page)i ? COLOR_AMBER : COLOR_MUTED, labels[i]);
        if (current_page == (Page)i) text(18, 8 + i * 4, COLOR_AMBER, "<");
    }
    rectangle(0, 214, 147, 1, COLOR_LINE);
    for (i = 0; i < 9; ++i) {
        rectangle(10 + i, 225 + i, 1, 1, COLOR_CROSS);
        rectangle(18 - i, 225 + i, 1, 1, COLOR_CROSS);
    }
    text(4, 28, COLOR_MUTED, "Actualiser");
    text(1, 31, COLOR_MUTED, "SELECT  Reel/Demo");
}

static int wrap(const char *utf8, char lines[][CONTENT_WIDTH + 1], int maximum)
{
    char ascii[200];
    size_t pos = 0, length, take, split;
    int count = 0;
    dashboard_ascii(utf8, ascii, sizeof(ascii));
    length = strlen(ascii);
    while (pos < length && count < maximum) {
        take = length - pos;
        if (take > CONTENT_WIDTH) {
            take = CONTENT_WIDTH;
            split = take;
            while (split > 0 && ascii[pos + split] != ' ') --split;
            if (split > 0 && length - pos - split <= (size_t)(maximum - count - 1) * CONTENT_WIDTH)
                take = split;
        }
        memcpy(lines[count], ascii + pos, take); lines[count][take] = 0;
        ++count;
        pos += take;
        while (ascii[pos] == ' ') ++pos;
    }
    return count;
}

static void wrapped(int y, const char *utf8, int maximum, u32 color)
{
    char lines[4][CONTENT_WIDTH + 1];
    int i, count = wrap(utf8, lines, maximum > 4 ? 4 : maximum);
    for (i = 0; i < count; ++i) text(24, y + i, color, lines[i]);
}

static void large_clock(int hour, int minute, int known)
{
    static const unsigned char digits[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}
    };
    char clock[6];
    int i, row, column, x = 168;
    if (known) snprintf(clock, sizeof(clock), "%02d:%02d", hour, minute);
    else strcpy(clock, "--:--");
    for (i = 0; i < 5; ++i) {
        if (clock[i] == ':') {
            rectangle(x + 5, 70, 5, 5, COLOR_AMBER); rectangle(x + 5, 85, 5, 5, COLOR_AMBER);
            x += 20;
        } else {
            if (clock[i] == '-') rectangle(x, 80, 25, 5, COLOR_TEXT);
            else for (row = 0; row < 7; ++row) for (column = 0; column < 5; ++column)
                if (digits[clock[i] - '0'][row] & (1 << (4 - column)))
                    rectangle(x + column * 5, 60 + row * 5, 5, 5, COLOR_TEXT);
            x += 35;
        }
    }
}

static int64_t current_unix(const Dashboard *d, uint64_t received, uint64_t now)
{
    return d->unix_time + (int64_t)((now >= received ? now - received : 0) / UINT64_C(1000000));
}

static int old_content(const Dashboard *d, int64_t now)
{
    return !d->has_content || d->content_stale || d->content_fetched_at > now || now - d->content_fetched_at > 1800;
}

static int old_weather(const Dashboard *d, int64_t now, const ParisTime *time)
{
    char date[16];
    snprintf(date, sizeof(date), "%04d-%02d-%02d", time->year, time->month, time->day);
    return !d->has_weather || d->weather_stale || d->weather_fetched_at > now ||
        now - d->weather_fetched_at > 1800 || strcmp(date, d->weather_date);
}

static void short_date(const char *date, char *out, size_t capacity)
{
    snprintf(out, capacity, "%.2s/%.2s/%.4s", date + 8, date + 5, date);
}

static void home(const Dashboard *d, int known, const ParisTime *time, int64_t now)
{
    static const char *const weekdays[] = {"Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"};
    char line[80], morning[80], evening[80], date[16];
    int stale;
    text(24, 5, COLOR_MUTED, "MAISON / TOULOUSE");
    large_clock(time->hour, time->minute, known);
    if (known) {
        snprintf(line, sizeof(line), "%s %02d/%02d/%04d", weekdays[time->weekday], time->day, time->month, time->year);
        text(24, 13, COLOR_TEXT, line);
        text(24, 15, d->synced == 1 ? COLOR_MUTED : COLOR_AMBER,
             d->synced == 1 ? "Heure de Paris" : d->synced == 0 ? "Heure non synchronisee" : "Synchronisation NTP inconnue");
    } else text(24, 13, COLOR_MUTED, "En attente de l'heure Raspberry");
    rectangle(168, 142, 294, 1, COLOR_LINE);
    if (!known || !d->has_weather) {
        text(24, 19, COLOR_MUTED, "METEO");
        text(24, 22, COLOR_AMBER, "Previsions indisponibles");
    } else {
        stale = old_weather(d, now, time);
        if (stale) {
            short_date(d->weather_date, date, sizeof(date));
            snprintf(line, sizeof(line), "METEO DU %s / ANCIENNE", date);
        } else strcpy(line, "METEO / AUJOURD'HUI");
        text(24, 19, stale ? COLOR_AMBER : COLOR_MUTED, line);
        snprintf(morning, sizeof(morning), "Matin       max %.1f C", d->morning.max);
        snprintf(evening, sizeof(evening), "Apres-midi/soir  max %.1f C", d->evening.max);
        text(24, 21, COLOR_TEXT, morning);
        wrapped(22, d->morning.label, 2, COLOR_MUTED);
        text(24, 25, COLOR_TEXT, evening);
        wrapped(26, d->evening.label, 2, COLOR_MUTED);
    }
    if (known && d->has_content) {
        snprintf(line, sizeof(line), "%d articles / %d evenements%s", d->shopping_count, d->agenda_count,
                 old_content(d, now) ? " / anciens" : "");
        text(24, 30, old_content(d, now) ? COLOR_AMBER : COLOR_MUTED, line);
    } else text(24, 30, COLOR_AMBER, "Courses et agenda indisponibles");
}

static void shopping(const Dashboard *d, int known, int64_t now)
{
    int i, first, pages;
    char line[64];
    text(24, 5, COLOR_MUTED, "A ACHETER / LECTURE SEULE");
    if (!known || !d->has_content) { text(24, 10, COLOR_AMBER, "Liste indisponible"); return; }
    if (!d->shopping_count) { text(24, 10, COLOR_TEXT, "Rien a acheter"); }
    pages = (d->shopping_count + SHOPPING_PER_PAGE - 1) / SHOPPING_PER_PAGE;
    if (pages < 1) pages = 1;
    if (shopping_page >= pages) shopping_page = pages - 1;
    first = shopping_page * SHOPPING_PER_PAGE;
    for (i = first; i < d->shopping_count && i < first + SHOPPING_PER_PAGE; ++i) {
        /* Two full-width lines per article, plus a separator line. */
        wrapped(9 + (i - first) * 3, d->shopping[i], 2, COLOR_TEXT);
    }
    snprintf(line, sizeof(line), "%d articles / Page %d/%d", d->shopping_count, shopping_page + 1, pages);
    text(24, 29, COLOR_MUTED, line);
    if (pages > 1) text(24, 31, COLOR_AMBER, "< GAUCHE   /   DROITE >");
    if (old_content(d, now)) text(24, 7, COLOR_AMBER, "Derniere liste connue / ancienne");
}

static void agenda(const Dashboard *d, int known, int64_t now)
{
    int i, y;
    char start[16], end[16], line[64];
    text(24, 5, COLOR_MUTED, "PROCHAINS EVENEMENTS");
    if (!known || !d->has_content) { text(24, 10, COLOR_AMBER, "Agenda indisponible"); return; }
    if (old_content(d, now)) text(24, 7, COLOR_AMBER, "Dernier agenda connu / ancien");
    if (!d->agenda_count) { text(24, 10, COLOR_TEXT, "Aucun evenement a venir"); return; }
    for (i = 0; i < d->agenda_count; ++i) {
        const AgendaEvent *event = &d->agenda[i];
        y = 9 + i * 7;
        short_date(event->start, start, sizeof(start));
        if (event->end[0]) {
            short_date(event->end, end, sizeof(end));
            snprintf(line, sizeof(line), "%s - %s", start, end);
        } else snprintf(line, sizeof(line), "%s", start);
        text(24, y, COLOR_AMBER, line);
        wrapped(y + 1, event->title, 3, COLOR_TEXT);
        if (event->time[0]) snprintf(line, sizeof(line), "%s%s", event->time, event->end[0] ? " (premier jour)" : "");
        else strcpy(line, "Sans horaire");
        text(24, y + 4, COLOR_MUTED, line);
    }
    snprintf(line, sizeof(line), "%d evenements", d->agenda_count);
    text(24, 31, COLOR_MUTED, line);
}

static void system_page(const Dashboard *d, int known, int64_t now, uint64_t received, uint64_t ticks, const ParisTime *time)
{
    char line[80];
    uint64_t age = known ? (ticks - received) / UINT64_C(1000000) : 0;
    text(3, 5, COLOR_MUTED, "STATUT SYSTEME");
    text(3, 8, COLOR_MUTED, "WIFI"); text(21, 8, network.wifi ? COLOR_GREEN : COLOR_RED, network.wifi ? "CONNECTE" : "DECONNECTE");
    text(3, 10, COLOR_MUTED, "IP PSP"); text(21, 10, COLOR_TEXT, network.ip);
    text(3, 12, COLOR_MUTED, "RASPBERRY"); text(21, 12, COLOR_TEXT, INFHOME_PI_IP ":8080");
    text(3, 14, COLOR_MUTED, "API"); text(21, 14, COLOR_TEXT, demo_mode ? "Simulation locale" : network.status);
    text(3, 17, COLOR_MUTED, "DERNIER SUCCES");
    if (known) snprintf(line, sizeof(line), "Il y a %llu s%s", (unsigned long long)age, demo_mode ? " (demo)" : "");
    else strcpy(line, "Aucun");
    text(21, 17, COLOR_TEXT, line);
    text(3, 19, COLOR_MUTED, "NTP");
    text(21, 19, known && d->synced == 1 ? COLOR_GREEN : COLOR_AMBER,
         !known || d->synced < 0 ? "INCONNU" : d->synced ? "SYNCHRONISE" : "NON SYNCHRONISE");
    text(3, 21, COLOR_MUTED, "CONTENU");
    text(21, 21, COLOR_TEXT, !known || !d->has_content ? "INDISPONIBLE" : old_content(d, now) ? "ANCIEN" : "A JOUR");
    text(3, 23, COLOR_MUTED, "METEO");
    text(21, 23, COLOR_TEXT, !known || !d->has_weather ? "INDISPONIBLE" : old_weather(d, now, time) ? "ANCIENNE" : "A JOUR");
    text(3, 26, COLOR_MUTED, "Actualisation 60 s / reprise sur erreur 10 s");
    text(3, 29, COLOR_MUTED, "HAUT: ACCUEIL   CROIX: ACTUALISER   SELECT: MODE");
}

static void draw(uint64_t ticks)
{
    const Dashboard *d = demo_mode ? &demo : &network.dashboard;
    uint64_t received = demo_mode ? demo_received_us : network.received_us;
    int known = demo_mode || network.has_dashboard;
    int64_t now = known ? current_unix(d, received, ticks) : 0;
    ParisTime time = {0};
    const char *status;
    u32 color;
    if (known) dashboard_paris_time(now, &time);
    /* Match pspDebugScreen's uncached VRAM alias for both text and geometry. */
    pixels = (u32 *)(((uintptr_t)sceGeEdramGetAddr() | 0x40000000u) + back_buffer * FRAME_BYTES);
    pspDebugScreenSetOffset(back_buffer * FRAME_BYTES);
    pspDebugScreenSetBackColor(COLOR_BG);
    pspDebugScreenClear();
    if (demo_mode) { status = "DEMO"; color = COLOR_AMBER; }
    else if (network.busy) { status = "ACTUALISATION"; color = COLOR_AMBER; }
    else if (!network.online) { status = "HORS LIGNE"; color = COLOR_RED; }
    else if (!known || old_content(d, now) || old_weather(d, now, &time)) { status = "DONNEES PARTIELLES"; color = COLOR_AMBER; }
    else { status = "EN LIGNE"; color = COLOR_GREEN; }
    text(2, 1, COLOR_AMBER, "INFHOME");
    text(67 - (int)strlen(status), 1, color, status);
    rectangle(0, 30, 480, 1, COLOR_LINE);
    if (current_page == PAGE_SYSTEM) system_page(d, known, now, received, ticks, &time);
    else {
        menu();
        switch (current_page) {
            case PAGE_SHOPPING: shopping(d, known, now); break;
            case PAGE_AGENDA: agenda(d, known, now); break;
            default: home(d, known, &time, now); break;
        }
    }
    sceDisplayWaitVblankStart();
    sceDisplaySetFrameBuf(pixels, 512, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);
    back_buffer = !back_buffer;
}

int main(void)
{
    unsigned previous_buttons = 0, last_revision = ~0u;
    uint64_t last_second = UINT64_MAX, ticks, second;
    int dirty = 1;
    setup_callbacks();
    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
    network_start();
    while (running) {
        SceCtrlData controller;
        unsigned pressed;
        network_read(&network);
        ticks = sceKernelGetSystemTimeWide();
        second = ticks / UINT64_C(1000000);
        if (network.revision != last_revision) { last_revision = network.revision; dirty = 1; }
        sceCtrlPeekBufferPositive(&controller, 1);
        pressed = controller.Buttons & ~previous_buttons;
        previous_buttons = controller.Buttons;
        if (pressed & PSP_CTRL_SELECT) {
            demo_mode = !demo_mode;
            network_enable(!demo_mode);
            if (demo_mode) { dashboard_demo(&demo); demo_received_us = ticks; }
            shopping_page = 0; dirty = 1;
        }
        if (pressed & PSP_CTRL_CROSS) {
            if (demo_mode) { dashboard_demo(&demo); demo_received_us = ticks; }
            else network_refresh();
            dirty = 1;
        }
        if (pressed & PSP_CTRL_SQUARE) { current_page = PAGE_HOME; dirty = 1; }
        if (pressed & PSP_CTRL_TRIANGLE) { current_page = PAGE_SHOPPING; dirty = 1; }
        if (pressed & PSP_CTRL_CIRCLE) { current_page = PAGE_AGENDA; dirty = 1; }
        if (pressed & PSP_CTRL_UP) { current_page = current_page == PAGE_SYSTEM ? PAGE_HOME : PAGE_SYSTEM; dirty = 1; }
        if (current_page == PAGE_SHOPPING && (pressed & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) {
            const Dashboard *d = demo_mode ? &demo : &network.dashboard;
            int pages = (d->shopping_count + SHOPPING_PER_PAGE - 1) / SHOPPING_PER_PAGE;
            if (pages < 1) pages = 1;
            if ((pressed & PSP_CTRL_RIGHT) && shopping_page + 1 < pages) ++shopping_page;
            if ((pressed & PSP_CTRL_LEFT) && shopping_page > 0) --shopping_page;
            dirty = 1;
        }
        if (second != last_second) {
            last_second = second; dirty = 1;
            /* Dedicated mains-powered display: keep both system and backlight awake. */
            scePowerTick(0);
        }
        if (dirty) { draw(ticks); dirty = 0; }
        else sceDisplayWaitVblankStart();
    }
    network_stop();
    sceKernelExitGame();
    return 0;
}
