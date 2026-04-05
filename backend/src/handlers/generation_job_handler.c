#include "handlers/generation_job_handler.h"

#include "libs/cJSON.h"
#include "services/generation_job_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_positive_int(const char *value)
{
    long parsed;
    char *endptr;

    if (!value || *value == '\0') {
        return -1;
    }

    parsed = strtol(value, &endptr, 10);
    if (*endptr != '\0' || parsed <= 0 || parsed > 2147483647L) {
        return -1;
    }

    return (int) parsed;
}

static int send_json(http_connection_t *conn, int status, cJSON *json)
{
    char *text;
    int rc;

    if (!conn || !json) {
        return -1;
    }

    text = cJSON_PrintUnformatted(json);
    if (!text) {
        return -1;
    }

    rc = http_send_response(conn, status, "application/json", text, strlen(text));
    cJSON_free(text);
    return rc;
}

static cJSON *build_draft_json(const generation_card_draft_t *draft)
{
    cJSON *root;
    cJSON *examples;
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
    cJSON_AddNumberToObject(root, "user_id", draft->user_id);
    cJSON_AddNumberToObject(root, "saved_card_id", draft->saved_card_id);
    cJSON_AddStringToObject(root, "status", draft->status ? draft->status : "");
    cJSON_AddStringToObject(root, "word", draft->word ? draft->word : "");
    cJSON_AddStringToObject(root, "transcription", draft->transcription ? draft->transcription : "");
    cJSON_AddStringToObject(root, "translation", draft->translation ? draft->translation : "");

    examples = cJSON_AddArrayToObject(root, "examples");
    if (!examples) {
        cJSON_Delete(root);
        return NULL;
    }
    for (i = 0; i < 2; i++) {
        cJSON_AddItemToArray(examples, cJSON_CreateString(draft->examples[i] ? draft->examples[i] : ""));
    }

    return root;
}

static cJSON *build_job_json(const generation_job_t *job, int include_drafts)
{
    cJSON *root;
    cJSON *drafts;
    size_t i;

    if (!job) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddNumberToObject(root, "job_id", job->job_id);
    cJSON_AddNumberToObject(root, "user_id", job->user_id);
    cJSON_AddStringToObject(root, "status", job->status ? job->status : "");
    cJSON_AddStringToObject(root, "text", job->source_text ? job->source_text : "");
    cJSON_AddNumberToObject(root, "total_words", job->total_words);
    cJSON_AddNumberToObject(root, "filtered_words", job->filtered_words);
    cJSON_AddNumberToObject(root, "existing_words", job->existing_words);
    cJSON_AddNumberToObject(root, "generated_drafts", job->generated_drafts);
    cJSON_AddNumberToObject(root, "reviewed_drafts", job->reviewed_drafts);
    if (job->error_message) {
        cJSON_AddStringToObject(root, "error", job->error_message);
    }

    if (include_drafts) {
        drafts = cJSON_AddArrayToObject(root, "drafts");
        if (!drafts) {
            cJSON_Delete(root);
            return NULL;
        }
        for (i = 0; i < job->draft_count; i++) {
            cJSON *draft_json = build_draft_json(&job->drafts[i]);
            if (!draft_json) {
                cJSON_Delete(root);
                return NULL;
            }
            cJSON_AddItemToArray(drafts, draft_json);
        }
    } else {
        cJSON_AddNumberToObject(root, "draft_count", (double) job->draft_count);
    }

    return root;
}

static void send_service_error(http_connection_t *conn, int service_rc)
{
    const char *body;
    int status;

    switch (service_rc) {
    case GENERATION_JOB_SERVICE_ERR_INVALID_ARGUMENT:
        status = 400;
        body = "{\"error\":\"invalid request\"}";
        break;
    case GENERATION_JOB_SERVICE_ERR_NOT_FOUND:
        status = 404;
        body = "{\"error\":\"not found\"}";
        break;
    case GENERATION_JOB_SERVICE_ERR_UNAUTHORIZED:
        status = 401;
        body = "{\"error\":\"authentication required\"}";
        break;
    case GENERATION_JOB_SERVICE_ERR_CONFLICT:
        status = 409;
        body = "{\"error\":\"conflict\"}";
        break;
    case GENERATION_JOB_SERVICE_ERR_UPSTREAM:
        status = 502;
        body = "{\"error\":\"generation failed\"}";
        break;
    default:
        status = 500;
        body = "{\"error\":\"internal\"}";
        break;
    }

    http_send_response(conn, status, "application/json", body, strlen(body));
}

void handle_generation_jobs_create(http_connection_t *conn, http_request_t *req)
{
    generation_job_create_input_t input;
    const generation_job_t *job;
    cJSON *json;
    int rc;
    cJSON *root;
    cJSON *text_item;
    cJSON *user_id_item;

    if (!conn || !req || !req->body) {
        http_send_response(conn, 400, "application/json",
                           "{\"error\":\"empty request body\"}",
                           strlen("{\"error\":\"empty request body\"}"));
        return;
    }

    root = cJSON_Parse(req->body);
    if (!root) {
        http_send_response(conn, 400, "application/json",
                           "{\"error\":\"invalid json\"}",
                           strlen("{\"error\":\"invalid json\"}"));
        return;
    }

    text_item = cJSON_GetObjectItemCaseSensitive(root, "text");
    user_id_item = cJSON_GetObjectItemCaseSensitive(root, "user_id");
    if (!cJSON_IsString(text_item) || !text_item->valuestring || text_item->valuestring[0] == '\0') {
        cJSON_Delete(root);
        http_send_response(conn, 400, "application/json",
                           "{\"error\":\"missing 'text'\"}",
                           strlen("{\"error\":\"missing 'text'\"}"));
        return;
    }

    input.text = text_item->valuestring;
    input.user_id = cJSON_IsNumber(user_id_item) ? user_id_item->valueint : 0;
    rc = generation_job_service_create(&input, &job);
    cJSON_Delete(root);
    if (rc != GENERATION_JOB_SERVICE_OK) {
        send_service_error(conn, rc);
        return;
    }

    json = build_job_json(job, 1);
    if (!json) {
        http_send_response(conn, 500, "application/json",
                           "{\"error\":\"internal\"}",
                           strlen("{\"error\":\"internal\"}"));
        return;
    }

    send_json(conn, 201, json);
    cJSON_Delete(json);
}

static void handle_generation_job_get(http_connection_t *conn, int job_id)
{
    const generation_job_t *job;
    cJSON *json;
    int rc = generation_job_service_get(job_id, &job);

    if (rc != GENERATION_JOB_SERVICE_OK) {
        send_service_error(conn, rc);
        return;
    }

    json = build_job_json(job, 1);
    if (!json) {
        http_send_response(conn, 500, "application/json",
                           "{\"error\":\"internal\"}",
                           strlen("{\"error\":\"internal\"}"));
        return;
    }

    send_json(conn, 200, json);
    cJSON_Delete(json);
}

static void handle_generation_job_cards(http_connection_t *conn, int job_id)
{
    const generation_card_draft_t *drafts;
    size_t count;
    cJSON *root;
    cJSON *items;
    size_t i;
    int rc = generation_job_service_list_drafts(job_id, &drafts, &count);

    if (rc != GENERATION_JOB_SERVICE_OK) {
        send_service_error(conn, rc);
        return;
    }

    root = cJSON_CreateObject();
    if (!root) {
        http_send_response(conn, 500, "application/json",
                           "{\"error\":\"internal\"}",
                           strlen("{\"error\":\"internal\"}"));
        return;
    }

    cJSON_AddNumberToObject(root, "job_id", job_id);
    items = cJSON_AddArrayToObject(root, "items");
    if (!items) {
        cJSON_Delete(root);
        http_send_response(conn, 500, "application/json",
                           "{\"error\":\"internal\"}",
                           strlen("{\"error\":\"internal\"}"));
        return;
    }

    for (i = 0; i < count; i++) {
        cJSON *draft_json = build_draft_json(&drafts[i]);
        if (!draft_json) {
            cJSON_Delete(root);
            http_send_response(conn, 500, "application/json",
                               "{\"error\":\"internal\"}",
                               strlen("{\"error\":\"internal\"}"));
            return;
        }
        cJSON_AddItemToArray(items, draft_json);
    }

    send_json(conn, 200, root);
    cJSON_Delete(root);
}

static void handle_generation_job_draft_action(http_connection_t *conn,
                                               int job_id,
                                               int draft_id,
                                               const char *action)
{
    const generation_job_t *job = NULL;
    const generation_card_draft_t *draft = NULL;
    cJSON *root;
    int rc;

    if (strcmp(action, "approve") == 0) {
        rc = generation_job_service_approve(job_id, draft_id, &job, &draft);
    } else if (strcmp(action, "reject") == 0) {
        rc = generation_job_service_reject(job_id, draft_id, &job, &draft);
    } else if (strcmp(action, "regenerate") == 0) {
        rc = generation_job_service_regenerate(job_id, draft_id, &job, &draft);
    } else {
        http_send_response(conn, 404, "application/json",
                           "{\"error\":\"not found\"}",
                           strlen("{\"error\":\"not found\"}"));
        return;
    }

    if (rc != GENERATION_JOB_SERVICE_OK) {
        send_service_error(conn, rc);
        return;
    }

    root = cJSON_CreateObject();
    if (!root) {
        http_send_response(conn, 500, "application/json",
                           "{\"error\":\"internal\"}",
                           strlen("{\"error\":\"internal\"}"));
        return;
    }

    cJSON_AddItemToObject(root, "job", build_job_json(job, 0));
    cJSON_AddItemToObject(root, "draft", build_draft_json(draft));
    send_json(conn, 200, root);
    cJSON_Delete(root);
}

static void handle_generation_job_cancel(http_connection_t *conn, int job_id)
{
    const generation_job_t *job;
    cJSON *json;
    int rc = generation_job_service_cancel(job_id, &job);

    if (rc != GENERATION_JOB_SERVICE_OK) {
        send_service_error(conn, rc);
        return;
    }

    json = build_job_json(job, 0);
    if (!json) {
        http_send_response(conn, 500, "application/json",
                           "{\"error\":\"internal\"}",
                           strlen("{\"error\":\"internal\"}"));
        return;
    }

    send_json(conn, 200, json);
    cJSON_Delete(json);
}

void handle_generation_jobs_routes(http_connection_t *conn, http_request_t *req)
{
    const char *prefix = "/api/v1/generation-jobs/";
    const char *tail;
    char path_copy[256];
    char *segments[8];
    int segment_count = 0;
    char *token;
    char *saveptr;
    int job_id;
    int draft_id;

    if (!conn || !req) {
        return;
    }

    if (strncmp(req->path, prefix, strlen(prefix)) != 0) {
        http_send_response(conn, 404, "application/json",
                           "{\"error\":\"not found\"}",
                           strlen("{\"error\":\"not found\"}"));
        return;
    }

    tail = req->path + strlen(prefix);
    if (*tail == '\0') {
        http_send_response(conn, 404, "application/json",
                           "{\"error\":\"not found\"}",
                           strlen("{\"error\":\"not found\"}"));
        return;
    }

    snprintf(path_copy, sizeof(path_copy), "%s", tail);
    token = strtok_r(path_copy, "/", &saveptr);
    while (token && segment_count < (int) (sizeof(segments) / sizeof(segments[0]))) {
        segments[segment_count++] = token;
        token = strtok_r(NULL, "/", &saveptr);
    }

    if (segment_count < 1) {
        http_send_response(conn, 404, "application/json",
                           "{\"error\":\"not found\"}",
                           strlen("{\"error\":\"not found\"}"));
        return;
    }

    job_id = parse_positive_int(segments[0]);
    if (job_id <= 0) {
        http_send_response(conn, 400, "application/json",
                           "{\"error\":\"invalid job id\"}",
                           strlen("{\"error\":\"invalid job id\"}"));
        return;
    }

    if (strcmp(req->method, "GET") == 0 && segment_count == 1) {
        handle_generation_job_get(conn, job_id);
        return;
    }
    if (strcmp(req->method, "GET") == 0 && segment_count == 2 && strcmp(segments[1], "cards") == 0) {
        handle_generation_job_cards(conn, job_id);
        return;
    }
    if (strcmp(req->method, "POST") == 0 && segment_count == 2 && strcmp(segments[1], "cancel") == 0) {
        handle_generation_job_cancel(conn, job_id);
        return;
    }
    if (strcmp(req->method, "POST") == 0 && segment_count == 4 && strcmp(segments[1], "cards") == 0) {
        draft_id = parse_positive_int(segments[2]);
        if (draft_id <= 0) {
            http_send_response(conn, 400, "application/json",
                               "{\"error\":\"invalid draft id\"}",
                               strlen("{\"error\":\"invalid draft id\"}"));
            return;
        }
        handle_generation_job_draft_action(conn, job_id, draft_id, segments[3]);
        return;
    }

    http_send_response(conn, 404, "application/json",
                       "{\"error\":\"not found\"}",
                       strlen("{\"error\":\"not found\"}"));
}
