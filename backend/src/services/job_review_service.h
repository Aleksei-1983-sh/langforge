#ifndef JOB_REVIEW_SERVICE_H
#define JOB_REVIEW_SERVICE_H

#include "domain/job/generation_job.h"

int job_review_service_approve(generation_job_t *job, generation_card_draft_t *draft);
int job_review_service_reject(generation_job_t *job, generation_card_draft_t *draft);
int job_review_service_regenerate(generation_job_t *job, generation_card_draft_t *draft);
int job_review_service_cancel(generation_job_t *job);
int job_review_service_finalize_job(generation_job_t *job);

#endif
