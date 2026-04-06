#include "services/generation_job_service.h"

#include "services/generation_job_app_service.h"

void generation_job_service_init(void)
{
    generation_job_app_service_init();
}

void generation_job_service_shutdown(void)
{
    generation_job_app_service_shutdown();
}

int generation_job_service_create(const generation_job_create_input_t *input,
                                  const generation_job_t **out_job)
{
    return generation_job_app_service_create(input, out_job);
}

int generation_job_service_get(int job_id, const generation_job_t **out_job)
{
    return generation_job_app_service_get(job_id, out_job);
}

int generation_job_service_list_drafts(int job_id,
                                       const generation_card_draft_t **out_drafts,
                                       size_t *out_count)
{
    return generation_job_app_service_list_drafts(job_id, out_drafts, out_count);
}

int generation_job_service_approve(int job_id, int draft_id,
                                   const generation_job_t **out_job,
                                   const generation_card_draft_t **out_draft)
{
    return generation_job_app_service_approve(job_id, draft_id, out_job, out_draft);
}

int generation_job_service_reject(int job_id, int draft_id,
                                  const generation_job_t **out_job,
                                  const generation_card_draft_t **out_draft)
{
    return generation_job_app_service_reject(job_id, draft_id, out_job, out_draft);
}

int generation_job_service_regenerate(int job_id, int draft_id,
                                      const generation_job_t **out_job,
                                      const generation_card_draft_t **out_draft)
{
    return generation_job_app_service_regenerate(job_id, draft_id, out_job, out_draft);
}

int generation_job_service_cancel(int job_id, const generation_job_t **out_job)
{
    return generation_job_app_service_cancel(job_id, out_job);
}
