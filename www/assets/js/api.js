
// api.js
const api = {
  postJson: async (url, data, fetchOptions = {}) => {
    const init = Object.assign({
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data),
      credentials: 'include'   // <--- важно: отправлять cookie и принимать Set-Cookie
    }, fetchOptions);

    const res = await fetch(url, init);

    if (!res.ok) {
      let errText = `HTTP ${res.status}`;
      try { const j = await res.clone().json(); errText = j.message || JSON.stringify(j); } catch(e){}
      throw new Error(errText);
    }
    return res.json();
  }
};

