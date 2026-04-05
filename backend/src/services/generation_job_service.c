#include "services/generation_job_service.h"

#include "internal_api/realtime_api.h"
#include "libs/cJSON.h"
#include "services/card_service.h"
#include "services/generate_service.h"
#include "utils/tokenizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GENERATION_JOB_MAX_JOBS 128

static generation_job_t g_jobs[GENERATION_JOB_MAX_JOBS];
static size_t g_job_count = 0;
static int g_next_job_id = 1;
static int g_next_draft_id = 1;

static char *job_strdup(const char *value)
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

static generation_job_t *generation_job_find(int job_id)
{
    size_t i;

    for (i = 0; i < g_job_count; i++) {
        if (g_jobs[i].job_id == job_id) {
            return &g_jobs[i];
        }
    }

    return NULL;
}

static generation_card_draft_t *generation_job_find_draft(generation_job_t *job, int draft_id)
{
    size_t i;

    if (!job) {
        return NULL;
    }

    for (i = 0; i < job->draft_count; i++) {
        if (job->drafts[i].draft_id == draft_id) {
            return &job->drafts[i];
        }
    }

    return NULL;
}

static int generation_job_set_status(generation_job_t *job, const char *status)
{
    char *copy;

    if (!job || !status) {
        return -1;
    }

    copy = job_strdup(status);
    if (!copy) {
        return -1;
    }

    free(job->status);
    job->status = copy;
    return 0;
}

static int generation_job_set_error(generation_job_t *job, const char *message)
{
    char *copy = NULL;

    if (!job) {
        return -1;
    }

    if (message) {
        copy = job_strdup(message);
        if (!copy) {
            return -1;
        }
    }

    free(job->error_message);
    job->error_message = copy;
    return 0;
}

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

static char *build_progress_payload(const char *step, int progress, const char *status)
{
    cJSON *root;
    char *out;

    root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "step", step ? step : "");
    cJSON_AddNumberToObject(root, "progress", progress);
    if (status) {
        cJSON_AddStringToObject(root, "status", status);
    }

    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *build_job_payload(const generation_job_t *job)
{
    cJSON *root;
    char *out;

    if (!job) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddNumberToObject(root, "job_id", job->job_id);
    cJSON_AddStringToObject(root, "status", job->status ? job->status : "");
    cJSON_AddNumberToObject(root, "generated_drafts", job->generated_drafts);
    cJSON_AddNumberToObject(root, "reviewed_drafts", job->reviewed_drafts);
    if (job->error_message) {
        cJSON_AddStringToObject(root, "error", job->error_message);
    }

    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *build_draft_payload(const generation_card_draft_t *draft)
{
    cJSON *root;
    cJSON *examples;
    char *out;
    int i;

    if (!draft) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddNumberToObject(root, "draft_id", draft->draft_id);
    cJSON_AddNumberToObject(root, "job_id", draft->job_id);
    cJSON_AddStringToObject(root, "status", draft->status ? draft->status : "");
    cJSON_AddStringToObject(root, "word", draft->word ? draft->word : "");
    cJSON_AddStringToObject(root, "transcription", draft->transcription ? draft->transcription : "");
    cJSON_AddStringToObject(root, "translation", draft->translation ? draft->translation : "");
    if (draft->saved_card_id > 0) {
        cJSON_AddNumberToObject(root, "card_id", draft->saved_card_id);
    }

    examples = cJSON_AddArrayToObject(root, "examples");
    if (!examples) {
        cJSON_Delete(root);
        return NULL;
    }
    for (i = 0; i < 2; i++) {
        cJSON_AddItemToArray(examples, cJSON_CreateString(draft->examples[i] ? draft->examples[i] : ""));
    }

    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static void emit_progress_event(generation_job_t *job, const char *step, int progress)
{
    char *payload;

    if (!job) {
        return;
    }

    payload = build_progress_payload(step, progress, job->status);
    if (!payload) {
        return;
    }

    realtime_emit_event(REALTIME_EVENT_GENERATION_JOB_PROGRESS, job->job_id, payload);
    cJSON_free(payload);
}

static void emit_draft_event(generation_job_t *job, const generation_card_draft_t *draft, const char *event_type)
{
    char *payload;

    (void) job;

    payload = build_draft_payload(draft);
    if (!payload) {
        return;
    }

    realtime_emit_event(event_type, draft->job_id, payload);
    cJSON_free(payload);
}

static void emit_job_event(generation_job_t *job, const char *event_type)
{
    char *payload;

    if (!job) {
        return;
    }

    payload = build_job_payload(job);
    if (!payload) {
        return;
    }

    realtime_emit_event(event_type, job->job_id, payload);
    cJSON_free(payload);
}

static int generation_job_append_draft(generation_job_t *job, const generate_service_card_t *card)
{
    generation_card_draft_t *tmp;
    generation_card_draft_t *draft;

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
    draft->status = job_strdup(GENERATION_DRAFT_STATUS_PENDING);
    draft->word = job_strdup(card->word ? card->word : "");
    draft->transcription = job_strdup(card->transcription ? card->transcription : "");
    draft->translation = job_strdup(card->translation ? card->translation : "");
    draft->examples[0] = job_strdup(card->examples[0] ? card->examples[0] : "");
    draft->examples[1] = job_strdup(card->examples[1] ? card->examples[1] : "");

    if (!draft->status || !draft->word || !draft->transcription ||
        !draft->translation || !draft->examples[0] || !draft->examples[1]) {
        generation_draft_clear(draft);
        return -1;
    }

    job->draft_count++;
    job->generated_drafts = (int) job->draft_count;
    return 0;
}

static void generation_job_finalize_if_review_complete(generation_job_t *job)
{
    size_t i;

    if (!job || !job->status) {
        return;
    }
    if (strcmp(job->status, GENERATION_JOB_STATE_CANCELED) == 0 ||
        strcmp(job->status, GENERATION_JOB_STATE_FAILED) == 0 ||
        strcmp(job->status, GENERATION_JOB_STATE_COMPLETED) == 0) {
        return;
    }

    for (i = 0; i < job->draft_count; i++) {
        if (job->drafts[i].status &&
            strcmp(job->drafts[i].status, GENERATION_DRAFT_STATUS_PENDING) == 0) {
            return;
        }
    }

    if (generation_job_set_status(job, GENERATION_JOB_STATE_COMPLETED) == 0) {
        emit_job_event(job, REALTIME_EVENT_GENERATION_JOB_COMPLETED);
    }
}

static int generation_job_run_pipeline(generation_job_t *job)
{
    char **words = NULL;
    char **candidates = NULL;
    int word_count = 0;
    int candidate_count = 0;
    int i;

    if (!job || !job->source_text) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    if (generation_job_set_status(job, GENERATION_JOB_STATE_TOKENIZING) != 0) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }
    emit_progress_event(job, GENERATION_JOB_STATE_TOKENIZING, 10);

    words = extract_unique_words(job->source_text, &word_count);
    if (!words && word_count != 0) {
        generation_job_set_status(job, GENERATION_JOB_STATE_FAILED);
        generation_job_set_error(job, "tokenization_failed");
        emit_job_event(job, REALTIME_EVENT_GENERATION_JOB_FAILED);
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }
    job->total_words = word_count;

    if (generation_job_set_status(job, GENERATION_JOB_STATE_FILTERING_COMMON_WORDS) != 0) {
        free_word_list(words, word_count);
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }
    emit_progress_event(job, GENERATION_JOB_STATE_FILTERING_COMMON_WORDS, 25);

    if (word_count > 0) {
        candidates = calloc((size_t) word_count, sizeof(*candidates));
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

    if (generation_job_set_status(job, GENERATION_JOB_STATE_CHECKING_DATABASE) != 0) {
        free(candidates);
        free_word_list(words, word_count);
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }
    emit_progress_event(job, GENERATION_JOB_STATE_CHECKING_DATABASE, 40);

    if (job->user_id > 0) {
        int write_index = 0;
        for (i = 0; i < candidate_count; i++) {
            card_service_exists_query_t exists_query;
            int exists = 0;

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

    if (generation_job_set_status(job, GENERATION_JOB_STATE_GENERATING) != 0) {
        free(candidates);
        free_word_list(words, word_count);
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }
    emit_progress_event(job, GENERATION_JOB_STATE_GENERATING, 60);

    for (i = 0; i < candidate_count; i++) {
        generate_service_request_t request;
        generate_service_card_t card;
        int rc;
        int progress;

        memset(&card, 0, sizeof(card));
        request.word = candidates[i];
        request.user_id = job->user_id;
        request.persist_if_authenticated = 0;

        rc = generate_service_generate(&request, &card);
        if (rc != GENERATE_SERVICE_OK) {
            generation_job_set_status(job, GENERATION_JOB_STATE_FAILED);
            generation_job_set_error(job, "llm_generation_failed");
            generate_service_free_card(&card);
            free(candidates);
            free_word_list(words, word_count);
            emit_job_event(job, REALTIME_EVENT_GENERATION_JOB_FAILED);
            return GENERATION_JOB_SERVICE_ERR_UPSTREAM;
        }

        if (generation_job_append_draft(job, &card) != 0) {
            generate_service_free_card(&card);
            free(candidates);
            free_word_list(words, word_count);
            return GENERATION_JOB_SERVICE_ERR_SERVER;
        }

        emit_draft_event(job, &job->drafts[job->draft_count - 1], REALTIME_EVENT_GENERATION_CARD_DRAFT);
        progress = 60 + (int) (((i + 1) * 35) / (candidate_count > 0 ? candidate_count : 1));
        emit_progress_event(job, GENERATION_JOB_STATE_GENERATING, progress);
        generate_service_free_card(&card);
    }

    free(candidates);
    free_word_list(words, word_count);

    if (job->draft_count == 0) {
        if (generation_job_set_status(job, GENERATION_JOB_STATE_COMPLETED) != 0) {
            return GENERATION_JOB_SERVICE_ERR_SERVER;
        }
        emit_job_event(job, REALTIME_EVENT_GENERATION_JOB_COMPLETED);
        return GENERATION_JOB_SERVICE_OK;
    }

    if (generation_job_set_status(job, GENERATION_JOB_STATE_REVIEW_READY) != 0) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }
    emit_progress_event(job, GENERATION_JOB_STATE_REVIEW_READY, 100);
    return GENERATION_JOB_SERVICE_OK;
}

void generation_job_service_init(void)
{
    memset(g_jobs, 0, sizeof(g_jobs));
    g_job_count = 0;
    g_next_job_id = 1;
    g_next_draft_id = 1;
}

void generation_job_service_shutdown(void)
{
    size_t i;

    for (i = 0; i < g_job_count; i++) {
        generation_job_clear(&g_jobs[i]);
    }
    g_job_count = 0;
}

int generation_job_service_create(const generation_job_create_input_t *input,
                                  const generation_job_t **out_job)
{
    generation_job_t *job;

    if (!input || !out_job || !input->text || input->text[0] == '\0') {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }
    if (g_job_count >= GENERATION_JOB_MAX_JOBS) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }

    job = &g_jobs[g_job_count];
    memset(job, 0, sizeof(*job));
    job->job_id = g_next_job_id++;
    job->user_id = input->user_id;
    job->source_text = job_strdup(input->text);
    job->status = job_strdup(GENERATION_JOB_STATE_QUEUED);
    if (!job->source_text || !job->status) {
        generation_job_clear(job);
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }

    g_job_count++;
    *out_job = job;

    realtime_emit_event(REALTIME_EVENT_GENERATION_JOB_CREATED,
                        job->job_id,
                        "{\"status\":\"queued\"}");

    return generation_job_run_pipeline(job);
}

int generation_job_service_get(int job_id, const generation_job_t **out_job)
{
    generation_job_t *job;

    if (!out_job || job_id <= 0) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    job = generation_job_find(job_id);
    if (!job) {
        return GENERATION_JOB_SERVICE_ERR_NOT_FOUND;
    }

    *out_job = job;
    return GENERATION_JOB_SERVICE_OK;
}

int generation_job_service_list_drafts(int job_id,
                                       const generation_card_draft_t **out_drafts,
                                       size_t *out_count)
{
    generation_job_t *job;

    if (!out_drafts || !out_count) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    job = generation_job_find(job_id);
    if (!job) {
        return GENERATION_JOB_SERVICE_ERR_NOT_FOUND;
    }

    *out_drafts = job->drafts;
    *out_count = job->draft_count;
    return GENERATION_JOB_SERVICE_OK;
}

int generation_job_service_approve(int job_id, int draft_id,
                                   const generation_job_t **out_job,
                                   const generation_card_draft_t **out_draft)
{
    generation_job_t *job;
    generation_card_draft_t *draft;
    card_service_create_input_t create_input;
    int card_id = 0;
    char *status_copy;

    if (!out_job || !out_draft) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    job = generation_job_find(job_id);
    if (!job) {
        return GENERATION_JOB_SERVICE_ERR_NOT_FOUND;
    }
    draft = generation_job_find_draft(job, draft_id);
    if (!draft) {
        return GENERATION_JOB_SERVICE_ERR_NOT_FOUND;
    }
    if (job->user_id <= 0) {
        return GENERATION_JOB_SERVICE_ERR_UNAUTHORIZED;
    }
    if (strcmp(job->status, GENERATION_JOB_STATE_CANCELED) == 0 ||
        strcmp(job->status, GENERATION_JOB_STATE_FAILED) == 0) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }
    if (draft->status && strcmp(draft->status, GENERATION_DRAFT_STATUS_PENDING) != 0) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }

    create_input.user_id = job->user_id;
    create_input.word = draft->word ? draft->word : "";
    create_input.transcription = draft->transcription ? draft->transcription : "";
    create_input.translation = draft->translation ? draft->translation : "";
    create_input.example = draft->examples[0] ? draft->examples[0] : "";

    if (card_service_create(&create_input, &card_id) != CARD_SERVICE_OK) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }

    status_copy = job_strdup(GENERATION_DRAFT_STATUS_APPROVED);
    if (!status_copy) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }

    free(draft->status);
    draft->status = status_copy;
    draft->saved_card_id = card_id;
    job->reviewed_drafts++;

    emit_draft_event(job, draft, REALTIME_EVENT_GENERATION_CARD_SAVED);
    generation_job_finalize_if_review_complete(job);

    *out_job = job;
    *out_draft = draft;
    return GENERATION_JOB_SERVICE_OK;
}

int generation_job_service_reject(int job_id, int draft_id,
                                  const generation_job_t **out_job,
                                  const generation_card_draft_t **out_draft)
{
    generation_job_t *job;
    generation_card_draft_t *draft;
    char *status_copy;

    if (!out_job || !out_draft) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    job = generation_job_find(job_id);
    if (!job) {
        return GENERATION_JOB_SERVICE_ERR_NOT_FOUND;
    }
    draft = generation_job_find_draft(job, draft_id);
    if (!draft) {
        return GENERATION_JOB_SERVICE_ERR_NOT_FOUND;
    }
    if (draft->status && strcmp(draft->status, GENERATION_DRAFT_STATUS_PENDING) != 0) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }

    status_copy = job_strdup(GENERATION_DRAFT_STATUS_REJECTED);
    if (!status_copy) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }

    free(draft->status);
    draft->status = status_copy;
    job->reviewed_drafts++;
    generation_job_finalize_if_review_complete(job);

    *out_job = job;
    *out_draft = draft;
    return GENERATION_JOB_SERVICE_OK;
}

int generation_job_service_regenerate(int job_id, int draft_id,
                                      const generation_job_t **out_job,
                                      const generation_card_draft_t **out_draft)
{
    generation_job_t *job;
    generation_card_draft_t *draft;
    generate_service_request_t request;
    generate_service_card_t card;
    char *status_copy;
    char *word_copy;
    char *transcription_copy;
    char *translation_copy;
    char *example_0;
    char *example_1;

    if (!out_job || !out_draft) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    job = generation_job_find(job_id);
    if (!job) {
        return GENERATION_JOB_SERVICE_ERR_NOT_FOUND;
    }
    draft = generation_job_find_draft(job, draft_id);
    if (!draft) {
        return GENERATION_JOB_SERVICE_ERR_NOT_FOUND;
    }
    if (strcmp(job->status, GENERATION_JOB_STATE_CANCELED) == 0 ||
        strcmp(job->status, GENERATION_JOB_STATE_FAILED) == 0) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }

    memset(&card, 0, sizeof(card));
    request.word = draft->word;
    request.user_id = job->user_id;
    request.persist_if_authenticated = 0;

    if (generate_service_generate(&request, &card) != GENERATE_SERVICE_OK) {
        return GENERATION_JOB_SERVICE_ERR_UPSTREAM;
    }

    status_copy = job_strdup(GENERATION_DRAFT_STATUS_PENDING);
    word_copy = job_strdup(card.word ? card.word : "");
    transcription_copy = job_strdup(card.transcription ? card.transcription : "");
    translation_copy = job_strdup(card.translation ? card.translation : "");
    example_0 = job_strdup(card.examples[0] ? card.examples[0] : "");
    example_1 = job_strdup(card.examples[1] ? card.examples[1] : "");
    if (!status_copy || !word_copy || !transcription_copy || !translation_copy || !example_0 || !example_1) {
        free(status_copy);
        free(word_copy);
        free(transcription_copy);
        free(translation_copy);
        free(example_0);
        free(example_1);
        generate_service_free_card(&card);
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }

    if (draft->status && strcmp(draft->status, GENERATION_DRAFT_STATUS_PENDING) != 0 &&
        job->reviewed_drafts > 0) {
        job->reviewed_drafts--;
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
    draft->examples[0] = example_0;
    draft->examples[1] = example_1;
    draft->saved_card_id = 0;

    emit_draft_event(job, draft, REALTIME_EVENT_GENERATION_CARD_UPDATED);
    generate_service_free_card(&card);

    *out_job = job;
    *out_draft = draft;
    return GENERATION_JOB_SERVICE_OK;
}

int generation_job_service_cancel(int job_id, const generation_job_t **out_job)
{
    generation_job_t *job;

    if (!out_job) {
        return GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT;
    }

    job = generation_job_find(job_id);
    if (!job) {
        return GENERATION_JOB_SERVICE_ERR_NOT_FOUND;
    }
    if (strcmp(job->status, GENERATION_JOB_STATE_COMPLETED) == 0 ||
        strcmp(job->status, GENERATION_JOB_STATE_FAILED) == 0 ||
        strcmp(job->status, GENERATION_JOB_STATE_CANCELED) == 0) {
        return GENERATION_JOB_SERVICE_ERR_CONFLICT;
    }

    if (generation_job_set_status(job, GENERATION_JOB_STATE_CANCELED) != 0) {
        return GENERATION_JOB_SERVICE_ERR_SERVER;
    }
    emit_progress_event(job, GENERATION_JOB_STATE_CANCELED, 100);

    *out_job = job;
    return GENERATION_JOB_SERVICE_OK;
}
