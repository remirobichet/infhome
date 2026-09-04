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
#define COLOR_PANEL 0xFF241C16
#define COLOR_LINE 0xFF5B4938
#define COLOR_TEXT 0xFFE6D8C5
#define COLOR_MUTED 0xFF9F8D79
#define COLOR_AMBER 0xFF49A7D7
#define COLOR_GREEN 0xFF79BC8A
#define COLOR_RED 0xFF6666C8

static char psp_ip[16] = "Not connected";
static int demo_mode;

static volatile int apctl_error;

static void draw_text(int x, int y, u32 color, const char *text)
{
    pspDebugScreenSetXY(x, y);
    pspDebugScreenSetTextColor(color);
    pspDebugScreenPrintf("%s", text);
}

static void draw_border(int x, int y, int width, int height)
{
    int i;

    for (i = x; i < x + width; ++i)
    {
        pspDebugScreenPutChar(i, y, COLOR_LINE, '-');
        pspDebugScreenPutChar(i, y + height - 1, COLOR_LINE, '-');
    }

    for (i = y; i < y + height; ++i)
    {
        pspDebugScreenPutChar(x, i, COLOR_LINE, '|');
        pspDebugScreenPutChar(x + width - 1, i, COLOR_LINE, '|');
    }

    pspDebugScreenPutChar(x, y, COLOR_LINE, '+');
    pspDebugScreenPutChar(x + width - 1, y, COLOR_LINE, '+');
    pspDebugScreenPutChar(x, y + height - 1, COLOR_LINE, '+');
    pspDebugScreenPutChar(x + width - 1, y + height - 1, COLOR_LINE, '+');
}

static void fill_panel(int x, int y, int width, int height)
{
    int column;
    int row;

    pspDebugScreenSetBackColor(COLOR_PANEL);

    for (row = y; row < y + height; ++row)
    {
        for (column = x; column < x + width; ++column)
        {
            pspDebugScreenPutChar(column, row, COLOR_PANEL, ' ');
        }
    }

    pspDebugScreenSetBackColor(COLOR_BG);
}

static void draw_dashboard(const char *message, int api_ok)
{
    pspDebugScreenSetBackColor(COLOR_BG);
    pspDebugScreenSetTextColor(COLOR_TEXT);
    pspDebugScreenClear();

    draw_text(2, 2, COLOR_AMBER, "INFHOME");
    draw_text(12, 2, COLOR_MUTED, "HOME DISPLAY");
    draw_text(43, 2, demo_mode ? COLOR_AMBER : (api_ok ? COLOR_GREEN : COLOR_RED), demo_mode ? "DEMO MODE" : (api_ok ? "ONLINE" : "OFFLINE"));

    draw_text(2, 4, COLOR_LINE, "--------------------------------------------------------");

    fill_panel(2, 7, 35, 15);
    draw_border(2, 7, 35, 15);
    draw_text(4, 9, COLOR_MUTED, "MESSAGE FROM HOME");
    draw_text(4, 12, COLOR_TEXT, message);
    draw_text(4, 19, COLOR_MUTED, demo_mode ? "Source: local demo data" : (api_ok ? "Last sync: just now" : "Last sync: unavailable"));

    fill_panel(39, 7, 19, 15);
    draw_border(39, 7, 19, 15);
    draw_text(41, 9, COLOR_MUTED, "SYSTEM");
    draw_text(41, 12, demo_mode ? COLOR_AMBER : COLOR_GREEN, demo_mode ? "DEMO DATA" : "WIFI CONNECTED");
    draw_text(41, 14, COLOR_MUTED, "PSP IP");
    draw_text(41, 15, COLOR_TEXT, psp_ip);
    draw_text(41, 18, COLOR_MUTED, "API");
    draw_text(41, 19, demo_mode ? COLOR_AMBER : (api_ok ? COLOR_GREEN : COLOR_RED), demo_mode ? "SIMULATED" : (api_ok ? "REACHABLE" : "UNREACHABLE"));

    draw_text(2, 25, COLOR_MUTED, "RASPBERRY PI");
    draw_text(16, 25, COLOR_TEXT, INFHOME_PI_IP ":8080");
    draw_text(2, 29, COLOR_AMBER, "X");
    draw_text(4, 29, COLOR_MUTED, "REFRESH");
    draw_text(19, 29, COLOR_AMBER, "SELECT");
    draw_text(26, 29, COLOR_MUTED, "MODE");
    draw_text(42, 29, COLOR_MUTED, "HOME TO QUIT");
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
    int network_ready = 0;
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
        draw_dashboard(message, 0);
    }
    else
    {
        network_ready = 1;
        pspDebugScreenPrintf("[4/4] Fetching Raspberry Pi status...\n");
        result = refresh_dashboard_message(message, sizeof(message));

        if (result < 0)
            strncpy(message, "Unable to reach Raspberry Pi", sizeof(message) - 1);

        message[sizeof(message) - 1] = '\0';
        draw_dashboard(message, result == 0);
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
                draw_dashboard(message, 1);
                continue;
            }

            draw_dashboard("Refreshing status...", 1);
            result = network_ready ? refresh_dashboard_message(message, sizeof(message)) : -1;

            if (result < 0)
                strncpy(message, "Unable to reach Raspberry Pi", sizeof(message) - 1);

            message[sizeof(message) - 1] = '\0';
            draw_dashboard(message, result == 0);
        }

        if (pressed & PSP_CTRL_SELECT)
        {
            demo_mode = !demo_mode;

            if (demo_mode)
            {
                strncpy(message, "Hello from Raspberry Pi", sizeof(message) - 1);
                message[sizeof(message) - 1] = '\0';
                draw_dashboard(message, 1);
            }
            else
            {
                draw_dashboard("Switching to live API...", 1);
                result = network_ready ? refresh_dashboard_message(message, sizeof(message)) : -1;

                if (result < 0)
                    strncpy(message, "Network connection required", sizeof(message) - 1);

                message[sizeof(message) - 1] = '\0';
                draw_dashboard(message, result == 0);
            }
        }

        sceDisplayWaitVblankStart();
    }

    return 0;
}
