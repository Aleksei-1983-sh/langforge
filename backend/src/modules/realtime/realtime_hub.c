#include "modules/realtime/realtime_hub.h"

#include "modules/realtime/realtime_ws.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static rt_hub_t g_rt_hub;

static int rt_hub_extract_job_id(const char *json)
{
    const char *job_key;
    char *endptr;
    long value;

    if (!json) {
        return 0;
    }

    job_key = strstr(json, "\"job_id\":");
    if (!job_key) {
        return 0;
    }

    job_key += strlen("\"job_id\":");
    value = strtol(job_key, &endptr, 10);
    if (endptr == job_key || value <= 0 || value > 2147483647L) {
        return 0;
    }

    return (int) value;
}

void rt_hub_init(void)
{
    memset(&g_rt_hub, 0, sizeof(g_rt_hub));
}

void rt_hub_shutdown(void)
{
    memset(&g_rt_hub, 0, sizeof(g_rt_hub));
}

int rt_hub_add_client(int fd, int is_ws, int is_sse)
{
    int i;

    if (fd < 0) {
        return -1;
    }

    for (i = 0; i < RT_HUB_MAX_CLIENTS; i++) {
        if (g_rt_hub.clients[i].fd == fd) {
            g_rt_hub.clients[i].is_websocket = is_ws ? 1 : 0;
            g_rt_hub.clients[i].is_sse = is_sse ? 1 : 0;
            g_rt_hub.clients[i].write_len = 0;
            return 0;
        }
    }

    for (i = 0; i < RT_HUB_MAX_CLIENTS; i++) {
        if (g_rt_hub.clients[i].fd == 0) {
            g_rt_hub.clients[i].fd = fd;
            g_rt_hub.clients[i].is_websocket = is_ws ? 1 : 0;
            g_rt_hub.clients[i].is_sse = is_sse ? 1 : 0;
            g_rt_hub.clients[i].user_id = 0;
            g_rt_hub.clients[i].subscribed_job_id = 0;
            g_rt_hub.clients[i].write_len = 0;
            g_rt_hub.count++;
            return 0;
        }
    }

    return -1;
}

void rt_hub_remove_client(int fd)
{
    int i;

    if (fd < 0) {
        return;
    }

    for (i = 0; i < RT_HUB_MAX_CLIENTS; i++) {
        if (g_rt_hub.clients[i].fd == fd) {
            memset(&g_rt_hub.clients[i], 0, sizeof(g_rt_hub.clients[i]));
            if (g_rt_hub.count > 0) {
                g_rt_hub.count--;
            }
            return;
        }
    }
}

void rt_hub_set_subscription(int fd, int job_id)
{
    int i;

    for (i = 0; i < RT_HUB_MAX_CLIENTS; i++) {
        if (g_rt_hub.clients[i].fd == fd) {
            g_rt_hub.clients[i].subscribed_job_id = job_id > 0 ? job_id : 0;
            return;
        }
    }
}

void rt_hub_mark_websocket(int fd)
{
    (void) rt_hub_add_client(fd, 1, 0);
}

void rt_hub_mark_sse(int fd)
{
    (void) rt_hub_add_client(fd, 0, 1);
}

void rt_hub_broadcast(const char *json)
{
    int i;
    int event_job_id;

    if (!json) {
        return;
    }

    event_job_id = rt_hub_extract_job_id(json);

    for (i = 0; i < RT_HUB_MAX_CLIENTS; i++) {
        rt_client_t *client = &g_rt_hub.clients[i];
        int rc;

        if (client->fd <= 0) {
            continue;
        }
        if (client->subscribed_job_id != 0 &&
            client->subscribed_job_id != event_job_id) {
            continue;
        }

        if (client->is_websocket) {
            rc = rt_ws_send_text(client->fd, json);
        } else if (client->is_sse) {
            rc = rt_sse_send(client->fd, json);
        } else {
            rc = -1;
        }

        if (rc != 0) {
            shutdown(client->fd, SHUT_RDWR);
            rt_hub_remove_client(client->fd);
        }
    }
}
