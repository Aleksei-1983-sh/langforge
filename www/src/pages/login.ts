import { login } from '@/api/client';
import { mustFind } from '@/lib/utils';

const form = mustFind<HTMLFormElement>('#login-form');
const username = mustFind<HTMLInputElement>('#username');
const pass = mustFind<HTMLInputElement>('#password');
const btn = mustFind<HTMLButtonElement>('#login-btn');

form.addEventListener('submit', async (ev) => {
  ev.preventDefault();
  btn.disabled = true;

  try {
    const res = await login(
      username.value.trim(),
      pass.value
    );

    if (res.success) {
      // при желании можно сохранить user_id
      localStorage.setItem('user_id', String(res.user_id));

      window.location.href = '/pages/dashboard/index.html';
    } else {
      alert('Login failed');
    }

  } catch (e) {
    console.error(e);
    alert(e instanceof Error ? e.message : 'Login error');
  } finally {
    btn.disabled = false;
  }
});
