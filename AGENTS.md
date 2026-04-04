# AGENTS.md — langforge

## Что это за проект

`langforge` — веб-приложение для изучения английского языка. Текущая архитектура проекта состоит из C-backend'а, который работает как HTTP-сервер, PostgreSQL для хранения данных и frontend'а со статическими страницами и TypeScript-логикой. В README проект описан как web-based application for learning English с backend на C и PostgreSQL. fileciteturn5file0

## Текущее устройство репозитория

### Backend
Основная точка входа — `backend/src/main.c`: сервер стартует на порту `1234`, инициализирует Ollama, подключение к БД, вычисляет каталог `www`, регистрирует роуты и затем крутится в polling-цикле. fileciteturn6file0

Сборка backend'а описана в `backend/Makefile`: используется `gcc`, флаги `-Wall -Wextra`, include-пути на `src`, `src/libs`, `src/db`, `src/handlers`, `src/models`, `src/ollama`, `src/utils`, линковка с `libpq` и `libcrypto`, а целевой бинарник — `backend/bin/englearn`. fileciteturn9file0

### Router и handlers
`backend/src/router.c` сейчас регистрирует следующие маршруты:
- `POST /api/v1/login`
- `POST /api/v1/register`
- `GET /api/v1/me`
- `POST /api/v1/me`
- `POST /api/v1/generate_card` fileciteturn14file0

В `backend/src/handlers/card_handler.h` объявлены обработчики `handle_index`, `handle_cards`, `handle_me`, `handle_static`, `handle_login`, `handle_register`, а также `resolve_www_dir()`. fileciteturn16file0

`backend/src/handlers/card_handler.c` сейчас совмещает сразу несколько обязанностей:
- определение и поиск каталога `www`;
- отдачу статических файлов;
- обработку авторизации;
- выдачу профиля пользователя;
- работу со словарными карточками;
- cookie/session-логику. fileciteturn17file0

Отдельный `backend/src/handlers/generate_handler.c` принимает `POST /api/v1/generate_card` с JSON вида `{"word":"..."}`, вызывает локальную LLM через Ollama и возвращает JSON-карточку. Сохранение в БД там пока отключено через `#if 0`. fileciteturn18file0

### DB слой
`backend/src/db/db.h` показывает текущий контракт БД:
- подключение/отключение PostgreSQL,
- регистрация/логин,
- создание и проверка сессий,
- профиль пользователя,
- чтение/добавление/проверка слов. fileciteturn11file0

### Ollama
`backend/src/ollama/ollama.h` задаёт структуру `word_card_t` с полями `word`, `translation`, `transcription` и массивом `examples[NUMBER_OF_EXAMPLES]`, где сейчас `NUMBER_OF_EXAMPLES = 2`. fileciteturn21file0

`backend/src/ollama/ollama.c` реализует:
- автостарт `ollama serve`, если сервер не отвечает;
- сборку prompt'а для слова;
- POST-запрос к Ollama;
- разбор ответа модели;
- очистку памяти для карточки. fileciteturn22file0

### HTTP слой
`backend/src/libs/http.h` содержит собственный HTTP client/server API, структуру `http_request_t`, регистрацию handler'ов и функции отправки ответов. Это не внешний фреймворк, а локальная реализация. fileciteturn12file0

### Frontend
`frontend/vite.config.ts` показывает, что frontend собирается Vite'ом, root — `.` , dev server на порту `3000`, а build'а хватает на три entry-point'а: `pages/login/index.html`, `pages/register/index.html`, `pages/dashboard/index.html`. fileciteturn30file0

`frontend/README.txt` прямо описывает структуру как login/register/dashboard + assets, и говорит, что страницы можно открывать локально через `pages/login/index.html` и `pages/register/index.html`. fileciteturn29file0

TypeScript-клиент находится в `frontend/src/api/client.ts`: там есть `login()`, `register()` и `generateCard()`, а запросы идут на `/api/v1/login`, `/api/v1/register`, `/api/v1/generate_card` с `credentials: 'include'`. fileciteturn25file0

`frontend/src/pages/login.ts`, `frontend/src/pages/register.ts` и `frontend/src/pages/dashboard.ts` — это тонкие page-controller'ы, которые связывают DOM с API. fileciteturn23file0turn27file0turn33file0

`frontend/src/lib/utils.ts` содержит `mustFind()` и `escapeHtml()`. fileciteturn32file0

## Как работает приложение сейчас

1. `main.c` поднимает backend на `1234`, инициализирует Ollama и PostgreSQL, вычисляет путь к `www`, затем регистрирует роуты. fileciteturn6file0turn14file0
2. Статика обслуживается из `www`, с fallback'ами на `pages/...` и `assets/...`; для `login`, `register` и `dashboard` есть редиректы на `/login/`, `/register/`, `/dashboard/` соответственно. Это важно не ломать без необходимости. fileciteturn17file0
3. Авторизация завязана на cookie `session`; TTL берётся из `SESSION_MAX_AGE`, если переменная не задана — по умолчанию 30 дней. fileciteturn17file0
4. `/api/v1/me` возвращает профиль и использует sliding expiration через проверку сессии в БД. fileciteturn17file0turn11file0
5. `/api/v1/generate_card` генерирует карточку через локальный Ollama и возвращает JSON с `word`, `transcription`, `translation`, `examples`. fileciteturn18file0turn21file0turn22file0

## Практические правила для изменений

### Backend
- Не менять сигнатуры и смысл функций из `db.h`, если нет явной миграции всей цепочки вызовов. Эти функции — контракт между HTTP-слоем и PostgreSQL. fileciteturn11file0
- Любой новый endpoint сначала добавлять в router, потом реализовывать handler, потом только подключать фронт.
- При работе с памятью помнить, что `db_*` и Ollama-слой часто возвращают heap-объекты; освобождение должно быть явным.
- Не логировать пароли, cookie и сырые session token'ы. В `card_handler.c` уже есть редактирование чувствительных заголовков при логировании. fileciteturn17file0
- Для ответов использовать текущий JSON shape, чтобы не сломать frontend.

### Frontend
- Не ломать пути `/pages/login/index.html`, `/pages/register/index.html`, `/pages/dashboard/index.html` и относительные ассеты: они уже заложены в Vite build и в серверную статику. fileciteturn30file0turn29file0turn17file0
- Для API-запросов сохранять `credentials: 'include'`, иначе cookie-сессия перестанет работать. fileciteturn25file0
- Ожидаемый ответ `/api/v1/generate_card` уже используется в dashboard-рендере, поэтому менять его нужно синхронно с frontend. fileciteturn33file0turn18file0

### Сборка и запуск
- Backend собирается через `backend/Makefile`.
- Frontend собирается через Vite.
- Тестовый таргет в Makefile есть, но он завязан на `./tests/run_tests.sh`; по текущему снимку репозитория это скорее заготовка, чем полноценная инфраструктура. fileciteturn9file0

## Текущее состояние проекта

Проект уже не "пустой каркас": есть рабочие модули авторизации, профиля, генерации карточек и статики. При этом архитектура всё ещё монолитная и смешивает в одном backend'е HTTP, auth, static serving, session management и бизнес-логику карточек. Это нормально для текущей стадии, но при дальнейшем росте лучше разделять:
- transport layer,
- auth/session service,
- word/card service,
- storage layer,
- external LLM adapter.  

## Что важно помнить при доработках

- Сохранять совместимость с текущими URL и JSON-полями.
- Не делать скрытых переименований файлов и маршрутов без одновременного обновления frontend'а.
- После изменения логики сессий проверять `handle_me`, `login` и `db_userid_by_session()` как единую цепочку.
- После изменения карточек проверять связку `generate_handler -> Ollama -> dashboard renderer`.
- Если добавляется новый статический путь, нужно смотреть и `handle_static`, и Vite build inputs, и фактическую файловую структуру `frontend/pages` / `frontend/assets`.

## Короткая карта ключевых файлов

- `backend/src/main.c`
- `backend/src/router.c`
- `backend/src/handlers/card_handler.c`
- `backend/src/handlers/generate_handler.c`
- `backend/src/db/db.h`
- `backend/src/libs/http.h`
- `backend/src/ollama/ollama.c`
- `frontend/src/api/client.ts`
- `frontend/src/pages/login.ts`
- `frontend/src/pages/register.ts`
- `frontend/src/pages/dashboard.ts`
- `frontend/src/lib/utils.ts`
- `frontend/vite.config.ts`
