#ifndef GENERATION_JOB_H
#define GENERATION_JOB_H

#include <stddef.h>

#define GENERATION_JOB_STATE_QUEUED "queued"
#define GENERATION_JOB_STATE_TOKENIZING "tokenizing"
#define GENERATION_JOB_STATE_FILTERING_COMMON_WORDS "filtering_common_words"
#define GENERATION_JOB_STATE_CHECKING_DATABASE "checking_database"
#define GENERATION_JOB_STATE_GENERATING "generating"
#define GENERATION_JOB_STATE_REVIEW_READY "review_ready"
#define GENERATION_JOB_STATE_COMPLETED "completed"
#define GENERATION_JOB_STATE_FAILED "failed"
#define GENERATION_JOB_STATE_CANCELED "canceled"

#define GENERATION_DRAFT_STATUS_PENDING "pending"
#define GENERATION_DRAFT_STATUS_APPROVED "approved"
#define GENERATION_DRAFT_STATUS_REJECTED "rejected"

typedef struct {
    int job_id;
    int draft_id;
    int user_id;
    int saved_card_id;
    char *status;
    char *word;
    char *transcription;
    char *translation;
    char *examples[2];
} generation_card_draft_t;

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
