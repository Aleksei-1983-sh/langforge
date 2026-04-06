#include "services/job_repository_memory.h"

#include <stdlib.h>
#include <string.h>

#define GENERATION_JOB_MAX_JOBS 128

static generation_job_t g_jobs[GENERATION_JOB_MAX_JOBS];
static size_t g_job_count = 0;
static int g_next_job_id = 1;
static int g_next_draft_id = 1;

static char *job_repository_memory_strdup(const char *value)
{
    if (!value) {
        return NULL;
    }

    return strdup(value);
}

static void generation_draft_clear(generation_card_draft_t *draft)
{
    int i;

    if (!draft) {
        return;
    }

    free(draft->status);
    free(draft->word);
    free(draft->transcription);
    free(draft->translation);
    for (i = 0; i < 2; i++) {
        free(draft->examples[i]);
        draft->examples[i] = NULL;
    }

    memset(draft, 0, sizeof(*draft));
}

static void generation_job_clear(generation_job_t *job)
{
    size_t i;

    if (!job) {
        return;
    }

    free(job->status);
    free(job->source_text);
    free(job->error_message);

    for (i = 0; i < job->draft_count; i++) {
        generation_draft_clear(&job->drafts[i]);
    }

    free(job->drafts);
    memset(job, 0, sizeof(*job));
}

void job_repository_memory_init(void)
{
    memset(g_jobs, 0, sizeof(g_jobs));
    g_job_count = 0;
    g_next_job_id = 1;
    g_next_draft_id = 1;
}

void job_repository_memory_shutdown(void)
{
    size_t i;

    for (i = 0; i < g_job_count; i++) {
        generation_job_clear(&g_jobs[i]);
    }

    g_job_count = 0;
}

int job_repository_memory_create(const generation_job_create_input_t *input,
                                 generation_job_t **out_job)
{
    generation_job_t *job;

    if (!input || !out_job || !input->text || input->text[0] == '\0') {
        return -1;
    }
    if (g_job_count >= GENERATION_JOB_MAX_JOBS) {
        return -1;
    }

    job = &g_jobs[g_job_count];
    memset(job, 0, sizeof(*job));

    job->job_id = g_next_job_id++;
    job->user_id = input->user_id;
    job->source_text = job_repository_memory_strdup(input->text);
    job->status = job_repository_memory_strdup(GENERATION_JOB_STATE_QUEUED);
    if (!job->source_text || !job->status) {
        generation_job_clear(job);
        return -1;
    }

    g_job_count++;
    *out_job = job;
    return 0;
}

int job_repository_memory_get(int job_id, generation_job_t **out_job)
{
    size_t i;

    if (!out_job || job_id <= 0) {
        return -1;
    }

    for (i = 0; i < g_job_count; i++) {
        if (g_jobs[i].job_id == job_id) {
            *out_job = &g_jobs[i];
            return 0;
        }
    }

    return -1;
}

int job_repository_memory_list(generation_job_t **out_jobs, size_t *out_count)
{
    if (!out_jobs || !out_count) {
        return -1;
    }

    *out_jobs = g_jobs;
    *out_count = g_job_count;
    return 0;
}

int job_repository_memory_list_drafts(int job_id,
                                      generation_card_draft_t **out_drafts,
                                      size_t *out_count)
{
    generation_job_t *job;

    if (!out_drafts || !out_count) {
        return -1;
    }
    if (job_repository_memory_get(job_id, &job) != 0) {
        return -1;
    }

    *out_drafts = job->drafts;
    *out_count = job->draft_count;
    return 0;
}

generation_card_draft_t *job_repository_memory_find_draft(generation_job_t *job,
                                                          int draft_id)
{
    size_t i;

    if (!job || draft_id <= 0) {
        return NULL;
    }

    for (i = 0; i < job->draft_count; i++) {
        if (job->drafts[i].draft_id == draft_id) {
            return &job->drafts[i];
        }
    }

    return NULL;
}

int job_repository_memory_update_job_status(generation_job_t *job,
                                            const char *status)
{
    char *copy;

    if (!job || !status) {
        return -1;
    }

    copy = job_repository_memory_strdup(status);
    if (!copy) {
        return -1;
    }

    free(job->status);
    job->status = copy;
    return 0;
}

int job_repository_memory_update_job_error(generation_job_t *job,
                                           const char *message)
{
    char *copy = NULL;

    if (!job) {
        return -1;
    }

    if (message) {
        copy = job_repository_memory_strdup(message);
        if (!copy) {
            return -1;
        }
    }

    free(job->error_message);
    job->error_message = copy;
    return 0;
}

int job_repository_memory_update_draft_status(generation_card_draft_t *draft,
                                              const char *status)
{
    char *copy;

    if (!draft || !status) {
        return -1;
    }

    copy = job_repository_memory_strdup(status);
    if (!copy) {
        return -1;
    }

    free(draft->status);
    draft->status = copy;
    return 0;
}

int job_repository_memory_append_draft(generation_job_t *job,
                                       const generate_service_card_t *card,
                                       generation_card_draft_t **out_draft)
{
    generation_card_draft_t *draft;
    generation_card_draft_t *tmp;

    if (!job || !card) {
        return -1;
    }

    tmp = realloc(job->drafts, (job->draft_count + 1) * sizeof(*job->drafts));
    if (!tmp) {
        return -1;
    }

    job->drafts = tmp;
    draft = &job->drafts[job->draft_count];
    memset(draft, 0, sizeof(*draft));

    draft->job_id = job->job_id;
    draft->draft_id = g_next_draft_id++;
    draft->user_id = job->user_id;
    draft->status = job_repository_memory_strdup(GENERATION_DRAFT_STATUS_PENDING);
    draft->word = job_repository_memory_strdup(card->word ? card->word : "");
    draft->transcription = job_repository_memory_strdup(card->transcription ? card->transcription : "");
    draft->translation = job_repository_memory_strdup(card->translation ? card->translation : "");
    draft->examples[0] = job_repository_memory_strdup(card->examples[0] ? card->examples[0] : "");
    draft->examples[1] = job_repository_memory_strdup(card->examples[1] ? card->examples[1] : "");

    if (!draft->status || !draft->word || !draft->transcription ||
        !draft->translation || !draft->examples[0] || !draft->examples[1]) {
        generation_draft_clear(draft);
        return -1;
    }

    job->draft_count++;
    job->generated_drafts = (int)job->draft_count;

    if (out_draft) {
        *out_draft = draft;
    }

    return 0;
}

int job_repository_memory_replace_draft(generation_card_draft_t *draft,
                                        const generate_service_card_t *card)
{
    char *status_copy;
    char *word_copy;
    char *transcription_copy;
    char *translation_copy;
    char *example_0_copy;
    char *example_1_copy;

    if (!draft || !card) {
        return -1;
    }

    status_copy = job_repository_memory_strdup(GENERATION_DRAFT_STATUS_PENDING);
    word_copy = job_repository_memory_strdup(card->word ? card->word : "");
    transcription_copy = job_repository_memory_strdup(card->transcription ? card->transcription : "");
    translation_copy = job_repository_memory_strdup(card->translation ? card->translation : "");
    example_0_copy = job_repository_memory_strdup(card->examples[0] ? card->examples[0] : "");
    example_1_copy = job_repository_memory_strdup(card->examples[1] ? card->examples[1] : "");
    if (!status_copy || !word_copy || !transcription_copy ||
        !translation_copy || !example_0_copy || !example_1_copy) {
        free(status_copy);
        free(word_copy);
        free(transcription_copy);
        free(translation_copy);
        free(example_0_copy);
        free(example_1_copy);
        return -1;
    }

    free(draft->status);
    free(draft->word);
    free(draft->transcription);
    free(draft->translation);
    free(draft->examples[0]);
    free(draft->examples[1]);

    draft->status = status_copy;
    draft->word = word_copy;
    draft->transcription = transcription_copy;
    draft->translation = translation_copy;
    draft->examples[0] = example_0_copy;
    draft->examples[1] = example_1_copy;
    draft->saved_card_id = 0;

    return 0;
}
