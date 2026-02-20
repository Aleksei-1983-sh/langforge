// register.js - логика регистрации
document.addEventListener('DOMContentLoaded', () => {
  const form = document.getElementById('register-form');
  const btn = document.getElementById('register-btn');

  form.addEventListener('submit', async (e) => {
	e.preventDefault();
	showMessage('', '');
	setBusy(btn, true);
//получение из элементов формы значений
	const username = form.username.value.trim();
	const email = form.email.value.trim();
	const password = form.password.value;
	const confirm = form.confirm.value;
//проверка наличие этих значений
	if (!username || !email || !password) {
	  showMessage('Заполните все поля.');
	  setBusy(btn, false);
	  return;
	}
//сравнение введенного пороля с повторно введеным поролям 
	if (password !== confirm) {
	  showMessage('Пароли не совпадают.');
	  setBusy(btn, false);
	  return;
	}
//попытка сформировать пост запрос с данными регестрации пользователя 
	try {
	//это функция взята из api.js
	  const data = await api.postJson('/api/register', { username, email, password });
	  if (data.success) {
		// редирект на страницу входа с флагом
		window.location.href = '/login/?registered=1';
	  } else {
		showMessage(data.message || 'Ошибка при регистрации.');
	  }
	} catch (err) {
	  console.error('Register error:', err);
	  showMessage('Ошибка сервера. Попробуйте позже.');
	} finally {
	  setBusy(btn, false);
	}
  });
});
