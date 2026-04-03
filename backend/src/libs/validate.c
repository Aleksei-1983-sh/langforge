
#include <regex.h>
#include <string.h>
#include <stddef.h>

/* Максимальная длина email по практическим соображениям (RFC ограничивает 254) */
#define EMAIL_MAX_LEN 254

/* Возвращает 1 если email валиден, 0 если невалиден */
int is_valid_email(const char *email)
{
    if (!email) return 0;

    size_t len = strlen(email);
    if (len == 0 || len > EMAIL_MAX_LEN) return 0;

    /* быстрый запрещающий чек: пробелы внутри email — не допускаем */
    for (size_t i = 0; i < len; ++i) {
        if (email[i] == ' ' || email[i] == '\t' || email[i] == '\r' || email[i] == '\n') {
            return 0;
        }
    }

    /* Простой, практичный regex для email */
    const char *pattern = "^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}$";
    regex_t re;
    int rc;

    rc = regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB);
    if (rc != 0) {
        /* не получилось скомпилировать регекс — считаем невалидным */
        return 0;
    }

    rc = regexec(&re, email, 0, NULL, 0);
    regfree(&re);

    if (rc == 0) {
        return 1; /* совпадение */
    } else {
        return 0; /* нет совпадения или ошибка */
    }
}
