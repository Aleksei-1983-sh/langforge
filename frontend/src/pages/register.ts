//./src/pages/regidter.ts
import { register } from '@/api/client';
import { mustFind } from '@/lib/utils';

const form = mustFind<HTMLFormElement>('#register-form');
const username = mustFind<HTMLInputElement>('#username');
const email = mustFind<HTMLInputElement>('#email');
const pass = mustFind<HTMLInputElement>('#password');
const confirm = mustFind<HTMLInputElement>('#confirm');
const btn = mustFind<HTMLButtonElement>('#register-btn');

form.addEventListener('submit', async (ev) => {
  ev.preventDefault();

  //Проверка совпадения паролей ДО запроса к серверу
  if (pass.value !== confirm.value) {
    alert('Passwords do not match');
    return;
  }

  btn.disabled = true;

  try {
    await register(
      username.value.trim(),
      email.value.trim(),
      pass.value
    );

    alert('Registered — please log in');
    window.location.href = '/pages/login/index.html';

  } catch (e) {
    console.error(e);
    alert(e instanceof Error ? e.message : 'Register error');
  } finally {
    btn.disabled = false;
  }
});
