# Web Architecture for the Query Engine

This repository is a C++ query processor. If you want a web terminal on top of it, keep the engine as the execution core and add a separate web layer around it.

## Goal

Let a user type SQL in a browser, send it to the backend, execute it with the existing engine, and show the result in a table view.

## Set Up a Virtual Environment 

Before running the web server, it is best practice to create a Python virtual environment to manage dependencies like Flask.

**On macOS and Linux:**
```bash
cd web
python3 -m venv venv
source venv/bin/activate
pip install Flask
```

**On Windows:**
```bash
cd web
python -m venv venv
venv\Scripts\activate
pip install Flask
```

## Run the Web Server

To start the local web interface, ensure you have Python and Flask installed, then run the application script:

```bash
python app.py
```

Once the server is running, you can access the web terminal by opening `http://127.0.0.1:5000` in your browser.

## Recommended Structure

### 1. Frontend
A small web UI with:
- SQL editor or textarea
- Run button
- Result table
- Error panel
- Query history

Recommended responsibilities:
- collect SQL input
- call the backend API
- render result rows and column headers
- display errors cleanly

### 2. Backend API
A thin HTTP service that wraps the existing C++ engine.

Recommended endpoints:
- `POST /api/query`
- `GET /api/health`
- `GET /api/schema` if you want to expose table metadata later

Example request:
```json
{
  "query": "SELECT name FROM users WHERE age > 30;"
}
```

Example response:
```json
{
  "columns": ["name"],
  "rows": [["Alice"], ["Bob"]],
  "tupleCount": 2
}
```

### 3. C++ Engine Layer
Keep the current repository as the query execution engine.

Responsibilities:
- tokenize SQL
- parse SQL
- plan the query
- execute the plan
- return structured results instead of only printing when called by the web backend

## Integration Options

### Option A: Add a separate HTTP wrapper
Create a small server that calls the engine library.

Best if:
- you want minimal changes to the existing code
- you want a clean separation between web and database logic

### Option B: Add an API mode inside the same binary
Make the current executable serve HTTP in a special mode.

Best if:
- you want one binary for demos
- you are fine mixing CLI and server startup logic

### Option C: Build a frontend-only demo with mocked responses first
This is useful if the backend API is not ready yet.

Best if:
- you want to show the UX early
- the backend work is split across teammates

## Suggested Stack

### Backend
- C++ HTTP library such as `Crow`, `Drogon`, or `httplib`
- JSON library such as `nlohmann/json`

### Frontend
- React or plain HTML/CSS/JS for a minimal demo
- Monaco Editor if you want a code-editor feel

## Important Design Rule

Do not couple the browser UI directly to parser internals.

Instead:
- frontend sends raw SQL
- backend returns structured JSON
- UI renders only the response

That keeps the web layer independent from query engine internals and avoids conflict with the current CLI workflow.

## Minimal Demo Flow

1. User opens the webpage.
2. User types a SQL query.
3. Frontend sends the query to `POST /api/query`.
4. Backend runs the query through the engine.
5. Backend returns columns, rows, and errors.
6. Frontend displays the result.

## What To Implement First

If you want the smallest useful web version, do this in order:
1. `POST /api/query`
2. JSON result format
3. simple browser UI
4. query history
5. schema browser later
