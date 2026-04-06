#include "services/job_pipeline_service.h"

#include "domain/job/job_state_machine.h"
#include "internal_api/realtime_api.h"
#include "services/card_service.h"
#include "services/generate_service.h"
#include "services/generation_job_service.h"
#include "services/job_event_service.h"
#include "services/job_repository_memory.h"
#include "utils/tokenizer.h"

#include <stdlib.h>
#include <string.h>

static int is_common_word(const char *word)
{
    static const char *common_words[] = {
        "the", "a", "an", "and", "or", "but", "to", "of", "in", "on",
        "at", "for", "with", "from", "by", "is", "am", "are", "was",
        "were", "be", "been", "being", "it", "this", "that", "these",
        "those", "i", "you", "he", "she", "we", "they", "my", "your",
        "his", "her", "our", "their", "me", "him", "them", "as", "if",
        "than", "then", "so", "do", "does", "did", "have", "has", "had"
    };
    size_t i;

    if (!word || word[0] == '\0') {
        return 1;
    }
    if (strlen(word) <= 2) {
        return 1;
    }

    for (i = 0; i < sizeof(common_words) / sizeof(common_words[0]); i++) {
        if (strcmp(common_words[i], word) == 0) {
            return 1;
        }
    }

    return 0;
}

static int job_pipeline_service_set_status(generation_job_t *job,
                                           const char *status,
                                           int progress)
{
    if (!job_state_machine_can_transition_job(job->status, status)) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }
    if (job_repository_memory_update_job_status(job, status) != 0) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }

    job_event_service_emit_progress(job, status, progress);
    return GENERATION_JOB_SERVICE_OK;
}

static int job_pipeline_service_fail(generation_job_t *job,
                                     const char *error_message,
                                     int service_error)
{
    if (job_state_machine_can_transition_job(job->status, GENERATION_JOB_STATE_FAILED)) {
        job_repository_memory_update_job_status(job, GENERATION_JOB_STATE_FAILED);
    }
    job_repository_memory_update_job_error(job, error_message);
    job_event_service_emit_job(job, REALTIME_EVENT_GENERATION_JOB_FAILED);
    return service_error;
}

int job_pipeline_service_run(generation_job_t *job)
{
    char **words;
    char **candidates;
    int candidate_count;
    int word_count;
    int i;
    int rc;

    if (!job || !job->source_text) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    words = NULL;
    candidates = NULL;
    candidate_count = 0;
    word_count = 0;

    rc = job_pipeline_service_set_status(job, GENERATION_JOB_STATE_TOKENIZING, 10);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        return rc;
    }

    words = extract_unique_words(job->source_text, &word_count);
    if (!words && word_count != 0) {
        return job_pipeline_service_fail(job, "tokenization_failed",
                                         GENERATION_JOB_SERVICE_ERR_SERVER);
    }
    job->total_words = word_count;

    rc = job_pipeline_service_set_status(job,
                                         GENERATION_JOB_STATE_FILTERING_COMMON_WORDS,
                                         25);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        free_word_list(words, word_count);
        return rc;
    }

    if (word_count > 0) {
        candidates = calloc((size_t)word_count, sizeof(*candidates));
        if (!candidates) {
            free_word_list(words, word_count);
            return GENERATION_JOB_SERVICE_ERR_SERVER;
        }
    }

    for (i = 0; i < word_count; i++) {
        if (is_common_word(words[i])) {
            job->filtered_words++;
            continue;
        }

        candidates[candidate_count++] = words[i];
    }

    rc = job_pipeline_service_set_status(job,
                                         GENERATION_JOB_STATE_CHECKING_DATABASE,
                                         40);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        free(candidates);
        free_word_list(words, word_count);
        return rc;
    }

    if (job->user_id > 0) {
        int write_index;

        write_index = 0;
        for (i = 0; i < candidate_count; i++) {
            card_service_exists_query_t exists_query;
            int exists;

            exists = 0;
            exists_query.user_id = job->user_id;
            exists_query.word = candidates[i];
            if (card_service_exists(&exists_query, &exists) == CARD_SERVICE_OK && exists) {
                job->existing_words++;
                continue;
            }

            candidates[write_index++] = candidates[i];
        }

        candidate_count = write_index;
    }

    rc = job_pipeline_service_set_status(job, GENERATION_JOB_STATE_GENERATING, 60);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        free(candidates);
        free_word_list(words, word_count);
        return rc;
    }

    for (i = 0; i < candidate_count; i++) {
        generate_service_card_t card;
        generate_service_request_t request;
        generation_card_draft_t *draft;
        int progress;

        memset(&card, 0, sizeof(card));
        draft = NULL;

        request.word = candidates[i];
        request.user_id = job->user_id;
        request.persist_if_authenticated = 0;

        if (generate_service_generate(&request, &card) != GENERATE_SERVICE_OK) {
            generate_service_free_card(&card);
            free(candidates);
            free_word_list(words, word_count);
            return job_pipeline_service_fail(job, "llm_generation_failed",
                                             GENERATION_JOB_SERVICE_ERR_UPSTREAM);
        }

        if (job_repository_memory_append_draft(job, &card, &draft) != 0) {
            generate_service_free_card(&card);
            free(candidates);
            free_word_list(words, word_count);
            return GENERATION_JOB_SERVICE_ERR_SERVER;
        }

        job_event_service_emit_draft(draft, REALTIME_EVENT_GENERATION_CARD_DRAFT);
        progress = 60 + (int)(((i + 1) * 35) / (candidate_count > 0 ? candidate_count : 1));
        job_event_service_emit_progress(job, GENERATION_JOB_STATE_GENERATING, progress);
        generate_service_free_card(&card);
    }

    free(candidates);
    free_word_list(words, word_count);

    rc = job_repository_memory_update_job_status(job,
                                                 job_state_machine_pipeline_final_status(job->draft_count));
    if (rc != 0) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }

    if (strcmp(job->status, GENERATION_JOB_STATE_COMPLETED) == 0) {
        job_event_service_emit_job(job, REALTIME_EVENT_GENERATION_JOB_COMPLETED);
    } else {
        job_event_service_emit_progress(job, GENERATION_JOB_STATE_REVIEW_READY, 100);
    }

    return GENERATION_JOB_SERVICE_OK;
}
