const engineStatus = document.getElementById("engineStatus");
const refreshBtn = document.getElementById("refreshBtn");
const targetDb = document.getElementById("targetDb");
const runBtn = document.getElementById("runBtn");
const resetSandboxBtn = document.getElementById("resetSandboxBtn");
const sqlInput = document.getElementById("sqlInput");
const outputArea = document.getElementById("outputArea");

const mainSchema = document.getElementById("mainSchema");
const sandboxSchema = document.getElementById("sandboxSchema");
const mainPreview = document.getElementById("mainPreview");
const sandboxPreview = document.getElementById("sandboxPreview");

const previewTemplate = document.getElementById("tablePreviewTemplate");

async function refreshHealth() {
  try {
    const res = await fetch("/api/health");
    const data = await res.json();
    if (data.engineBuilt) {
      engineStatus.textContent = "engine ready";
      engineStatus.classList.add("ok");
    } else {
      engineStatus.textContent = "engine not built";
      engineStatus.classList.remove("ok");
    }
  } catch {
    engineStatus.textContent = "health check failed";
    engineStatus.classList.remove("ok");
  }
}

function setOutputMessage(message, isError = false) {
  outputArea.innerHTML = "";
  const div = document.createElement("div");
  div.className = isError ? "output-error" : "output-muted";
  div.textContent = message;
  outputArea.appendChild(div);
}

function renderTableInElement(tableEl, columns, rows) {
  const thead = document.createElement("thead");
  const headerRow = document.createElement("tr");
  for (const col of columns) {
    const th = document.createElement("th");
    th.textContent = col;
    headerRow.appendChild(th);
  }
  thead.appendChild(headerRow);

  const tbody = document.createElement("tbody");
  for (const row of rows) {
    const tr = document.createElement("tr");
    for (const cell of row) {
      const td = document.createElement("td");
      td.textContent = cell;
      tr.appendChild(td);
    }
    tbody.appendChild(tr);
  }

  tableEl.innerHTML = "";
  tableEl.append(thead, tbody);
}

function renderOutputTable(tablePayload) {
  outputArea.innerHTML = "";

  const columns = tablePayload.columns || [];
  const rows = tablePayload.rows || [];

  const wrap = document.createElement("div");
  wrap.className = "output-table-wrap scrollbox-inner";

  const table = document.createElement("table");
  table.className = "table-grid";
  renderTableInElement(table, columns, rows);
  wrap.appendChild(table);

  const meta = document.createElement("div");
  meta.className = "output-meta";
  const tupleCount = Number(tablePayload.tupleCount || 0);
  meta.textContent = `${tupleCount} ${tupleCount === 1 ? "row" : "rows"}`;

  outputArea.append(wrap, meta);
}

function renderSchema(target, tables) {
  target.innerHTML = "";
  if (!tables || tables.length === 0) {
    const empty = document.createElement("div");
    empty.className = "output-muted";
    empty.textContent = "No tables";
    target.appendChild(empty);
    return;
  }

  for (const t of tables) {
    const item = document.createElement("div");
    item.className = "schema-item";

    const title = document.createElement("div");
    title.className = "schema-title";
    title.textContent = t.name;

    const cols = document.createElement("div");
    cols.className = "schema-columns";
    cols.textContent = (t.columns || []).join(", ");

    item.append(title, cols);
    target.appendChild(item);
  }
}

function renderPreviews(target, tables) {
  target.innerHTML = "";
  if (!tables || tables.length === 0) {
    const empty = document.createElement("div");
    empty.className = "output-muted";
    empty.textContent = "No tables";
    target.appendChild(empty);
    return;
  }

  for (const t of tables) {
    const fragment = previewTemplate.content.cloneNode(true);
    const card = fragment.querySelector(".table-preview-card");
    const title = fragment.querySelector(".table-preview-title");
    const meta = fragment.querySelector(".table-preview-meta");
    const grid = fragment.querySelector(".table-grid");

    title.textContent = t.name;
    meta.textContent = `${t.rows} rows`;

    const previewRows = t.previewRows || [];
    if (!t.columns || t.columns.length === 0) {
      card.querySelector(".table-preview-grid-wrap").textContent = "No preview";
    } else {
      renderTableInElement(grid, t.columns, previewRows);
    }

    target.appendChild(fragment);
  }
}

async function refreshAllTables() {
  const [mainRes, sandboxRes] = await Promise.all([
    fetch("/api/main/tables"),
    fetch("/api/sandbox/tables"),
  ]);

  const mainData = await mainRes.json();
  const sandboxData = await sandboxRes.json();

  renderSchema(mainSchema, mainData.tables || []);
  renderSchema(sandboxSchema, sandboxData.tables || []);
  renderPreviews(mainPreview, mainData.tables || []);
  renderPreviews(sandboxPreview, sandboxData.tables || []);
}

async function runQuery() {
  const command = sqlInput.value.trim();
  if (!command) {
    setOutputMessage("Query is empty", true);
    return;
  }

  const target = targetDb.value;
  const endpoint = target === "main" ? "/api/main/execute" : "/api/sandbox/execute";

  try {
    const res = await fetch(endpoint, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ command }),
    });

    const data = await res.json();

    if (data.action === "clear") {
      setOutputMessage("Output cleared");
      return;
    }

    if (!data.ok) {
      setOutputMessage(data.output || "Query failed", true);
      return;
    }

    if (data.table) {
      renderOutputTable(data.table);
    } else if (data.output) {
      setOutputMessage(data.output, false);
    }

    if (target === "sandbox") {
      await refreshAllTables();
    }
  } catch (err) {
    setOutputMessage(`Request failed: ${err}`, true);
  }
}

async function resetSandbox() {
  try {
    const res = await fetch("/api/sandbox/reset", { method: "POST" });
    const data = await res.json();
    if (!data.ok) {
      setOutputMessage(data.output || "Sandbox reset failed", true);
      return;
    }

    setOutputMessage(data.output || "Sandbox reset completed", false);
    await refreshAllTables();
  } catch (err) {
    setOutputMessage(`Request failed: ${err}`, true);
  }
}

runBtn.addEventListener("click", runQuery);
refreshBtn.addEventListener("click", refreshAllTables);
resetSandboxBtn.addEventListener("click", resetSandbox);

sqlInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    if (event.shiftKey) {
      return;
    }
    event.preventDefault();
    runQuery();
  }
});

refreshHealth();
refreshAllTables();
sqlInput.focus();
