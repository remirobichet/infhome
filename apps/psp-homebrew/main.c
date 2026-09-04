#include <pspuser.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_apctl.h>
#include <pspnet_inet.h>
#include <pspsdk.h>
#include <psputility.h>
#include <psputility_netmodules.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

PSP_MODULE_INFO("Infhome", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define INFHOME_WIFI_PROFILE 1
#define INFHOME_PI_IP "192.168.0.104"
#define INFHOME_PI_PORT 8080

#define COLOR_BG 0xFF17120F
#define COLOR_LINE 0xFF5B4938
#define COLOR_TEXT 0xFFE6D8C5
#define COLOR_MUTED 0xFF9F8D79
#define COLOR_AMBER 0xFF49A7D7
#define COLOR_GREEN 0xFF79BC8A
#define COLOR_RED 0xFF6666C8
#define COLOR_PSP_SQUARE 0xFFDE78D4
#define COLOR_PSP_TRIANGLE 0xFF6FC477
#define COLOR_PSP_CIRCLE 0xFF625DD8
#define COLOR_PSP_CROSS 0xFFD88D4F

typedef enum
{
    PAGE_HOME,
    PAGE_SHOPPING,
    PAGE_AGENDA,
    PAGE_SYSTEM
} Page;

typedef enum
{
    BUTTON_SQUARE,
    BUTTON_TRIANGLE,
    BUTTON_CIRCLE,
    BUTTON_CROSS
} Button;

static char psp_ip[16] = "Not connected";
static int demo_mode;
static int network_ready;
static int api_reachable;
static Page current_page = PAGE_HOME;

static volatile int apctl_error;

static void draw_text(int x, int y, u32 color, const char *text)
{
    pspDebugScreenSetXY(x, y);
    pspDebugScreenSetTextColor(color);
    pspDebugScreenPrintf("%s", text);
}

static const char *current_status(void)
{
    if (demo_mode)
        return "DEMO";

    return api_reachable ? "ONLINE" : "OFFLINE";
}

static u32 current_status_color(void)
{
    if (demo_mode)
        return COLOR_AMBER;

    return api_reachable ? COLOR_GREEN : COLOR_RED;
}

static void draw_horizontal_rule(int start_x, int end_x, int y)
{
    void *framebuffer;
    int buffer_width;
    int pixel_format;
    int i;

    if (sceDisplayGetFrameBuf(&framebuffer, &buffer_width, &pixel_format, PSP_DISPLAY_SETBUF_IMMEDIATE) < 0 || framebuffer == NULL)
        return;

    for (i = start_x; i < end_x; ++i)
        ((u32 *)framebuffer)[(y * 8 + 6) * buffer_width + i] = COLOR_LINE;
}

static void draw_vertical_rule(int x, int y)
{
    void *framebuffer;
    int buffer_width;
    int pixel_format;
    int i;

    if (sceDisplayGetFrameBuf(&framebuffer, &buffer_width, &pixel_format, PSP_DISPLAY_SETBUF_IMMEDIATE) < 0 || framebuffer == NULL)
        return;

    for (i = y * 8; i < 272; ++i)
        ((u32 *)framebuffer)[i * buffer_width + x * 7] = COLOR_LINE;
}

static void draw_topbar(void)
{
    int status_x = 68 - strlen(current_status());

    draw_text(2, 1, COLOR_AMBER, "INFHOME");
    draw_text(status_x - 8, 1, COLOR_MUTED, "STATUS");
    draw_text(status_x, 1, current_status_color(), current_status());
    draw_horizontal_rule(0, 480, 3);
}

static void draw_button_pixel(u32 *framebuffer, int buffer_width, int x, int y, u32 color)
{
    if (x >= 0 && x < 480 && y >= 0 && y < 272)
        framebuffer[y * buffer_width + x] = color;
}

static void draw_button_icon(int x, int y, Button button)
{
    static const char *square[] =
    {
        "##########",
        "#        #",
        "#        #",
        "#        #",
        "#        #",
        "#        #",
        "#        #",
        "#        #",
        "#        #",
        "##########"
    };
    static const char *triangle[] =
    {
        "    ##    ",
        "   ####   ",
        "   #  #   ",
        "  #    #  ",
        "  #    #  ",
        " #      # ",
        " #      # ",
        "#        #",
        "##########"
    };
    static const char *circle[] =
    {
        "   ####   ",
        " ##    ## ",
        "#        #",
        "#        #",
        "#        #",
        "#        #",
        "#        #",
        " ##    ## ",
        "   ####   "
    };
    static const char *cross[] =
    {
        "##      ##",
        " ###  ### ",
        "  ######  ",
        "   ####   ",
        "   ####   ",
        "  ######  ",
        " ###  ### ",
        "##      ##"
    };
    const char **shape;
    u32 color;
    void *framebuffer;
    int buffer_width;
    int pixel_format;
    int row;
    int column;
    int shape_height;

    switch (button)
    {
        case BUTTON_SQUARE:
            shape = square;
            shape_height = 10;
            color = COLOR_PSP_SQUARE;
            break;
        case BUTTON_TRIANGLE:
            shape = triangle;
            shape_height = 9;
            color = COLOR_PSP_TRIANGLE;
            break;
        case BUTTON_CIRCLE:
            shape = circle;
            shape_height = 9;
            color = COLOR_PSP_CIRCLE;
            break;
        case BUTTON_CROSS:
        default:
            shape = cross;
            shape_height = 8;
            color = COLOR_PSP_CROSS;
            break;
    }

    if (sceDisplayGetFrameBuf(&framebuffer, &buffer_width, &pixel_format, PSP_DISPLAY_SETBUF_IMMEDIATE) < 0 || framebuffer == NULL)
        return;

    for (row = 0; row < shape_height; ++row)
    {
        for (column = 0; shape[row][column] != '\0'; ++column)
        {
            if (shape[row][column] == '#')
                draw_button_pixel((u32 *)framebuffer, buffer_width, x * 8 + column, y * 8 + row, color);
        }
    }
}

static void draw_menu_item(int y, const char *label, Page page, Button button)
{
    draw_button_icon(1, y, button);
    draw_text(5, y, current_page == page ? COLOR_AMBER : COLOR_MUTED, label);

    if (current_page == page)
        draw_text(18, y, COLOR_AMBER, "<");
}

static void draw_menu(void)
{
    draw_vertical_rule(21, 4);
    draw_text(1, 5, COLOR_MUTED, "PAGES");
    draw_menu_item(8, "Accueil", PAGE_HOME, BUTTON_SQUARE);
    draw_menu_item(12, "Courses", PAGE_SHOPPING, BUTTON_TRIANGLE);
    draw_menu_item(16, "Agenda", PAGE_AGENDA, BUTTON_CIRCLE);
    draw_horizontal_rule(0, 21 * 7, 26);
    draw_button_icon(1, 28, BUTTON_CROSS);
    draw_text(4, 28, COLOR_MUTED, "refresh");
    draw_text(1, 31, COLOR_MUTED, "SELECT  switch mode");
}

static void draw_home_page(const char *message)
{
    draw_text(24, 5, COLOR_MUTED, "MAISON");
    draw_text(24, 8, COLOR_TEXT, message);
    draw_text(24, 12, COLOR_MUTED, "Aujourd'hui");
    draw_text(24, 14, COLOR_TEXT, "Tout est sous controle.");
    draw_text(24, 18, COLOR_MUTED, "Source");
    draw_text(24, 20, demo_mode ? COLOR_AMBER : current_status_color(), demo_mode ? "Donnees de demonstration" : (api_reachable ? "Raspberry Pi" : "Connexion indisponible"));
}

static void draw_shopping_page(void)
{
    draw_text(24, 5, COLOR_MUTED, "A ACHETER");
    draw_text(24, 8, COLOR_TEXT, "[ ] Lait");
    draw_text(24, 10, COLOR_TEXT, "[ ] Pain complet");
    draw_text(24, 12, COLOR_TEXT, "[ ] Tomates");
    draw_text(24, 14, COLOR_MUTED, "[x] Cafe");
    draw_text(24, 19, COLOR_MUTED, "3 articles restants");
}

static void draw_agenda_page(void)
{
    draw_text(24, 5, COLOR_MUTED, "AUJOURD'HUI");
    draw_text(24, 8, COLOR_AMBER, "09:30");
    draw_text(31, 8, COLOR_TEXT, "Reunion equipe");
    draw_text(24, 11, COLOR_AMBER, "18:00");
    draw_text(31, 11, COLOR_TEXT, "Courses");
    draw_text(24, 14, COLOR_AMBER, "20:30");
    draw_text(31, 14, COLOR_TEXT, "Diner maison");
    draw_text(24, 19, COLOR_MUTED, "3 evenements a venir");
}

static void draw_system_page(void)
{
    draw_text(3, 5, COLOR_MUTED, "STATUT SYSTEME");
    draw_text(3, 8, COLOR_MUTED, "MODE");
    draw_text(18, 8, current_status_color(), current_status());
    draw_text(3, 11, COLOR_MUTED, "WIFI");
    draw_text(18, 11, network_ready ? COLOR_GREEN : COLOR_RED, network_ready ? "CONNECTED" : "DISCONNECTED");
    draw_text(3, 14, COLOR_MUTED, "PSP IP");
    draw_text(18, 14, COLOR_TEXT, psp_ip);
    draw_text(3, 17, COLOR_MUTED, "API");
    draw_text(18, 17, demo_mode ? COLOR_AMBER : current_status_color(), demo_mode ? "SIMULATED" : (api_reachable ? "REACHABLE" : "UNREACHABLE"));
    draw_text(3, 20, COLOR_MUTED, "RASPBERRY PI");
    draw_text(18, 20, COLOR_TEXT, INFHOME_PI_IP ":8080");
    draw_text(3, 25, COLOR_MUTED, "HAUT: RETOUR ACCUEIL");
}

static void draw_dashboard(const char *message)
{
    pspDebugScreenSetBackColor(COLOR_BG);
    pspDebugScreenSetTextColor(COLOR_TEXT);
    pspDebugScreenClear();


    draw_topbar();

    if (current_page == PAGE_SYSTEM)
    {
        draw_system_page();
        return;
    }

    draw_menu();

    switch (current_page)
    {
        case PAGE_SHOPPING:
            draw_shopping_page();
            break;
        case PAGE_AGENDA:
            draw_agenda_page();
            break;
        case PAGE_HOME:
        default:
            draw_home_page(message);
            break;
    }
}

static void apctl_handler(int old_state, int new_state, int event, int error, void *arg)
{
    if (event == PSP_NET_APCTL_EVENT_ERROR || error != 0)
        apctl_error = error;
}

static void wait_vblank_frames(int frames)
{
    int i;

    for (i = 0; i < frames; ++i)
    {
        sceDisplayWaitVblankStart();
    }
}

static int load_network_module(int module)
{
    int result = sceUtilityLoadNetModule(module);

    if (result < 0 && result != SCE_ERROR_MODULE_ALREADY_LOADED)
    {
        pspDebugScreenPrintf("Load network module %d failed: 0x%08X\n", module, result);
        return result;
    }

    return 0;
}

static int load_network_modules(void)
{
    int result;

    result = load_network_module(PSP_NET_MODULE_COMMON);
    if (result < 0)
        return result;

    result = load_network_module(PSP_NET_MODULE_INET);
    if (result < 0)
        return result;

    return 0;
}

static int wait_for_ip(void)
{
    int result;
    int state;
    int last_state = -1;
    int attempts = 0;

    while (attempts < 900)
    {
        result = sceNetApctlGetState(&state);
        if (result < 0)
        {
            pspDebugScreenPrintf("sceNetApctlGetState failed: 0x%08X\n", result);
            return result;
        }

        if (state != last_state)
        {
            pspDebugScreenPrintf("Wi-Fi state: %d\n", state);
            last_state = state;
        }

        if (state == PSP_NET_APCTL_STATE_GOT_IP)
            return 0;

        if (apctl_error != 0)
        {
            pspDebugScreenPrintf("Wi-Fi authentication failed: 0x%08X\n", apctl_error);
            return apctl_error;
        }

        wait_vblank_frames(2);
        ++attempts;
    }

    pspDebugScreenPrintf("Timed out while waiting for Wi-Fi IP after 30 seconds\n");
    return -1;
}

static int init_network(void)
{
    int result;
    int handler_id;
    union SceNetApctlInfo info;

    pspDebugScreenPrintf("[1/4] Loading network modules...\n");
    result = load_network_modules();
    if (result < 0)
        return result;

    pspDebugScreenPrintf("[2/4] Initializing network...\n");
    result = pspSdkInetInit();
    if (result < 0)
    {
        pspDebugScreenPrintf("pspSdkInetInit failed: 0x%08X\n", result);
        return result;
    }

    apctl_error = 0;
    handler_id = sceNetApctlAddHandler(apctl_handler, NULL);
    if (handler_id < 0)
    {
        pspDebugScreenPrintf("sceNetApctlAddHandler failed: 0x%08X\n", handler_id);
        return handler_id;
    }

    pspDebugScreenPrintf("[3/4] Connecting Wi-Fi profile %d...\n", INFHOME_WIFI_PROFILE);
    result = sceNetApctlConnect(INFHOME_WIFI_PROFILE);
    if (result < 0)
    {
        pspDebugScreenPrintf("sceNetApctlConnect failed: 0x%08X\n", result);
        return result;
    }

    result = wait_for_ip();
    if (result < 0)
        return result;

    result = sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &info);
    if (result < 0)
    {
        pspDebugScreenPrintf("sceNetApctlGetInfo(IP) failed: 0x%08X\n", result);
        return result;
    }

    strncpy(psp_ip, info.ip, sizeof(psp_ip) - 1);
    psp_ip[sizeof(psp_ip) - 1] = '\0';
    pspDebugScreenPrintf("Connected. PSP IP: %s\n", psp_ip);
    return 0;
}

static int fetch_status(char *buffer, unsigned int buffer_size)
{
    const char request[] = "GET /api/status HTTP/1.0\r\nHost: " INFHOME_PI_IP ":8080\r\n\r\n";
    struct sockaddr_in server;
    int socket_fd;
    int bytes_sent;
    int total_read = 0;

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(INFHOME_PI_PORT);
    server.sin_addr.s_addr = inet_addr(INFHOME_PI_IP);

    socket_fd = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        pspDebugScreenPrintf("socket failed: %d\n", socket_fd);
        return socket_fd;
    }

    if (sceNetInetConnect(socket_fd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        pspDebugScreenPrintf("TCP connect failed: %d\n", sceNetInetGetErrno());
        sceNetInetClose(socket_fd);
        return -1;
    }

    bytes_sent = (int)sceNetInetSend(socket_fd, request, strlen(request), 0);
    if (bytes_sent != (int)strlen(request))
    {
        pspDebugScreenPrintf("HTTP request send failed: %d\n", sceNetInetGetErrno());
        sceNetInetClose(socket_fd);
        return -1;
    }

    while (total_read < (int)buffer_size - 1)
    {
        int bytes_read = (int)sceNetInetRecv(socket_fd, buffer + total_read, buffer_size - 1 - total_read, 0);

        if (bytes_read < 0)
        {
            pspDebugScreenPrintf("HTTP response read failed: %d\n", sceNetInetGetErrno());
            sceNetInetClose(socket_fd);
            return bytes_read;
        }

        if (bytes_read == 0)
            break;

        total_read += bytes_read;
    }

    buffer[total_read] = '\0';
    sceNetInetClose(socket_fd);
    return 0;
}

static int extract_message_from_json(const char *json, char *message, unsigned int message_size)
{
    const char *key = "\"message\"";
    const char *start = strstr(json, key);
    unsigned int message_length = 0;

    if (start == NULL || message_size == 0)
        return -1;

    start = strchr(start, ':');
    if (start == NULL)
        return -1;

    while (*start == ':' || *start == ' ')
        ++start;

    if (*start == '"')
        ++start;

    while (*start != '\0' && *start != '"' && *start != '\n' && *start != '\r' && message_length < message_size - 1)
    {
        message[message_length] = *start;
        ++message_length;
        ++start;
    }

    message[message_length] = '\0';
    return message_length > 0 ? 0 : -1;
}

static int refresh_dashboard_message(char *message, unsigned int message_size)
{
    char response[512];
    int result = fetch_status(response, sizeof(response));

    if (result < 0)
        return result;

    return extract_message_from_json(response, message, message_size);
}

int exit_callback(int arg1, int arg2, void *common)
{
    sceKernelExitGame();
    return 0;
}

int callback_thread(SceSize args, void *argp)
{
    int cbid = sceKernelCreateCallback(
        "Exit Callback",
        exit_callback,
        NULL
    );

    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();

    return 0;
}

int setup_callbacks(void)
{
    int thid = sceKernelCreateThread(
        "callback_thread",
        callback_thread,
        0x11,
        0xFA0,
        0,
        0
    );

    if (thid >= 0)
        sceKernelStartThread(thid, 0, 0);

    return thid;
}

int main(void)
{
    char message[160] = "Connecting to Infhome...";
    int result;
    unsigned int previous_buttons = 0;

    setup_callbacks();

    pspDebugScreenInit();
    pspDebugScreenSetBackColor(COLOR_BG);
    pspDebugScreenSetTextColor(COLOR_TEXT);
    pspDebugScreenClear();

    pspDebugScreenSetXY(0, 0);
    pspDebugScreenPrintf("INFHOME\n\n");

    result = init_network();
    if (result < 0)
    {
        strncpy(message, "Network connection failed", sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
        api_reachable = 0;
        draw_dashboard(message);
    }
    else
    {
        network_ready = 1;
        pspDebugScreenPrintf("[4/4] Fetching Raspberry Pi status...\n");
        result = refresh_dashboard_message(message, sizeof(message));

        if (result < 0)
            strncpy(message, "Unable to reach Raspberry Pi", sizeof(message) - 1);

        message[sizeof(message) - 1] = '\0';
        api_reachable = result == 0;
        draw_dashboard(message);
    }

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while (1)
    {
        SceCtrlData controller;
        unsigned int pressed;

        sceCtrlReadBufferPositive(&controller, 1);
        pressed = controller.Buttons & ~previous_buttons;
        previous_buttons = controller.Buttons;

        if (pressed & PSP_CTRL_CROSS)
        {
            if (demo_mode)
            {
                strncpy(message, "Hello from Raspberry Pi", sizeof(message) - 1);
                message[sizeof(message) - 1] = '\0';
                draw_dashboard(message);
                continue;
            }

            draw_dashboard("Refreshing status...");
            result = network_ready ? refresh_dashboard_message(message, sizeof(message)) : -1;

            if (result < 0)
                strncpy(message, "Unable to reach Raspberry Pi", sizeof(message) - 1);

            message[sizeof(message) - 1] = '\0';
            api_reachable = result == 0;
            draw_dashboard(message);
        }

        if (pressed & PSP_CTRL_SELECT)
        {
            demo_mode = !demo_mode;

            if (demo_mode)
            {
                strncpy(message, "Hello from Raspberry Pi", sizeof(message) - 1);
                message[sizeof(message) - 1] = '\0';
                draw_dashboard(message);
            }
            else
            {
                draw_dashboard("Switching to live API...");
                result = network_ready ? refresh_dashboard_message(message, sizeof(message)) : -1;

                if (result < 0)
                    strncpy(message, "Network connection required", sizeof(message) - 1);

                message[sizeof(message) - 1] = '\0';
                api_reachable = result == 0;
                draw_dashboard(message);
            }
        }

        if (pressed & PSP_CTRL_SQUARE)
        {
            current_page = PAGE_HOME;
            draw_dashboard(message);
        }

        if (pressed & PSP_CTRL_TRIANGLE)
        {
            current_page = PAGE_SHOPPING;
            draw_dashboard(message);
        }

        if (pressed & PSP_CTRL_CIRCLE)
        {
            current_page = PAGE_AGENDA;
            draw_dashboard(message);
        }

        if (pressed & PSP_CTRL_UP)
        {
            current_page = current_page == PAGE_SYSTEM ? PAGE_HOME : PAGE_SYSTEM;
            draw_dashboard(message);
        }

        sceDisplayWaitVblankStart();
    }

    return 0;
}
