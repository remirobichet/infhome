#include <pspuser.h>
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

static volatile int apctl_error;

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

    pspDebugScreenPrintf("Connected. PSP IP: %s\n", info.ip);
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

static void print_message_from_json(const char *json)
{
    const char *key = "\"message\"";
    const char *start = strstr(json, key);

    if (start == NULL)
    {
        pspDebugScreenPrintf("Response:\n%s\n", json);
        return;
    }

    start = strchr(start, ':');
    if (start == NULL)
    {
        pspDebugScreenPrintf("Response:\n%s\n", json);
        return;
    }

    while (*start == ':' || *start == ' ')
        ++start;

    if (*start == '"')
        ++start;

    pspDebugScreenPrintf("Message: ");

    while (*start != '\0' && *start != '"' && *start != '\n' && *start != '\r')
    {
        pspDebugScreenPrintf("%c", *start);
        ++start;
    }

    pspDebugScreenPrintf("\n");
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
    char response[512];
    int result;

    setup_callbacks();

    pspDebugScreenInit();
    pspDebugScreenClear();

    pspDebugScreenSetXY(0, 0);
    pspDebugScreenPrintf("INFHOME\n\n");

    result = init_network();
    if (result < 0)
    {
        pspDebugScreenPrintf("\nNetwork init failed.\n");
    }
    else
    {
        pspDebugScreenPrintf("[4/4] Fetching Raspberry Pi status...\n");
        result = fetch_status(response, sizeof(response));

        if (result < 0)
        {
            pspDebugScreenPrintf("\nHTTP request failed.\n");
        }
        else
        {
            pspDebugScreenPrintf("\nRaw response:\n%s\n\n", response);
            print_message_from_json(response);
        }
    }

    pspDebugScreenPrintf("\nPress Home to quit.\n");

    while (1)
    {
        sceDisplayWaitVblankStart();
    }

    return 0;
}
