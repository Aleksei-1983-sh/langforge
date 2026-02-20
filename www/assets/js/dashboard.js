
// dashboard.js
document.addEventListener('DOMContentLoaded', () => {
  const userNameEl = document.getElementById('user-name');
  const welcomeEl = document.getElementById('user-welcome');
  const logoutBtn = document.getElementById('logout-btn');
  const errorEl = document.getElementById('error-msg');
  const statsWords = document.getElementById('stat-words');
  const statsLessons = document.getElementById('stat-lessons');

  // Конфигурация retry/timing
  const MAX_RETRIES = 5;
  const BASE_DELAY_MS = 2000; // базовая задержка для экспоненциального бэкаффа
  const FETCH_TIMEOUT_MS = 7000; // таймаут одного fetch-запроса
  let retryCount = 0;
  let isLoggedIn = false;
  let periodicRefreshId = null;

  // Утилиты UI
  function showTemporaryError(msg, persist = false) {
    if (!errorEl) {
      // Если элемента нет — лог в консоль и return
      console.warn('error element not found:', msg);
      return;
    }
    errorEl.textContent = msg;

    // Добавим кнопку "Повторить", если надо
    let retryBtn = errorEl.querySelector('.retry-btn');
    if (!retryBtn) {
      retryBtn = document.createElement('button');
      retryBtn.type = 'button';
      retryBtn.className = 'retry-btn';
      retryBtn.textContent = 'Повторить';
      retryBtn.addEventListener('click', () => {
        hideError();
        retryCount = 0;
        fetchProfile();
      });
      errorEl.appendChild(document.createTextNode(' '));
      errorEl.appendChild(retryBtn);
    }

    errorEl.style.display = 'block';

    if (!persist) {
      setTimeout(() => {
        // если за это время статус не изменился — скрываем
        if (errorEl) errorEl.style.display = 'none';
      }, 5000);
    }
  }

  function hideError() {
    if (errorEl) {
      errorEl.style.display = 'none';
      errorEl.textContent = '';
    }
  }

  function showLoggedOutUI(reason) {
    isLoggedIn = false;
    hideError();
    // Очистим личные данные с UI
    if (userNameEl) userNameEl.textContent = '';
    if (welcomeEl) welcomeEl.textContent = 'Гость';
    if (statsWords) statsWords.textContent = '—';
    if (statsLessons) statsLessons.textContent = '—';

    // Показать сообщение о том, что нужна повторная авторизация (но не редирект).
    showTemporaryError(reason || 'Требуется повторная авторизация.', true);

    // Остановим периодическое обновление, если было
    if (periodicRefreshId) {
      clearInterval(periodicRefreshId);
      periodicRefreshId = null;
    }
  }

  function startPeriodicRefresh() {
    if (periodicRefreshId) return;
    // Обновляем профиль каждые 60сек, только если залогинен
    periodicRefreshId = setInterval(() => {
      if (isLoggedIn) fetchProfile();
    }, 60000);
  }

  // Выполняет fetch с AbortController + timeout
  async function fetchWithTimeout(url, opts = {}, timeout = FETCH_TIMEOUT_MS) {
    const controller = new AbortController();
    const id = setTimeout(() => controller.abort(), timeout);
    try {
      const res = await fetch(url, { ...opts, signal: controller.signal });
      return res;
    } finally {
      clearTimeout(id);
    }
  }

  // Основная логика получения профиля
  async function fetchProfile() {
    // Если уже слишком много попыток — покажем persistent сообщение и остановимся
    if (retryCount > MAX_RETRIES) {
      showLoggedOutUI('Не удалось получить профиль — попробуйте позже или войдите вручную.');
      return;
    }

    try {
      const resp = await fetchWithTimeout('/api/me', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        // Оставляем include — важно для отправки cookie (HttpOnly/secure cookies по-прежнему не видимые в JS, но идут в запросе)
        credentials: 'include',
        cache: 'no-store'
      });

      // Обработка статусов без редиректа
      if (resp.status === 401 || resp.status === 403) {
        console.warn('Unauthorized (401/403) — not redirecting, showing logged-out UI');
        showLoggedOutUI('Требуется вход в систему. Пожалуйста, выполните повторную авторизацию.');
        return;
      }

      if (!resp.ok) {
        console.error('Server responded with error:', resp.status);
        // Считаем это временной ошибкой — пробуем повторить с бэкаффом
        retryCount++;
        const delay = BASE_DELAY_MS * Math.pow(2, retryCount - 1);
        showTemporaryError(`Сервер временно недоступен (код ${resp.status}). Повтор через ${Math.round(delay/1000)}с`, true);
        setTimeout(fetchProfile, delay);
        return;
      }

      // Успешный ответ — пытаемся прочитать JSON
      let data;
      try {
        data = await resp.json();
      } catch (e) {
        console.error('JSON parse error', e);
        showTemporaryError('Некорректный ответ сервера. Повторяем попытку...');
        retryCount++;
        setTimeout(fetchProfile, BASE_DELAY_MS);
        return;
      }

      // Проверяем payload
      if (data && data.user) {
        // Успешная аутентификация — обновляем UI
        isLoggedIn = true;
        retryCount = 0;
        hideError();

        if (userNameEl) userNameEl.textContent = data.user.username || '';
        if (welcomeEl) welcomeEl.textContent = data.user.username ? `Привет, ${data.user.username}` : 'Привет';
        if (statsWords) statsWords.textContent = String(data.user.words_learned || 0);
        if (statsLessons) statsLessons.textContent = String(data.user.active_lessons || 0);

        // Запускаем периодическое обновление профиля
        startPeriodicRefresh();
      } else {
        // Если нет данных — считаем, что сессия недействительна, но НЕ редиректим
        console.warn('No user data in /api/me response — treating as logged out');
        showLoggedOutUI('Сессия недействительна. Пожалуйста, войдите снова.');
      }
    } catch (err) {
      // Ошибка сети или таймаут
      if (err && err.name === 'AbortError') {
        console.warn('Fetch aborted (timeout). Will retry.');
      } else {
        console.error('Network or unexpected error:', err);
      }
      retryCount++;
      const delay = BASE_DELAY_MS * Math.pow(2, Math.min(retryCount - 1, 4));
      showTemporaryError('Проблемы с соединением. Попытка повторного запроса...', true);
      setTimeout(fetchProfile, delay);
    }
  }

  // Обработчик логаута — не редиректим, а очищаем UI и показываем сообщение
  if (logoutBtn) {
    logoutBtn.addEventListener('click', async () => {
      try {
        await fetch('/api/logout', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          credentials: 'include'
        });
      } catch (e) {
        console.warn('Logout request failed, but we still clear local state.', e);
      } finally {
        // Переводим страницу в состояние "вышли", но НЕ переходим на /login/
        showLoggedOutUI('Вы успешно вышли. Чтобы снова войти — используйте форму входа.');
        // (опционально) показать кнопку/ссылку для ручной навигации к странице входа, если пользователь захочет.
        // НО автоматических редиректов нет.
      }
    });
  }

  // Первичный запрос профиля
  fetchProfile();
});
