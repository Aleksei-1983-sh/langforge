#ifndef TOKENIZER_H
#define TOKENIZER_H

// Возвращает список уникальных слов из текста.
// Выделяет память, caller должен освободить.
char **extract_unique_words(const char *text, int *count);

void free_word_list(char **words, int count);

#endif // TOKENIZER_H
