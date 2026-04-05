#ifndef GENERATION_JOB_SERVICE_H
#define GENERATION_JOB_SERVICE_H

#include <stddef.h>

#include "models/generation_job.h"

enum {
    GENERATION_JOB_SERVICE_OK = 0,
    GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT = -1,
    GENERATION_JOB_SERVICE_ERR_NOT_FOUND = -2,
    GENERATION_JOB_SERVICE_ERR_CONFLICT = -3,
    GENERATION_JOB_SERVICE_ERR_UNAUTHORIZED = -4,
    GENERATION_JOB_SERVICE_ERR_UPSTREAM = -5,
    GENERATION_JOB_SERVICE_ERR_SERVER = -6
};

void generation_job_service_init(void);
void generation_job_service_shutdown(void);

int generation_job_service_create(const generation_job_create_input_t *input,
                                  const generation_job_t **out_job);
int generation_job_service_get(int job_id, const generation_job_t **out_job);
int generation_job_service_list_drafts(int job_id,
                                       const generation_card_draft_t **out_drafts,
                                       size_t *out_count);
int generation_job_service_approve(int job_id, int draft_id,
                                   const generation_job_t **out_job,
                                   const generation_card_draft_t **out_draft);
int generation_job_service_reject(int job_id, int draft_id,
                                  const generation_job_t **out_job,
                                  const generation_card_draft_t **out_draft);
int generation_job_service_regenerate(int job_id, int draft_id,
                                      const generation_job_t **out_job,
                                      const generation_card_draft_t **out_draft);
int generation_job_service_cancel(int job_id, const generation_job_t **out_job);

#endif
