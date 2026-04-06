#include "services/job_event_service.h"

#include "internal_api/realtime_api.h"
#include "libs/cJSON.h"

static char *build_progress_payload(const generation_job_t *job,
                                    const char *step,
                                    int progress)
{
    cJSON *root;
    char *out;

    root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "step", step ? step : "");
    cJSON_AddNumberToObject(root, "progress", progress);
    cJSON_AddStringToObject(root, "status", job && job->status ? job->status : "");

    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *build_job_payload(const generation_job_t *job)
{
    cJSON *root;
    char *out;

    if (!job) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddNumberToObject(root, "job_id", job->job_id);
    cJSON_AddStringToObject(root, "status", job->status ? job->status : "");
    cJSON_AddNumberToObject(root, "generated_drafts", job->generated_drafts);
    cJSON_AddNumberToObject(root, "reviewed_drafts", job->reviewed_drafts);
    if (job->error_message) {
        cJSON_AddStringToObject(root, "error", job->error_message);
    }

    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *build_draft_payload(const generation_card_draft_t *draft)
{
    cJSON *root;
    cJSON *examples;
    char *out;
    int i;

    if (!draft) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddNumberToObject(root, "draft_id", draft->draft_id);
    cJSON_AddNumberToObject(root, "job_id", draft->job_id);
    cJSON_AddStringToObject(root, "status", draft->status ? draft->status : "");
    cJSON_AddStringToObject(root, "word", draft->word ? draft->word : "");
    cJSON_AddStringToObject(root, "transcription", draft->transcription ? draft->transcription : "");
    cJSON_AddStringToObject(root, "translation", draft->translation ? draft->translation : "");
    if (draft->saved_card_id > 0) {
        cJSON_AddNumberToObject(root, "card_id", draft->saved_card_id);
    }

    examples = cJSON_AddArrayToObject(root, "examples");
    if (!examples) {
        cJSON_Delete(root);
        return NULL;
    }

    for (i = 0; i < 2; i++) {
        cJSON_AddItemToArray(examples, cJSON_CreateString(draft->examples[i] ? draft->examples[i] : ""));
    }

    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

void job_event_service_emit_created(const generation_job_t *job)
{
    if (!job) {
        return;
    }

    realtime_emit_event(REALTIME_EVENT_GENERATION_JOB_CREATED,
                        job->job_id,
                        "{\"status\":\"queued\"}");
}

void job_event_service_emit_progress(const generation_job_t *job,
                                     const char *step,
                                     int progress)
{
    char *payload;

    if (!job) {
        return;
    }

    payload = build_progress_payload(job, step, progress);
    if (!payload) {
        return;
    }

    realtime_emit_event(REALTIME_EVENT_GENERATION_JOB_PROGRESS, job->job_id, payload);
    cJSON_free(payload);
}

void job_event_service_emit_job(const generation_job_t *job,
                                const char *event_type)
{
    char *payload;

    if (!job || !event_type) {
        return;
    }

    payload = build_job_payload(job);
    if (!payload) {
        return;
    }

    realtime_emit_event(event_type, job->job_id, payload);
    cJSON_free(payload);
}

void job_event_service_emit_draft(const generation_card_draft_t *draft,
                                  const char *event_type)
{
    char *payload;

    if (!draft || !event_type) {
        return;
    }

    payload = build_draft_payload(draft);
    if (!payload) {
        return;
    }

    realtime_emit_event(event_type, draft->job_id, payload);
    cJSON_free(payload);
}
