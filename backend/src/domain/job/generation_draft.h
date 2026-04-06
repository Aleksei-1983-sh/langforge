#ifndef DOMAIN_JOB_GENERATION_DRAFT_H
#define DOMAIN_JOB_GENERATION_DRAFT_H

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

#endif
