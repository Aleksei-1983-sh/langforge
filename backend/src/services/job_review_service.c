#include "services/job_review_service.h"

#include "domain/job/job_state_machine.h"
#include "internal_api/realtime_api.h"
#include "services/card_service.h"
#include "services/generate_service.h"
#include "services/generation_job_service.h"
#include "services/job_event_service.h"
#include "services/job_repository_memory.h"

#include <string.h>

static int job_review_service_prepare_review_state(generation_job_t *job)
{
    if (!job) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }
    if (!job_state_machine_job_can_be_reviewed(job->status)) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }

    return GENERATION_JOB_SERVICE_OK;
}

int job_review_service_finalize_job(generation_job_t *job)
{
    size_t i;

    if (!job) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }
    if (!strcmp(job->status, GENERATION_JOB_STATE_CANCELED) ||
        !strcmp(job->status, GENERATION_JOB_STATE_FAILED) ||
        !strcmp(job->status, GENERATION_JOB_STATE_COMPLETED)) {
        return GENERATION_JOB_SERVICE_OK;
    }

    for (i = 0; i < job->draft_count; i++) {
        if (job->drafts[i].status &&
            strcmp(job->drafts[i].status, GENERATION_DRAFT_STATUS_PENDING) == 0) {
            return GENERATION_JOB_SERVICE_OK;
        }
    }

    if (!job_state_machine_can_transition_job(job->status, GENERATION_JOB_STATE_COMPLETED)) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }
    if (job_repository_memory_update_job_status(job, GENERATION_JOB_STATE_COMPLETED) != 0) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }

    job_event_service_emit_job(job, REALTIME_EVENT_GENERATION_JOB_COMPLETED);
    return GENERATION_JOB_SERVICE_OK;
}

int job_review_service_approve(generation_job_t *job, generation_card_draft_t *draft)
{
    card_service_create_input_t create_input;
    int card_id;
    int rc;

    if (!job || !draft) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    rc = job_review_service_prepare_review_state(job);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }
    if (job->user_id <= 0) {
        return GENERATION_JOB_SERVICE_ERR_UNAUTHORIZED;
    }
    if (!job_state_machine_can_transition_draft(draft->status, GENERATION_DRAFT_STATUS_APPROVED)) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }

    create_input.user_id = job->user_id;
    create_input.word = draft->word ? draft->word : "";
    create_input.transcription = draft->transcription ? draft->transcription : "";
    create_input.translation = draft->translation ? draft->translation : "";
    create_input.example = draft->examples[0] ? draft->examples[0] : "";

    card_id = 0;
    if (card_service_create(&create_input, &card_id) != CARD_SERVICE_OK) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }
    if (job_repository_memory_update_draft_status(draft, GENERATION_DRAFT_STATUS_APPROVED) != 0) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }

    draft->saved_card_id = card_id;
    job->reviewed_drafts++;
    job_event_service_emit_draft(draft, REALTIME_EVENT_GENERATION_CARD_SAVED);

    return job_review_service_finalize_job(job);
}

int job_review_service_reject(generation_job_t *job, generation_card_draft_t *draft)
{
    int rc;

    if (!job || !draft) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    rc = job_review_service_prepare_review_state(job);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }
    if (!job_state_machine_can_transition_draft(draft->status, GENERATION_DRAFT_STATUS_REJECTED)) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }
    if (job_repository_memory_update_draft_status(draft, GENERATION_DRAFT_STATUS_REJECTED) != 0) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }

    job->reviewed_drafts++;
    return job_review_service_finalize_job(job);
}

int job_review_service_regenerate(generation_job_t *job, generation_card_draft_t *draft)
{
    generate_service_card_t card;
    generate_service_request_t request;
    int was_reviewed;

    if (!job || !draft) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }
    if (!job_state_machine_job_can_be_reviewed(job->status)) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }
    if (strcmp(job->status, GENERATION_JOB_STATE_CANCELED) == 0 ||
        strcmp(job->status, GENERATION_JOB_STATE_FAILED) == 0) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }
    if (!job_state_machine_can_transition_draft(draft->status, GENERATION_DRAFT_STATUS_PENDING)) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }

    memset(&card, 0, sizeof(card));
    was_reviewed = strcmp(draft->status, GENERATION_DRAFT_STATUS_PENDING) != 0;
    request.word = draft->word;
    request.user_id = job->user_id;
    request.persist_if_authenticated = 0;

    if (generate_service_generate(&request, &card) != GENERATE_SERVICE_OK) {
        return GENERATION_JOB_SERVICE_ERR_UPSTREAM;
    }
    if (job_repository_memory_replace_draft(draft, &card) != 0) {
        generate_service_free_card(&card);
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }
    generate_service_free_card(&card);

    if (was_reviewed && job->reviewed_drafts > 0) {
        job->reviewed_drafts--;
    }

    if (strcmp(job->status, GENERATION_JOB_STATE_COMPLETED) == 0 &&
        job_state_machine_can_transition_job(job->status, GENERATION_JOB_STATE_REVIEW_READY)) {
        if (job_repository_memory_update_job_status(job, GENERATION_JOB_STATE_REVIEW_READY) != 0) {
            return GENERATION_JOB_SERVICE_ERR_SERVER;
        }
    }

    job_event_service_emit_draft(draft, REALTIME_EVENT_GENERATION_CARD_UPDATED);
    return GENERATION_JOB_SERVICE_OK;
}

int job_review_service_cancel(generation_job_t *job)
{
    if (!job) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }
    if (!job_state_machine_job_can_be_canceled(job->status)) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }
    if (!job_state_machine_can_transition_job(job->status, GENERATION_JOB_STATE_CANCELED)) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }
    if (job_repository_memory_update_job_status(job, GENERATION_JOB_STATE_CANCELED) != 0) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }

    job_event_service_emit_progress(job, GENERATION_JOB_STATE_CANCELED, 100);
    return GENERATION_JOB_SERVICE_OK;
}
