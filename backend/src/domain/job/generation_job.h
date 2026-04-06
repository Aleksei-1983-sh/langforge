#ifndef DOMAIN_JOB_GENERATION_JOB_H
#define DOMAIN_JOB_GENERATION_JOB_H

#include <stddef.h>

#include "domain/job/generation_draft.h"

#define GENERATION_JOB_STATE_QUEUED "queued"
#define GENERATION_JOB_STATE_TOKENIZING "tokenizing"
#define GENERATION_JOB_STATE_FILTERING_COMMON_WORDS "filtering_common_words"
#define GENERATION_JOB_STATE_CHECKING_DATABASE "checking_database"
#define GENERATION_JOB_STATE_GENERATING "generating"
#define GENERATION_JOB_STATE_REVIEW_READY "review_ready"
#define GENERATION_JOB_STATE_COMPLETED "completed"
#define GENERATION_JOB_STATE_FAILED "failed"
#define GENERATION_JOB_STATE_CANCELED "canceled"

typedef struct {
    int job_id;
    int user_id;
    char *status;
    char *source_text;
    char *error_message;
    int total_words;
    int filtered_words;
    int existing_words;
    int generated_drafts;
    int reviewed_drafts;
    generation_card_draft_t *drafts;
    size_t draft_count;
} generation_job_t;

typedef struct {
    const char *text;
    int user_id;
} generation_job_create_input_t;

#endif
