#include "network.h"

#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_apctl.h>
#include <pspnet_inet.h>
#include <pspsdk.h>
#include <psputility.h>
#include <psputility_netmodules.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

static SceUID mutex = -1, worker = -1;
static NetworkState shared;
static int enabled = 1, stopping, requested = 1;
static int manual_requested;

static void lock(void) { sceKernelWaitSema(mutex, 1, NULL); }
static void unlock(void) { sceKernelSignalSema(mutex, 1); }

static int active(void)
{
    int result;
    lock(); result = enabled && !stopping; unlock();
    return result;
}

static void status(const char *message, int busy, int online)
{
    lock();
    snprintf(shared.status, sizeof(shared.status), "%s", message);
    shared.busy = busy;
    shared.online = online;
    ++shared.revision;
    unlock();
}

static void wifi_state(int connected)
{
    union SceNetApctlInfo info;
    char ip[16] = "--";
    if (connected && sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &info) == 0)
        snprintf(ip, sizeof(ip), "%s", info.ip);
    lock();
    if (shared.wifi != connected || strcmp(shared.ip, ip)) {
        shared.wifi = connected;
        snprintf(shared.ip, sizeof(shared.ip), "%s", ip);
        if (!connected) shared.online = 0;
        ++shared.revision;
    }
    unlock();
}

static int connect_wifi(void)
{
    int state;
    uint64_t deadline;
    if (sceNetApctlGetState(&state) == 0 && state == PSP_NET_APCTL_STATE_GOT_IP) {
        wifi_state(1);
        return 1;
    }
    wifi_state(0);
    status("Connexion Wi-Fi...", 1, 0);
    sceNetApctlDisconnect();
    deadline = sceKernelGetSystemTimeWide() + UINT64_C(2000000);
    do {
        if (!active()) return 0;
        if (sceNetApctlGetState(&state) == 0 && state == PSP_NET_APCTL_STATE_DISCONNECTED) break;
        sceKernelDelayThread(100000);
    } while ((uint64_t)sceKernelGetSystemTimeWide() < deadline);
    if (!active() || sceNetApctlConnect(INFHOME_WIFI_PROFILE) < 0) return 0;
    deadline = sceKernelGetSystemTimeWide() + UINT64_C(30000000);
    while (active() && (uint64_t)sceKernelGetSystemTimeWide() < deadline) {
        if (sceNetApctlGetState(&state) < 0) break;
        if (state == PSP_NET_APCTL_STATE_GOT_IP) { wifi_state(1); return 1; }
        sceKernelDelayThread(100000);
    }
    sceNetApctlDisconnect();
    return 0;
}

static int would_block(void)
{
    int error = sceNetInetGetErrno();
    return error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS || error == EALREADY || error == EINTR;
}

static int ready(int socket_fd, int writing, uint64_t deadline)
{
    struct SceNetInetPollfd fd;
    int result;
    fd.fd = socket_fd;
    fd.events = writing ? SCE_NET_INET_POLLOUT : SCE_NET_INET_POLLIN;
    while (active() && (uint64_t)sceKernelGetSystemTimeWide() < deadline) {
        fd.revents = 0;
        result = sceNetInetPoll(&fd, 1, 100);
        if (result < 0) { if (would_block()) continue; return 0; }
        if (result > 0) {
            if (fd.revents & (SCE_NET_INET_POLLERR | SCE_NET_INET_POLLNVAL)) return 0;
            /* A closed read side may still contain buffered response bytes. */
            if (fd.revents & (fd.events | SCE_NET_INET_POLLHUP)) return 1;
        }
    }
    return 0;
}

static int receive(int socket_fd, char *buffer, size_t capacity, uint64_t deadline)
{
    int result;
    while (ready(socket_fd, 0, deadline)) {
        result = (int)sceNetInetRecv(socket_fd, buffer, capacity, 0);
        if (result >= 0) return result;
        if (!would_block()) return -1;
    }
    return -1;
}

static int fetch(Dashboard *out, const char **error, int manual)
{
    const char *request = manual
        ? "POST /api/v1/dashboard/refresh HTTP/1.0\r\nHost: " INFHOME_PI_IP ":8080\r\nAccept: application/json\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
        : "GET /api/v1/dashboard HTTP/1.0\r\nHost: " INFHOME_PI_IP ":8080\r\nAccept: application/json\r\nConnection: close\r\n\r\n";
    char headers[DASHBOARD_HEADER_LIMIT + 1], body[DASHBOARD_BODY_LIMIT + 1];
    struct sockaddr_in server;
    int socket_fd, nonblocking = 1, result, socket_error = 0, ok = 0;
    socklen_t error_length = sizeof(socket_error);
    size_t sent = 0, header_length = 0, body_length = 0, used = 0;
    size_t request_length = strlen(request);
    uint64_t deadline = sceKernelGetSystemTimeWide() + (manual ? UINT64_C(60000000) : UINT64_C(10000000));
    *error = "Connexion API impossible";
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(INFHOME_PI_PORT);
    server.sin_addr.s_addr = inet_addr(INFHOME_PI_IP);
    socket_fd = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) return 0;
    if (sceNetInetSetsockopt(socket_fd, SOL_SOCKET, SO_NONBLOCK, &nonblocking, sizeof(nonblocking)) < 0) goto done;
    if (sceNetInetConnect(socket_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        if (!would_block() || !ready(socket_fd, 1, deadline)) goto done;
        if (sceNetInetGetsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_length) < 0 || socket_error) goto done;
    }
    *error = "Envoi HTTP interrompu";
    while (sent < request_length) {
        if (!ready(socket_fd, 1, deadline)) goto done;
        result = (int)sceNetInetSend(socket_fd, request + sent, request_length - sent, 0);
        if (result < 0 && would_block()) continue;
        if (result <= 0) goto done;
        sent += (size_t)result;
    }
    *error = "En-tetes HTTP invalides / timeout";
    /* Read header bytes separately so no body byte can consume header capacity. */
    while (header_length < DASHBOARD_HEADER_LIMIT) {
        result = receive(socket_fd, headers + header_length, 1, deadline);
        if (result != 1) goto done;
        ++header_length;
        if (header_length >= 4 && !memcmp(headers + header_length - 4, "\r\n\r\n", 4)) break;
    }
    headers[header_length] = 0;
    if (!dashboard_http_headers(headers, header_length, &body_length)) goto done;
    *error = "Corps HTTP incomplet / timeout";
    while (used < body_length) {
        result = receive(socket_fd, body + used, body_length - used, deadline);
        if (result <= 0) goto done;
        used += (size_t)result;
    }
    /* The contract closes the connection. Reject bytes beyond Content-Length. */
    if (receive(socket_fd, body + used, 1, deadline) != 0) goto done;
    body[used] = 0;
    *error = "Dashboard JSON invalide";
    ok = dashboard_parse(body, used, out);
done:
    sceNetInetClose(socket_fd);
    return ok;
}

static int network_worker(SceSize args, void *argp)
{
    Dashboard result;
    uint64_t next = 0;
    int inet_ready = 0, common_loaded = 0, inet_loaded = 0;
    int end, run, refresh, manual, state, code;
    const char *error = "Requete annulee";
    (void)args; (void)argp;
    for (;;) {
        lock();
        end = stopping; run = enabled; refresh = requested; manual = manual_requested;
        if (run) { requested = 0; manual_requested = 0; }
        unlock();
        if (end) break;
        if (!run) { sceKernelDelayThread(100000); continue; }
        if (inet_ready && sceNetApctlGetState(&state) == 0) {
            wifi_state(state == PSP_NET_APCTL_STATE_GOT_IP);
        }
        if (!refresh && (uint64_t)sceKernelGetSystemTimeWide() < next) { sceKernelDelayThread(100000); continue; }
        status("Actualisation...", 1, 0);
        if (!common_loaded) {
            code = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
            common_loaded = code >= 0 || (unsigned)code == SCE_ERROR_MODULE_ALREADY_LOADED;
        }
        if (common_loaded && !inet_loaded) {
            code = sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
            inet_loaded = code >= 0 || (unsigned)code == SCE_ERROR_MODULE_ALREADY_LOADED;
        }
        if (inet_loaded && !inet_ready) inet_ready = pspSdkInetInit() >= 0;
        if (!inet_ready) {
            status("Initialisation reseau impossible", 0, 0);
            next = sceKernelGetSystemTimeWide() + UINT64_C(10000000);
            continue;
        }
        if (!connect_wifi()) {
            status(active() ? "Wi-Fi indisponible" : "Reseau en pause", 0, 0);
            next = sceKernelGetSystemTimeWide() + UINT64_C(10000000);
            continue;
        }
        status(manual ? "Actualisation courses/agenda..." : "Lecture du dashboard...", 1, 0);
        if (active() && fetch(&result, &error, manual)) {
            lock();
            if (enabled && !stopping) {
                shared.dashboard = result;
                shared.has_dashboard = 1;
                shared.received_us = sceKernelGetSystemTimeWide();
                shared.online = 1;
                snprintf(shared.status, sizeof(shared.status), "Dashboard actualise");
            }
            shared.busy = 0;
            ++shared.revision;
            unlock();
            next = sceKernelGetSystemTimeWide() + UINT64_C(60000000);
        } else {
            status(active() ? (manual ? "Echec actualisation courses/agenda" : error) : "Reseau en pause", 0, 0);
            next = sceKernelGetSystemTimeWide() + UINT64_C(10000000);
        }
    }
    if (inet_ready) { sceNetApctlDisconnect(); pspSdkInetTerm(); }
    return 0;
}

int network_start(void)
{
    memset(&shared, 0, sizeof(shared));
    strcpy(shared.ip, "--"); strcpy(shared.status, "Demarrage reseau...");
    mutex = sceKernelCreateSema("infhome_data", 0, 1, 1, NULL);
    if (mutex < 0) return 0;
    worker = sceKernelCreateThread("infhome_network", network_worker, 0x19, 96 * 1024, PSP_THREAD_ATTR_USER, NULL);
    if (worker < 0 || sceKernelStartThread(worker, 0, NULL) < 0) {
        if (worker >= 0) sceKernelDeleteThread(worker);
        worker = -1;
        strcpy(shared.status, "Thread reseau indisponible");
        return 0;
    }
    return 1;
}

void network_read(NetworkState *out)
{
    if (mutex < 0) { memset(out, 0, sizeof(*out)); strcpy(out->status, "Reseau indisponible"); return; }
    lock(); *out = shared; unlock();
}

void network_refresh(void)
{
    if (mutex < 0) return;
    lock();
    if (enabled && !shared.busy) { requested = 1; manual_requested = 1; shared.busy = 1; }
    unlock();
}

void network_enable(int value)
{
    if (mutex < 0) return;
    lock(); enabled = value; manual_requested = 0; if (value) requested = 1; unlock();
}

void network_stop(void)
{
    if (mutex < 0) return;
    lock(); stopping = 1; unlock();
    if (worker >= 0) {
        sceKernelWaitThreadEnd(worker, NULL);
        sceKernelDeleteThread(worker);
    }
    sceKernelDeleteSema(mutex);
    mutex = -1;
}
