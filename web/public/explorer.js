(function () {
  const S = window.AdditionSite;
  if (!S) {
    return;
  }

  function el(id) {
    return document.getElementById(id);
  }

  function setText(node, text, className) {
    if (!node) {
      return;
    }
    node.hidden = !text;
    node.className = className || "";
    node.textContent = text || "";
  }

  function clearRows(tbody) {
    while (tbody.firstChild) {
      tbody.removeChild(tbody.firstChild);
    }
  }

  function cell(text, className) {
    const td = document.createElement("td");
    if (className) {
      td.className = className;
    }
    td.textContent = text;
    return td;
  }

  function linkCell(href, text, className) {
    const td = document.createElement("td");
    if (className) {
      td.className = className;
    }
    const a = document.createElement("a");
    a.href = href;
    a.textContent = text;
    td.appendChild(a);
    return td;
  }

  function queryHref(value) {
    const params = new URLSearchParams();
    const rpc = new URLSearchParams(window.location.search).get("rpc");
    if (rpc) {
      params.set("rpc", rpc);
    }
    params.set("q", value);
    return "/?" + params.toString();
  }

  function renderRows(tbody, rows) {
    clearRows(tbody);
    for (let i = 0; i < rows.length; i += 1) {
      const row = rows[i];
      const tr = document.createElement("tr");
      if (row.height) {
        tr.appendChild(linkCell(queryHref(row.height), row.height));
      } else {
        tr.appendChild(cell(""));
      }
      if (row.hash) {
        tr.appendChild(linkCell(queryHref(row.hash), row.hash, "hash-cell"));
      } else {
        tr.appendChild(cell("", "hash-cell"));
      }
      tr.appendChild(cell(row.tx_count || ""));
      tr.appendChild(cell(row.time || ""));
      tbody.appendChild(tr);
    }
  }

  function showSearch(result) {
    const status = el("search-status");
    const box = el("block-result");
    const fields = el("block-fields");
    if (result.kind === "empty") {
      setText(status, "", "");
      box.hidden = true;
      fields.innerHTML = "";
      return;
    }
    if (result.kind === "offline") {
      setText(status, "RPC offline", "offline");
      box.hidden = true;
      fields.innerHTML = "";
      return;
    }
    if (result.kind === "notfound") {
      setText(status, "Not found", "empty");
      box.hidden = true;
      fields.innerHTML = "";
      return;
    }
    setText(status, "", "");
    box.hidden = false;
    S.renderFields(fields, result.fields, "Not found");
  }

  async function searchQuery(q) {
    const cmd = S.blockSearchCommand(q);
    if (!cmd) {
      return { kind: "empty" };
    }
    const result = await S.explorerCommand(cmd);
    if (result.offline) {
      return { kind: "offline" };
    }
    if (!result.ok || !result.raw || result.raw.indexOf("error:") === 0) {
      return { kind: "notfound" };
    }
    if (!result.fields || Object.keys(result.fields).length === 0) {
      return { kind: "notfound" };
    }
    return { kind: "block", fields: result.fields };
  }

  async function loadLatest() {
    const state = el("rpc-state");
    const tbody = el("latest-blocks");
    const recent = await S.loadLatestBlockRows(10);
    if (recent.offline) {
      setText(state, "RPC offline", "offline");
      clearRows(tbody);
      return;
    }
    setText(state, "", "");
    renderRows(tbody, recent.blocks || []);
  }

  async function boot() {
    const form = el("search-form");
    const input = el("q");
    const params = new URLSearchParams(window.location.search);
    const q = (params.get("q") || "").trim();
    if (input && q) {
      input.value = q;
      showSearch(await searchQuery(q));
    }
    if (form) {
      form.addEventListener("submit", async function (event) {
        event.preventDefault();
        const value = input ? input.value.trim() : "";
        const next = new URL(window.location.href);
        if (value) {
          next.searchParams.set("q", value);
        } else {
          next.searchParams.delete("q");
        }
        window.history.replaceState({}, "", next.pathname + next.search);
        showSearch(await searchQuery(value));
      });
    }
    await loadLatest();
  }

  boot();
}());
