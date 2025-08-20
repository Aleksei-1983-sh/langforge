
#ifndef DBUG_H
#define DBUG_H

#include <stdio.h>
#include <time.h>
#define DEBUG_LOG_FILE "./src/dbug/debug.log"

#define DEBUG_ROUTER
#define DEBUG_DB
#define DEBUG_CARD_HANDLER
#define DEBUG_MAIN
#define DEBUG_TEXT_HANDLER
#define DEBUG_HTTP

#define ERROR_PRINT_DB(fmt, ...) do { \
    FILE *f = fopen(DEBUG_LOG_FILE, "a"); \
    if (f) { \
        fprintf(f, "DEBUG: %s:%s[%d]: " fmt "\n", \
            __FILE__, __func__, __LINE__, ##__VA_ARGS__); \
        fclose(f); \
    } \
} while(0)

#ifdef DEBUG_ROUTER
#define DEBUG_PRINT_ROUTER(fmt, ...) do { \
    FILE *f = fopen(DEBUG_LOG_FILE, "a"); \
    if (f) { \
        fprintf(f, "DEBUG: %s:%s[%d]: " fmt "\n", \
            __FILE__, __func__, __LINE__, ##__VA_ARGS__); \
        fclose(f); \
    } \
} while(0)
#else
#define DEBUG_PRINT_ROUTER(fmt, ...) ((void)0)
#endif


#ifdef DEBUG_DB
#define DEBUG_PRINT_DB(fmt, ...) do { \
    FILE *f = fopen(DEBUG_LOG_FILE, "a"); \
    if (f) { \
        fprintf(f, "DEBUG: %s:%s[%d]: " fmt "\n", \
            __FILE__, __func__, __LINE__, ##__VA_ARGS__); \
        fclose(f); \
    } \
} while(0)
#else
#define DEBUG_PRINT_DB(fmt, ...) ((void)0)
#endif

#ifdef DEBUG_CARD_HANDLER
#define DEBUG_PRINT_CARD_HANDLER(fmt, ...) do { \
    FILE *f = fopen(DEBUG_LOG_FILE, "a"); \
    if (f) { \
        fprintf(f, "DEBUG: %s:%s[%d]: " fmt "\n", \
            __FILE__, __func__, __LINE__, ##__VA_ARGS__); \
        fclose(f); \
    } \
} while(0)
#else
#define DEBUG_PRINT_CARD_HANDLER(fmt, ...) ((void)0)
#endif

#ifdef DEBUG_MAIN
#define DEBUG_PRINT_MAIN(fmt, ...) do { \
    FILE *f = fopen(DEBUG_LOG_FILE, "a"); \
    if (f) { \
        fprintf(f, "DEBUG: %s:%s[%d]: " fmt "\n", \
            __FILE__, __func__, __LINE__, ##__VA_ARGS__); \
        fclose(f); \
    } \
} while(0)
#else
#define DEBUG_PRINT_MAIN(fmt, ...) ((void)0)
#endif

//DT
#ifdef DEBUG_HTTP
#define DEBUG_LOG_HTTP(hm) do { \
    char method[16], uri[128], body[512]; \
    mg_snprintf(method, sizeof(method), "%.*s", (int)(hm)->method.len, (hm)->method.buf); \
    mg_snprintf(uri, sizeof(uri), "%.*s", (int)(hm)->uri.len, (hm)->uri.buf); \
    mg_snprintf(body, sizeof(body), "%.*s", (int)(hm)->body.len < 511 ? (int)(hm)->body.len : 511, (hm)->body.buf); \
    \
    time_t now = time(NULL); \
    struct tm *t = localtime(&now); \
    char timestamp[64]; \
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t); \
    \
    FILE *f = fopen(DEBUG_LOG_FILE, "a"); \
    if (f) { \
        fprintf(f, "[%s] API call: %s %s\n", timestamp, method, uri); \
        if ((hm)->body.len > 0) \
            fprintf(f, "  Body: %s\n", body); \
        fclose(f); \
    } \
} while (0)
#else
#define DEBUG_LOG_HTTP(hm) ((void)0)
#endif

#endif //DBUG_H
