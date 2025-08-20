function getTextId() {
  const params = new URLSearchParams(window.location.search);
  return params.get('id');
}

function highlightWord(word) {
  const content = document.getElementById('text-content');
  const regex = new RegExp(`\\b(${word})\\b`, 'gi');
  content.innerHTML = content.innerHTML.replace(/<span class="highlight">|<\/span>/g, '');
  content.innerHTML = content.innerHTML.replace(regex, '<span class="highlight">$1</span>');
}

document.addEventListener('DOMContentLoaded', () => {
  const textId = getTextId();

  // Загрузка текста
  fetch(`/api/texts/${textId}`)
    .then(res => res.json())
    .then(data => {
      document.getElementById('text-title').textContent = data.title;
      document.getElementById('text-content').textContent = data.body;
    });

  // Загрузка слов
  fetch(`/api/texts/${textId}/words`)
    .then(res => res.json())
    .then(data => {
      const wordList = document.getElementById('word-list');
      data.words.forEach(word => {
        const li = document.createElement('li');
        li.textContent = word.word;

        li.onclick = () => highlightWord(word.word);

        li.ondblclick = () => {
          fetch(`/api/words/${word.id}`)
            .then(res => res.json())
            .then(wordData => {
              document.getElementById('word-name').textContent = wordData.word;
              document.getElementById('word-translation').textContent = wordData.translation;
              document.getElementById('word-transcription').textContent = wordData.transcription;

              const examplesDiv = document.getElementById('word-examples');
              examplesDiv.innerHTML = '';
              wordData.examples.forEach(example => {
                const p = document.createElement('p');
                p.textContent = example;
                examplesDiv.appendChild(p);
              });

              document.getElementById('word-popup').classList.remove('hidden');
            });
        };

        wordList.appendChild(li);
      });
    });

  // Закрытие карточки слова
  document.getElementById('close-popup').onclick = () => {
    document.getElementById('word-popup').classList.add('hidden');
  };
});
