# docker/ollama/mock_server.py
from flask import Flask, jsonify, request
import re

app = Flask(__name__)

MODEL_INFO = {
    "name": "llama3:8b",
    "id": "365c0bd3c000",
    "size": "4.7 GB",
    "modified": "2 months ago"
}

@app.route("/v1/list", methods=["GET"])
@app.route("/api/tags", methods=["GET"])
def list_models():
    return jsonify([MODEL_INFO])


def extract_word_from_prompt(prompt: str) -> str:
    """
    Извлекаем слово из prompt.
    Ожидаем формат:
    English word: "run"
    """
    if not prompt:
        return "unknown"

    match = re.search(r'English word:\s*"([^"]+)"', prompt)
    if match:
        return match.group(1)

    # fallback — если формат вдруг изменится
    match = re.search(r'"([^"]+)"', prompt)
    if match:
        return match.group(1)

    return "unknown"


@app.route("/v1/generate", methods=["POST"])
@app.route("/api/generate", methods=["POST"])
def generate():
    data = request.json or {}
    prompt = data.get("prompt", "")

    word = extract_word_from_prompt(prompt)

    # Возвращаем структуру, которую ожидает твой C-парсер
    return jsonify({
        "word": word,
        "translation": "_______________",
        "transcription": "ˈtest",
        "example": [
            {"text": f"This is a test sentence with the word '{word}'."},
            {"text": f"Another example using '{word}' in context."}
        ]
    })


@app.route("/ping")
def ping():
    return "pong"


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=11434)
