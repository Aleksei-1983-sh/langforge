
document.getElementById("register-form").addEventListener("submit", function (e) {
  e.preventDefault();

  const username = document.getElementById("username").value.trim();
  const email = document.getElementById("email").value.trim();
  const password = document.getElementById("password").value.trim();
  const errorMsg = document.getElementById("reg-error-msg");
  errorMsg.textContent = "";

  // простая валидация
  if (username.length < 3) {
    errorMsg.textContent = "Username должен быть минимум 3 символа.";
    return;
  }
  if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) {
    errorMsg.textContent = "Некорректный email.";
    return;
  }
  if (password.length < 6) {
    errorMsg.textContent = "Пароль должен быть минимум 6 символов.";
    return;
  }

  fetch("/api/register", {
    method: "POST",
    headers: {
      "Content-Type": "application/json"
    },
    body: JSON.stringify({ username, email, password })
  })
  .then((res) => res.json())
  .then((data) => {
    if (data.success) {
      // после успешной регистрации — переадресуем на страницу входа
      window.location.href = "/index.html?registered=1";
    } else {
      errorMsg.textContent = data.message || "Registration error.";
    }
  })
  .catch((err) => {
    console.error("Register error:", err);
    errorMsg.textContent = "Server error. Try again later.";
  });
});

