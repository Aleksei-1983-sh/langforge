#include "modules/realtime/realtime.h"
#include "modules/realtime/realtime_hub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int realtime_build_timestamp(char *buffer, size_t buffer_size)
{
    time_t now;
    struct tm tm_utc;

    if (!buffer || buffer_size < 21) {
        return -1;
    }

    now = time(NULL);
    if (now == (time_t) -1) {
        return -1;
    }

    if (!gmtime_r(&now, &tm_utc)) {
        return -1;
    }

    if (strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%SZ", &tm_utc) == 0) {
        return -1;
    }

    return 0;
}

char *realtime_build_event(const char *type, int job_id, const char *payload_json)
{
    const char *payload;
    char timestamp[32];
    int needed;
    char *event_json;

    if (!type || type[0] == '\0' || job_id < 0) {
        return NULL;
    }

    payload = payload_json ? payload_json : "{}";

    if (realtime_build_timestamp(timestamp, sizeof(timestamp)) != 0) {
        return NULL;
    }

    needed = snprintf(NULL, 0,
                      "{\"type\":\"%s\",\"job_id\":%d,\"payload\":%s,\"ts\":\"%s\"}",
                      type, job_id, payload, timestamp);
    if (needed < 0) {
        return NULL;
    }

    event_json = malloc((size_t) needed + 1);
    if (!event_json) {
        return NULL;
    }

    if (snprintf(event_json, (size_t) needed + 1,
                 "{\"type\":\"%s\",\"job_id\":%d,\"payload\":%s,\"ts\":\"%s\"}",
                 type, job_id, payload, timestamp) != needed) {
        free(event_json);
        return NULL;
    }

    return event_json;
}

int realtime_emit_event(const char *event_type, int job_id, const char *json_payload)
{
    char *event_json;
    int rc;

    event_json = realtime_build_event(event_type, job_id, json_payload);
    if (!event_json) {
        return REALTIME_API_ERR_INVALID_ARGUMENT;
    }

    rc = fprintf(stdout, "[REALTIME] %s\n", event_json);

    if (rc < 0) {
        free(event_json);
        clearerr(stdout);
        return REALTIME_API_ERR_SERVER;
    }

    rt_hub_broadcast(event_json);
    free(event_json);
    fflush(stdout);
    return REALTIME_API_OK;
}
