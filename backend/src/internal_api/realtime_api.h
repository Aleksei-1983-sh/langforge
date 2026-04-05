#ifndef INTERNAL_API_REALTIME_API_H
#define INTERNAL_API_REALTIME_API_H

enum {
    REALTIME_API_OK = 0,
    REALTIME_API_ERR_INVALID_ARGUMENT = -1,
    REALTIME_API_ERR_SERVER = -2
};

#define REALTIME_EVENT_GENERATION_JOB_CREATED "generation.job.created"
#define REALTIME_EVENT_GENERATION_JOB_PROGRESS "generation.job.progress"
#define REALTIME_EVENT_GENERATION_JOB_STEP "generation.job.step"
#define REALTIME_EVENT_GENERATION_CARD_DRAFT "generation.card.draft"
#define REALTIME_EVENT_GENERATION_CARD_UPDATED "generation.card.updated"
#define REALTIME_EVENT_GENERATION_CARD_SAVED "generation.card.saved"
#define REALTIME_EVENT_GENERATION_JOB_COMPLETED "generation.job.completed"
#define REALTIME_EVENT_GENERATION_JOB_FAILED "generation.job.failed"

int realtime_emit_event(const char *event_type, int job_id, const char *json_payload);

#endif
