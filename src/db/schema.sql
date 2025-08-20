-- schema.sql

-- Удаление таблиц в правильном порядке для переинициализации схемы
DROP TABLE IF EXISTS text_words;
DROP TABLE IF EXISTS words;
DROP TABLE IF EXISTS texts;
DROP TABLE IF EXISTS users;

-- Таблица пользователей
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    username TEXT NOT NULL UNIQUE,
    email TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Таблица текстов (каждый текст принадлежит пользователю)
CREATE TABLE texts (
    id SERIAL PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    title TEXT NOT NULL,
    content TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Таблица слов (каждое слово принадлежит пользователю)
CREATE TABLE words (
    id SERIAL PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    word TEXT NOT NULL,
    transcription TEXT,
    translation TEXT,
    example_1 TEXT,
    example_2 TEXT,
    UNIQUE(user_id, word)
);

-- Связующая таблица: многие-ко-многим между текстами и словами
CREATE TABLE text_words (
    text_id INTEGER NOT NULL REFERENCES texts(id) ON DELETE CASCADE,
    word_id INTEGER NOT NULL REFERENCES words(id) ON DELETE CASCADE,
    PRIMARY KEY (text_id, word_id)
);

