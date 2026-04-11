from __future__ import annotations

import csv
import re
import secrets
import shlex
import sqlite3
import subprocess
from pathlib import Path

from flask import Flask, jsonify, render_template, request, session

ROOT_DIR = Path(__file__).resolve().parent.parent
DATA_DIR = ROOT_DIR / "data"
ENGINE_BIN = ROOT_DIR / "build" / "query_engine"
SANDBOX_CONNECTIONS: dict[str, sqlite3.Connection] = {}
PREVIEW_LIMIT = 8

app = Flask(__name__, template_folder="templates", static_folder="static")
app.secret_key = "simple-query-processor-web-secret"


def _is_inside_root(path: Path) -> bool:
    try:
        path.resolve().relative_to(ROOT_DIR.resolve())
        return True
    except ValueError:
        return False


def _safe_path(raw_path: str) -> Path:
    candidate = (ROOT_DIR / raw_path).resolve()
    if not _is_inside_root(candidate):
        raise ValueError("Access denied: path escapes project root")
    return candidate


def _read_main_tables(include_preview: bool = False) -> list[dict[str, object]]:
    tables: list[dict[str, object]] = []
    if not DATA_DIR.exists():
        return tables

    for table_file in sorted(DATA_DIR.glob("*.db")):
        columns: list[str] = []
        preview_rows: list[list[str]] = []
        row_count = 0

        with table_file.open("r", encoding="utf-8", newline="") as f:
            reader = csv.reader(f)
            first_row = next(reader, None)
            if first_row is not None:
                columns = first_row

            for row in reader:
                row_count += 1
                if include_preview and len(preview_rows) < PREVIEW_LIMIT:
                    preview_rows.append(row)

        table_meta: dict[str, object] = {
            "name": table_file.stem,
            "file": str(table_file.relative_to(ROOT_DIR)),
            "columns": columns,
            "rows": row_count,
        }
        if include_preview:
            table_meta["previewRows"] = preview_rows

        tables.append(table_meta)

    return tables


def _load_main_table(table_name: str) -> dict[str, object] | None:
    table_path = DATA_DIR / f"{table_name}.db"
    if not table_path.exists():
        return None

    with table_path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.reader(f)
        columns = next(reader, None)
        if columns is None:
            return None
        rows = [row for row in reader]

    return {"columns": columns, "rows": rows}


def _get_session_id() -> str:
    sid = session.get("sandbox_id")
    if isinstance(sid, str) and sid:
        return sid

    sid = secrets.token_hex(16)
    session["sandbox_id"] = sid
    return sid


def _quote_identifier(identifier: str) -> str:
    return '"' + identifier.replace('"', '""') + '"'


def _bootstrap_sandbox(conn: sqlite3.Connection) -> None:
    source = _load_main_table("users")
    if source is None:
        return

    columns = source["columns"]
    rows = source["rows"]

    if not isinstance(columns, list) or not isinstance(rows, list):
        return

    column_sql = ", ".join(f"{_quote_identifier(col)} TEXT" for col in columns)
    conn.execute(f"CREATE TABLE IF NOT EXISTS users ({column_sql});")

    existing_count = conn.execute("SELECT COUNT(*) FROM users;").fetchone()[0]
    if existing_count > 0:
        return

    placeholders = ", ".join("?" for _ in columns)
    conn.executemany(
        f"INSERT INTO users VALUES ({placeholders});",
        rows,
    )
    conn.commit()


def _get_sandbox_connection() -> sqlite3.Connection:
    sid = _get_session_id()
    conn = SANDBOX_CONNECTIONS.get(sid)
    if conn is not None:
        return conn

    conn = sqlite3.connect(":memory:", check_same_thread=False)
    conn.row_factory = sqlite3.Row
    _bootstrap_sandbox(conn)
    SANDBOX_CONNECTIONS[sid] = conn
    return conn


def _reset_sandbox() -> None:
    sid = _get_session_id()
    conn = SANDBOX_CONNECTIONS.pop(sid, None)
    if conn is not None:
        conn.close()

    _get_sandbox_connection()


def _get_sandbox_tables() -> list[dict[str, object]]:
    conn = _get_sandbox_connection()
    tables: list[dict[str, object]] = []

    rows = conn.execute(
        "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name;"
    ).fetchall()

    for row in rows:
        table_name = str(row["name"])
        quoted_name = _quote_identifier(table_name)

        col_rows = conn.execute(f"PRAGMA table_info({quoted_name});").fetchall()
        columns = [str(col_row["name"]) for col_row in col_rows]

        count_row = conn.execute(f"SELECT COUNT(*) AS c FROM {quoted_name};").fetchone()
        row_count = int(count_row["c"]) if count_row is not None else 0

        preview = conn.execute(f"SELECT * FROM {quoted_name} LIMIT ?;", (PREVIEW_LIMIT,)).fetchall()
        preview_rows: list[list[str]] = []
        for preview_row in preview:
            preview_rows.append(["" if value is None else str(value) for value in preview_row])

        tables.append(
            {
                "name": table_name,
                "file": "sandbox",
                "columns": columns,
                "rows": row_count,
                "previewRows": preview_rows,
            }
        )

    return tables


def _parse_sql_table_output(output: str) -> dict[str, object] | None:
    lines = [line.rstrip() for line in output.splitlines()]
    lines = [line for line in lines if line.strip()]
    if len(lines) < 3:
        return None

    tuple_line = lines[-1]
    match = re.fullmatch(r"\((\d+) tuple(?:s)?\)", tuple_line.strip())
    if not match:
        return None

    header_line = lines[0]
    separator_line = lines[1]
    data_lines = lines[2:-1]

    if "-" not in separator_line:
        return None

    if "|" in header_line:
        columns = [part.strip() for part in header_line.split("|")]
    else:
        columns = [header_line.strip()]

    rows: list[list[str]] = []

    for line in data_lines:
        if "|" in line:
            row = [part.strip() for part in line.split("|")]
        else:
            row = [line.strip()]
        rows.append(row)

    return {
        "columns": columns,
        "rows": rows,
        "tupleCount": int(match.group(1)),
        "raw": output,
    }


def _run_sql(sql: str) -> dict[str, object]:
    if not ENGINE_BIN.exists():
        return {
            "ok": False,
            "output": "Engine binary not found. Run: make build",
            "kind": "sql",
        }

    command = [str(ENGINE_BIN), "--query", sql]
    result = subprocess.run(
        command,
        cwd=ROOT_DIR,
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )

    output = result.stdout.strip()
    error = result.stderr.strip()

    if result.returncode != 0:
        message = error if error else output if output else "Query failed"
        return {"ok": False, "output": message, "kind": "sql"}

    parsed = _parse_sql_table_output(output) if output else None
    response: dict[str, object] = {
        "ok": True,
        "kind": "sql",
        "output": output if output else "(no output)",
    }
    if parsed is not None:
        response["table"] = parsed
    return response


def _run_sandbox_sql(sql: str) -> dict[str, object]:
    text = sql.strip()
    if not text:
        return {"ok": False, "kind": "sql", "output": "Query is empty"}

    # SQLite does not support SHOW TABLES syntax, map it to sqlite_master lookup.
    if re.fullmatch(r"SHOW\s+TABLES\s*;?", text, flags=re.IGNORECASE):
        text = "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name;"

    conn = _get_sandbox_connection()
    head = text.split(maxsplit=1)[0].upper()
    query_like = head in {"SELECT", "WITH", "PRAGMA"}

    try:
        if query_like:
            cursor = conn.execute(text)
            columns = [str(col[0]) for col in (cursor.description or [])]
            rows = cursor.fetchall()
            serialized_rows: list[list[str]] = []
            for row in rows:
                serialized_rows.append(["" if value is None else str(value) for value in row])

            return {
                "ok": True,
                "kind": "sql",
                "table": {
                    "columns": columns,
                    "rows": serialized_rows,
                    "tupleCount": len(serialized_rows),
                },
                "output": f"{len(serialized_rows)} row(s)",
            }

        conn.executescript(text)
        conn.commit()
        return {"ok": True, "kind": "sql", "output": "Statement executed successfully"}
    except sqlite3.Error as exc:
        return {"ok": False, "kind": "sql", "output": f"SQLite error: {exc}"}


def _cmd_pwd() -> dict[str, object]:
    return {"ok": True, "output": str(ROOT_DIR), "kind": "shell"}


def _cmd_ls(args: list[str]) -> dict[str, object]:
    target = args[0] if args else "."
    try:
        path = _safe_path(target)
    except ValueError as exc:
        return {"ok": False, "output": str(exc), "kind": "shell"}

    if not path.exists():
        return {"ok": False, "output": f"ls: cannot access '{target}': No such file or directory", "kind": "shell"}
    if not path.is_dir():
        return {"ok": True, "output": path.name, "kind": "shell"}

    entries = sorted(path.iterdir(), key=lambda p: p.name.lower())
    lines = [f"{entry.name}/" if entry.is_dir() else entry.name for entry in entries]
    return {"ok": True, "output": "\n".join(lines) if lines else "(empty)", "kind": "shell"}


def _cmd_cat(args: list[str]) -> dict[str, object]:
    if not args:
        return {"ok": False, "output": "cat: missing file operand", "kind": "shell"}

    try:
        path = _safe_path(args[0])
    except ValueError as exc:
        return {"ok": False, "output": str(exc), "kind": "shell"}

    if not path.exists() or not path.is_file():
        return {"ok": False, "output": f"cat: {args[0]}: No such file", "kind": "shell"}

    content = path.read_text(encoding="utf-8", errors="replace")
    return {"ok": True, "output": content if content else "(empty file)", "kind": "shell"}


def _cmd_head_or_tail(cmd: str, args: list[str]) -> dict[str, object]:
    if not args:
        return {"ok": False, "output": f"{cmd}: missing file operand", "kind": "shell"}

    line_count = 10
    file_arg_idx = 0

    if len(args) >= 2 and args[0] == "-n":
        try:
            line_count = max(1, int(args[1]))
        except ValueError:
            return {"ok": False, "output": f"{cmd}: invalid number: {args[1]}", "kind": "shell"}
        file_arg_idx = 2

    if file_arg_idx >= len(args):
        return {"ok": False, "output": f"{cmd}: missing file operand", "kind": "shell"}

    file_arg = args[file_arg_idx]

    try:
        path = _safe_path(file_arg)
    except ValueError as exc:
        return {"ok": False, "output": str(exc), "kind": "shell"}

    if not path.exists() or not path.is_file():
        return {"ok": False, "output": f"{cmd}: {file_arg}: No such file", "kind": "shell"}

    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    chosen = lines[:line_count] if cmd == "head" else lines[-line_count:]
    return {"ok": True, "output": "\n".join(chosen) if chosen else "(empty file)", "kind": "shell"}


def _run_shell(raw: str) -> dict[str, object]:
    try:
        parts = shlex.split(raw)
    except ValueError as exc:
        return {"ok": False, "output": f"shell parse error: {exc}", "kind": "shell"}

    if not parts:
        return {"ok": True, "output": "", "kind": "shell"}

    cmd = parts[0]
    args = parts[1:]

    if cmd == "help":
        return {
            "ok": True,
            "kind": "shell",
            "output": (
                "Commands:\n"
                "  SQL query ending with ;\n"
                "  pwd\n"
                "  ls [path]\n"
                "  cat <file>\n"
                "  head [-n N] <file>\n"
                "  tail [-n N] <file>\n"
                "  tables\n"
                "  clear\n"
                "  exit\n"
            ),
        }

    if cmd == "pwd":
        return _cmd_pwd()
    if cmd == "ls":
        return _cmd_ls(args)
    if cmd == "cat":
        return _cmd_cat(args)
    if cmd == "head":
        return _cmd_head_or_tail("head", args)
    if cmd == "tail":
        return _cmd_head_or_tail("tail", args)
    if cmd == "tables":
        tables = _read_main_tables(include_preview=False)
        if not tables:
            return {"ok": True, "output": "No tables found in data/", "kind": "shell"}
        lines = [f"{t['name']} ({t['rows']} rows)" for t in tables]
        return {"ok": True, "output": "\n".join(lines), "kind": "shell"}

    return {
        "ok": False,
        "kind": "shell",
        "output": f"Unsupported command: {cmd}. Type 'help' for allowed commands.",
    }


def _is_sql(command: str) -> bool:
    stripped = command.strip()
    if not stripped:
        return False

    first_token = stripped.split(maxsplit=1)[0].upper()
    return first_token == "SELECT"


@app.get("/")
def index() -> str:
    return render_template("index.html")


@app.get("/api/health")
def health() -> tuple[object, int]:
    _get_sandbox_connection()
    return jsonify({"ok": True, "engineBuilt": ENGINE_BIN.exists()}), 200


@app.get("/api/main/tables")
def main_tables() -> tuple[object, int]:
    return jsonify({"ok": True, "tables": _read_main_tables(include_preview=True)}), 200


@app.get("/api/sandbox/tables")
def sandbox_tables() -> tuple[object, int]:
    return jsonify({"ok": True, "tables": _get_sandbox_tables()}), 200


@app.post("/api/main/execute")
def execute_main() -> tuple[object, int]:
    payload = request.get_json(silent=True) or {}
    command = str(payload.get("command", "")).strip()

    if not command:
        return jsonify({"ok": False, "output": "Empty command"}), 400

    if command.split(maxsplit=1)[0].upper() != "SELECT":
        return jsonify({"ok": False, "kind": "sql", "output": "Main database is read-only in web mode. Use SELECT only."}), 400

    result = _run_sql(command)
    status = 200 if result.get("ok") else 400
    return jsonify(result), status


@app.post("/api/sandbox/execute")
def execute_sandbox() -> tuple[object, int]:
    payload = request.get_json(silent=True) or {}
    command = str(payload.get("command", "")).strip()

    if not command:
        return jsonify({"ok": False, "output": "Empty command"}), 400

    lowered = command.lower()
    if lowered == "clear":
        return jsonify({"ok": True, "kind": "control", "action": "clear", "output": ""}), 200

    result = _run_sandbox_sql(command)
    status = 200 if result.get("ok") else 400
    return jsonify(result), status


@app.post("/api/sandbox/reset")
def sandbox_reset() -> tuple[object, int]:
    _reset_sandbox()
    return jsonify({"ok": True, "output": "Sandbox reset completed"}), 200


@app.post("/api/execute")
def execute() -> tuple[object, int]:
    payload = request.get_json(silent=True) or {}
    command = str(payload.get("command", "")).strip()

    if not command:
        return jsonify({"ok": False, "output": "Empty command"}), 400

    lowered = command.lower()
    if lowered == "clear":
        return jsonify({"ok": True, "kind": "control", "action": "clear", "output": ""}), 200
    if lowered in {"exit", "quit"}:
        return jsonify({"ok": True, "kind": "control", "action": "noop", "output": "Web terminal stays active. Close the tab to exit."}), 200

    result = _run_sql(command) if _is_sql(command) else _run_shell(command)
    status = 200 if result.get("ok") else 400
    return jsonify(result), status


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=5000, debug=False)
