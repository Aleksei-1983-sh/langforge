
document.getElementById("login-form").addEventListener("submit", function (e) {
  e.preventDefault();

  const username = document.getElementById("username").value.trim();
  const password = document.getElementById("password").value.trim();
  const errorMsg = document.getElementById("error-msg");
  errorMsg.textContent = "";

  fetch("/api/login", {
    method: "POST",
    headers: {
      "Content-Type": "application/json"
    },
    body: JSON.stringify({ username, password })
  })
  .then((res) => res.json())
  .then((data) => {
    if (data.success) {
      window.location.href = "/dashboard";
    } else {
      errorMsg.textContent = data.message || "Invalid credentials.";
    }
  })
  .catch((err) => {
    console.error("Login error:", err);
    errorMsg.textContent = "Server error. Try again later.";
  });
});

// показать сообщение если пришли с регистрации
if (new URLSearchParams(window.location.search).get('registered') === '1') {
  const e = document.getElementById("error-msg");
  if (e) e.style.color = "green", e.textContent = "Регистрация прошла успешно. Войдите, пожалуйста.";
}

