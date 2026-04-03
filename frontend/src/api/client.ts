// src/api/client.ts
import { z } from 'zod';

export interface WordExample {
  text: string;
}

export interface WordCard {
  word: string;
  transcription?: string;
  translation?: string;
  examples?: string[];   // ← ВАЖНО
}

const WordExampleSchema = z.object({
  text: z.string()
});

const WordCardSchema = z.object({
  word: z.string(),
  transcription: z.string().optional(),
  translation: z.string().optional(),
  examples: z.array(z.string()).optional()
});

/**
 * Запрашивает у backend'а карточку слова.
 * Отправляет { "word": "<word>" } в POST и ожидает структуру,
 * описанную в WordCardSchema.
 */
export async function generateCard(word: string): Promise<WordCard> {
  const res = await fetch('/api/v1/generate_card', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    credentials: 'include',
    body: JSON.stringify({ word })
  });

  if (!res.ok) {
    const txt = await res.text();
    throw new Error(`API error ${res.status}: ${txt}`);
  }

  const json = await res.json();
  const parsed = WordCardSchema.safeParse(json);
  if (!parsed.success) {
    console.error('Invalid API response', parsed.error, 'payload:', json);
    throw new Error('Invalid API response shape');
  }
  return parsed.data;
}

/* --- Auth helpers (пример, адаптируйте под бекенд) --- */

const AuthResponse = z.object({ token: z.string().optional(), error: z.string().optional() });

export async function login(
  username: string,
  password: string
): Promise<{ success: boolean; user_id?: number }> {
  const res = await fetch('/api/v1/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    credentials: 'include', // если используешь cookie-сессию
    body: JSON.stringify({ username, password })
  });

  if (!res.ok) {
    const txt = await res.text();
    throw new Error(`Login failed: ${res.status} ${txt}`);
  }

  return res.json();
}

export async function postJson<T>(url: string, data: unknown): Promise<T> {
  const res = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    credentials: 'include',   // если есть cookie-сессии
    body: JSON.stringify(data)
  });

  if (!res.ok) {
    throw new Error(`HTTP ${res.status}`);
  }

  return res.json();
}

export async function register(
  username: string,
  email: string,
  password: string
): Promise<void> {

  await postJson('/api/v1/register', {
    username,
    email,
    password
  });
}
