#ifndef DOMAIN_JOB_STATE_MACHINE_H
#define DOMAIN_JOB_STATE_MACHINE_H

#include <stddef.h>

int job_state_machine_can_transition_job(const char *current_status,
                                         const char *next_status);
int job_state_machine_can_transition_draft(const char *current_status,
                                           const char *next_status);
int job_state_machine_job_is_terminal(const char *status);
int job_state_machine_job_can_be_reviewed(const char *status);
int job_state_machine_job_can_be_canceled(const char *status);
const char *job_state_machine_pipeline_final_status(size_t draft_count);

#endif
