#include "domain/job/job_state_machine.h"

#include "domain/job/generation_job.h"

#include <string.h>

static int job_state_equals(const char *left, const char *right)
{
    if (!left || !right) {
        return 0;
    }

    return strcmp(left, right) == 0;
}

int job_state_machine_can_transition_job(const char *current_status,
                                         const char *next_status)
{
    if (!current_status || !next_status) {
        return 0;
    }

    if (job_state_equals(current_status, GENERATION_JOB_STATE_QUEUED)) {
        return job_state_equals(next_status, GENERATION_JOB_STATE_TOKENIZING) ||
               job_state_equals(next_status, GENERATION_JOB_STATE_CANCELED) ||
               job_state_equals(next_status, GENERATION_JOB_STATE_FAILED);
    }
    if (job_state_equals(current_status, GENERATION_JOB_STATE_TOKENIZING)) {
        return job_state_equals(next_status, GENERATION_JOB_STATE_FILTERING_COMMON_WORDS) ||
               job_state_equals(next_status, GENERATION_JOB_STATE_CANCELED) ||
               job_state_equals(next_status, GENERATION_JOB_STATE_FAILED);
    }
    if (job_state_equals(current_status, GENERATION_JOB_STATE_FILTERING_COMMON_WORDS)) {
        return job_state_equals(next_status, GENERATION_JOB_STATE_CHECKING_DATABASE) ||
               job_state_equals(next_status, GENERATION_JOB_STATE_CANCELED) ||
               job_state_equals(next_status, GENERATION_JOB_STATE_FAILED);
    }
    if (job_state_equals(current_status, GENERATION_JOB_STATE_CHECKING_DATABASE)) {
        return job_state_equals(next_status, GENERATION_JOB_STATE_GENERATING) ||
               job_state_equals(next_status, GENERATION_JOB_STATE_CANCELED) ||
               job_state_equals(next_status, GENERATION_JOB_STATE_FAILED);
    }
    if (job_state_equals(current_status, GENERATION_JOB_STATE_GENERATING)) {
        return job_state_equals(next_status, GENERATION_JOB_STATE_REVIEW_READY) ||
               job_state_equals(next_status, GENERATION_JOB_STATE_COMPLETED) ||
               job_state_equals(next_status, GENERATION_JOB_STATE_CANCELED) ||
               job_state_equals(next_status, GENERATION_JOB_STATE_FAILED);
    }
    if (job_state_equals(current_status, GENERATION_JOB_STATE_REVIEW_READY)) {
        return job_state_equals(next_status, GENERATION_JOB_STATE_COMPLETED) ||
               job_state_equals(next_status, GENERATION_JOB_STATE_CANCELED) ||
               job_state_equals(next_status, GENERATION_JOB_STATE_FAILED);
    }
    if (job_state_equals(current_status, GENERATION_JOB_STATE_COMPLETED)) {
        return job_state_equals(next_status, GENERATION_JOB_STATE_REVIEW_READY);
    }

    return 0;
}

int job_state_machine_can_transition_draft(const char *current_status,
                                           const char *next_status)
{
    if (!current_status || !next_status) {
        return 0;
    }

    if (job_state_equals(current_status, GENERATION_DRAFT_STATUS_PENDING)) {
        return job_state_equals(next_status, GENERATION_DRAFT_STATUS_APPROVED) ||
               job_state_equals(next_status, GENERATION_DRAFT_STATUS_REJECTED) ||
               job_state_equals(next_status, GENERATION_DRAFT_STATUS_PENDING);
    }
    if (job_state_equals(current_status, GENERATION_DRAFT_STATUS_APPROVED) ||
        job_state_equals(current_status, GENERATION_DRAFT_STATUS_REJECTED)) {
        return job_state_equals(next_status, GENERATION_DRAFT_STATUS_PENDING);
    }

    return 0;
}

int job_state_machine_job_is_terminal(const char *status)
{
    if (!status) {
        return 0;
    }

    return job_state_equals(status, GENERATION_JOB_STATE_COMPLETED) ||
           job_state_equals(status, GENERATION_JOB_STATE_FAILED) ||
           job_state_equals(status, GENERATION_JOB_STATE_CANCELED);
}

int job_state_machine_job_can_be_reviewed(const char *status)
{
    if (!status) {
        return 0;
    }

    return job_state_equals(status, GENERATION_JOB_STATE_REVIEW_READY) ||
           job_state_equals(status, GENERATION_JOB_STATE_COMPLETED);
}

int job_state_machine_job_can_be_canceled(const char *status)
{
    if (!status) {
        return 0;
    }

    return !job_state_machine_job_is_terminal(status);
}

const char *job_state_machine_pipeline_final_status(size_t draft_count)
{
    if (draft_count == 0) {
        return GENERATION_JOB_STATE_COMPLETED;
    }

    return GENERATION_JOB_STATE_REVIEW_READY;
}
