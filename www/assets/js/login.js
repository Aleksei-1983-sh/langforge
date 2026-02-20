// login.js - логика страницы логина, использует utils + api
document.addEventListener('DOMContentLoaded', () => {
  const form = document.getElementById('login-form');
  const btn = document.getElementById('login-btn');

  // Показать сообщение об успешной регистрации: ?registered=1
  if (new URLSearchParams(window.location.search).get('registered') === '1') {
    showMessage('Регистрация прошла успешно. Войдите, пожалуйста.', 'success');
  }

  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    showMessage('', ''); // очистить
    setBusy(btn, true);

    const username = form.username.value.trim();
    const password = form.password.value.trim();

    if (!username || !password) {
      showMessage('Введите логин и пароль.');
      setBusy(btn, false);
      return;
    }

    try {
      const data = await api.postJson('/api/login', { username, password });
      if (data.success) {
        // редирект, указанный сервером, или стандартный
        window.location.href = data.redirect || '/dashboard/';
      } else {
        showMessage(data.message || 'Неверные учетные данные.');
      }
    } catch (err) {
      console.error('Login error:', err);
      showMessage('Ошибка сервера. Попробуйте позже.');
    } finally {
      setBusy(btn, false);
    }
  });
});
