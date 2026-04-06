#include "services/generation_job_app_service.h"

#include "domain/job/job_state_machine.h"
#include "services/generation_job_service.h"
#include "services/job_event_service.h"
#include "services/job_pipeline_service.h"
#include "services/job_repository_memory.h"
#include "services/job_review_service.h"

static int map_repository_lookup_error(int rc)
{
    if (rc == 0) {
        return GENERATION_JOB_SERVICE_OK;
    }

    return GENERATION_JOB_SERVICE_ERR_NOT_FOUND;
}

static int generation_job_app_service_load_job(int job_id, generation_job_t **out_job)
{
    if (!out_job || job_id <= 0) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    return map_repository_lookup_error(job_repository_memory_get(job_id, out_job));
}

static int generation_job_app_service_load_draft(generation_job_t *job,
                                                 int draft_id,
                                                 generation_card_draft_t **out_draft)
{
    generation_card_draft_t *draft;

    if (!job || !out_draft || draft_id <= 0) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    draft = job_repository_memory_find_draft(job, draft_id);
    if (!draft) {
        return GENERATION_JOB_SERVICE_ERR_NOT_FOUND;
    }

    *out_draft = draft;
    return GENERATION_JOB_SERVICE_OK;
}

void generation_job_app_service_init(void)
{
    job_repository_memory_init();
}

void generation_job_app_service_shutdown(void)
{
    job_repository_memory_shutdown();
}

int generation_job_app_service_create(const generation_job_create_input_t *input,
                                      const generation_job_t **out_job)
{
    generation_job_t *job;
    int rc;

    if (!input || !out_job || !input->text || input->text[0] == '\0') {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    job = NULL;
    if (job_repository_memory_create(input, &job) != 0) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }

    if (!job_state_machine_can_transition_job(job->status, GENERATION_JOB_STATE_TOKENIZING)) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }

    job_event_service_emit_created(job);
    rc = job_pipeline_service_run(job);
    *out_job = job;
    return rc;
}

int generation_job_app_service_get(int job_id, const generation_job_t **out_job)
{
    generation_job_t *job;
    int rc;

    if (!out_job) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    rc = generation_job_app_service_load_job(job_id, &job);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }

    *out_job = job;
    return GENERATION_JOB_SERVICE_OK;
}

int generation_job_app_service_list_drafts(int job_id,
                                           const generation_card_draft_t **out_drafts,
                                           size_t *out_count)
{
    generation_card_draft_t *drafts;

    if (!out_drafts || !out_count) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }
    if (job_repository_memory_list_drafts(job_id, &drafts, out_count) != 0) {
        return GENERATION_JOB_SERVICE_ERR_NOT_FOUND;
    }

    *out_drafts = drafts;
    return GENERATION_JOB_SERVICE_OK;
}

int generation_job_app_service_approve(int job_id, int draft_id,
                                       const generation_job_t **out_job,
                                       const generation_card_draft_t **out_draft)
{
    generation_job_t *job;
    generation_card_draft_t *draft;
    int rc;

    if (!out_job || !out_draft) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    rc = generation_job_app_service_load_job(job_id, &job);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }
    rc = generation_job_app_service_load_draft(job, draft_id, &draft);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }

    rc = job_review_service_approve(job, draft);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }

    *out_job = job;
    *out_draft = draft;
    return GENERATION_JOB_SERVICE_OK;
}

int generation_job_app_service_reject(int job_id, int draft_id,
                                      const generation_job_t **out_job,
                                      const generation_card_draft_t **out_draft)
{
    generation_job_t *job;
    generation_card_draft_t *draft;
    int rc;

    if (!out_job || !out_draft) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    rc = generation_job_app_service_load_job(job_id, &job);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }
    rc = generation_job_app_service_load_draft(job, draft_id, &draft);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }

    rc = job_review_service_reject(job, draft);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }

    *out_job = job;
    *out_draft = draft;
    return GENERATION_JOB_SERVICE_OK;
}

int generation_job_app_service_regenerate(int job_id, int draft_id,
                                          const generation_job_t **out_job,
                                          const generation_card_draft_t **out_draft)
{
    generation_job_t *job;
    generation_card_draft_t *draft;
    int rc;

    if (!out_job || !out_draft) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    rc = generation_job_app_service_load_job(job_id, &job);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }
    rc = generation_job_app_service_load_draft(job, draft_id, &draft);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }

    rc = job_review_service_regenerate(job, draft);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }

    *out_job = job;
    *out_draft = draft;
    return GENERATION_JOB_SERVICE_OK;
}

int generation_job_app_service_cancel(int job_id, const generation_job_t **out_job)
{
    generation_job_t *job;
    int rc;

    if (!out_job) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    rc = generation_job_app_service_load_job(job_id, &job);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }

    rc = job_review_service_cancel(job);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }

    *out_job = job;
    return GENERATION_JOB_SERVICE_OK;
}
