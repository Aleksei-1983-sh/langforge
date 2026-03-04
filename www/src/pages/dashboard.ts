// src/pages/dashboard.ts
import { generateCard, type WordCard } from '@/api/client';
import { mustFind, escapeHtml } from '@/lib/utils';

const input = mustFind<HTMLInputElement>('#word');
const btn = mustFind<HTMLButtonElement>('#gen');
const result = mustFind<HTMLDivElement>('#result');

function renderCardHtml(card: WordCard): string {
  const transcriptionHtml = card.transcription
    ? `<div class="transcription">/${escapeHtml(card.transcription)}/</div>`
    : '';

  const translationHtml = card.translation
    ? `<div class="translation">${escapeHtml(card.translation)}</div>`
    : '';

  let examplesHtml = '';
  if (Array.isArray(card.examples) && card.examples.length > 0) {
    examplesHtml = `<div class="examples"><h4>Examples</h4><ul>`;
    for (const ex of card.examples) {
      examplesHtml += `<li>${escapeHtml(ex)}</li>`;
    }
    examplesHtml += `</ul></div>`;
  }

  return `
    <article class="word-card">
      <header>
        <h3 class="word">${escapeHtml(card.word)}</h3>
        ${transcriptionHtml}
      </header>

      ${translationHtml}

      ${examplesHtml}
    </article>
  `;
}

btn.addEventListener('click', async () => {
  const word = input.value.trim();

  if (!word) {
    result.textContent = 'Please enter a word';
    return;
  }

  btn.disabled = true;
  result.innerHTML = `<div class="loading">Loading…</div>`;

  try {
    const card = await generateCard(word);
    result.innerHTML = renderCardHtml(card);
  } catch (err) {
    console.error(err);
    result.innerHTML = `<div class="error">${
      escapeHtml(err instanceof Error ? err.message : String(err))
    }</div>`;
  } finally {
    btn.disabled = false;
  }
});
