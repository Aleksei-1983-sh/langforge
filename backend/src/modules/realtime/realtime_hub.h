#ifndef REALTIME_HUB_H
#define REALTIME_HUB_H

#include <stddef.h>

typedef struct {
    int fd;
    int is_websocket;
    int is_sse;
    int user_id;
    int subscribed_job_id;
    char write_buffer[4096];
    size_t write_len;
} rt_client_t;

#define RT_HUB_MAX_CLIENTS 10000

typedef struct {
    rt_client_t clients[RT_HUB_MAX_CLIENTS];
    int count;
} rt_hub_t;

void rt_hub_init(void);
void rt_hub_shutdown(void);
int rt_hub_add_client(int fd, int is_ws, int is_sse);
void rt_hub_remove_client(int fd);
void rt_hub_set_subscription(int fd, int job_id);
void rt_hub_mark_websocket(int fd);
void rt_hub_mark_sse(int fd);
void rt_hub_broadcast(const char *json);

#endif
