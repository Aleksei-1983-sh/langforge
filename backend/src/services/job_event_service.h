#ifndef JOB_EVENT_SERVICE_H
#define JOB_EVENT_SERVICE_H

#include "domain/job/generation_job.h"

void job_event_service_emit_created(const generation_job_t *job);
void job_event_service_emit_progress(const generation_job_t *job,
                                     const char *step,
                                     int progress);
void job_event_service_emit_job(const generation_job_t *job,
                                const char *event_type);
void job_event_service_emit_draft(const generation_card_draft_t *draft,
                                  const char *event_type);

#endif
