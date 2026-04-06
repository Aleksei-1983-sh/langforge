#ifndef JOB_REPOSITORY_MEMORY_H
#define JOB_REPOSITORY_MEMORY_H

#include <stddef.h>

#include "domain/job/generation_job.h"
#include "services/generate_service.h"

void job_repository_memory_init(void);
void job_repository_memory_shutdown(void);

int job_repository_memory_create(const generation_job_create_input_t *input,
                                 generation_job_t **out_job);
int job_repository_memory_get(int job_id, generation_job_t **out_job);
int job_repository_memory_list(generation_job_t **out_jobs, size_t *out_count);
int job_repository_memory_list_drafts(int job_id,
                                      generation_card_draft_t **out_drafts,
                                      size_t *out_count);
generation_card_draft_t *job_repository_memory_find_draft(generation_job_t *job,
                                                          int draft_id);
int job_repository_memory_update_job_status(generation_job_t *job,
                                            const char *status);
int job_repository_memory_update_job_error(generation_job_t *job,
                                           const char *message);
int job_repository_memory_update_draft_status(generation_card_draft_t *draft,
                                              const char *status);
int job_repository_memory_append_draft(generation_job_t *job,
                                       const generate_service_card_t *card,
                                       generation_card_draft_t **out_draft);
int job_repository_memory_replace_draft(generation_card_draft_t *draft,
                                        const generate_service_card_t *card);

#endif
