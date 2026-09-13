#ifndef INFHOME_NETWORK_H
#define INFHOME_NETWORK_H

#include "dashboard.h"

#define INFHOME_WIFI_PROFILE 1
#define INFHOME_PI_IP "192.168.0.104"
#define INFHOME_PI_PORT 8080

typedef struct {
    Dashboard dashboard;
    int has_dashboard;
    int wifi;
    int online;
    int busy;
    unsigned revision;
    uint64_t received_us;
    char ip[16];
    char status[64];
} NetworkState;

int network_start(void);
void network_read(NetworkState *out);
void network_refresh(void);
void network_enable(int enabled);
void network_stop(void);

#endif
