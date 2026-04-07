#ifndef GENERATION_JOB_APP_SERVICE_H
#define GENERATION_JOB_APP_SERVICE_H

#include "domain/job/generation_job.h"

void generation_job_app_service_init(void);
void generation_job_app_service_shutdown(void);

//создает джобу которая запускает пайплай обработку закинутого текста 
int generation_job_app_service_create(const generation_job_create_input_t *input,
                                      const generation_job_t **out_job);
int generation_job_app_service_get(int job_id, const generation_job_t **out_job);
int generation_job_app_service_list_drafts(int job_id,
                                           const generation_card_draft_t **out_drafts,
                                           size_t *out_count);
int generation_job_app_service_approve(int job_id, int draft_id,
                                       const generation_job_t **out_job,
                                       const generation_card_draft_t **out_draft);
int generation_job_app_service_reject(int job_id, int draft_id,
                                      const generation_job_t **out_job,
                                      const generation_card_draft_t **out_draft);
int generation_job_app_service_regenerate(int job_id, int draft_id,
                                          const generation_job_t **out_job,
                                          const generation_card_draft_t **out_draft);
int generation_job_app_service_cancel(int job_id, const generation_job_t **out_job);

#endif
