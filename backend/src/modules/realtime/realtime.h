#ifndef REALTIME_H
#define REALTIME_H

#include "internal_api/realtime_api.h"

char *realtime_build_event(const char *type, int job_id, const char *payload_json);

#endif
