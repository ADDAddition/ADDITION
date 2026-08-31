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

  function renderBlockRows(tbody, rows) {
    clearRows(tbody);
    for (let i = 0; i < rows.length; i += 1) {
      const row = rows[i];
      const tr = document.createElement("tr");
      if (row.height) {
        tr.appendChild(linkCell(S.blockHref(row.height), row.height));
      } else {
        tr.appendChild(cell(""));
      }
      if (row.hash) {
        tr.appendChild(linkCell(S.blockHref(row.hash), row.hash, "hash-cell"));
      } else {
        tr.appendChild(cell("", "hash-cell"));
      }
      tr.appendChild(cell(row.tx_count || ""));
      tr.appendChild(cell(row.time || ""));
      tbody.appendChild(tr);
    }
  }

  function renderTxRows(tbody, rows) {
    clearRows(tbody);
    for (let i = 0; i < rows.length; i += 1) {
      const row = rows[i];
      const tr = document.createElement("tr");
      if (row.tx_hash) {
        tr.appendChild(linkCell(S.txHref(row.tx_hash), row.tx_hash, "hash-cell"));
      } else {
        tr.appendChild(cell("", "hash-cell"));
      }
      if (row.height) {
        tr.appendChild(linkCell(S.blockHref(row.height), row.height));
      } else {
        tr.appendChild(cell(""));
      }
      tr.appendChild(cell(row.index || ""));
      tbody.appendChild(tr);
    }
  }

  function renderStrip(status) {
    const strip = el("live-strip");
    const flag = el("strip-flag");
    const cells = el("strip-cells");
    if (!strip || !flag || !cells) {
      return;
    }
    clearRows(cells);
    if (!status || status.offline || !status.ok) {
      strip.className = "status-strip offline";
      flag.textContent = "RPC offline";
      return;
    }
    const fields = status.strip || S.stripFields(status.fields || {});
    const keys = Object.keys(fields);
    if (keys.length === 0) {
      strip.className = "status-strip offline";
      flag.textContent = "RPC offline";
      return;
    }
    strip.className = "status-strip ok";
    flag.textContent = "live";
    for (let i = 0; i < keys.length; i += 1) {
      const wrap = document.createElement("div");
      const dt = document.createElement("dt");
      dt.textContent = keys[i];
      const dd = document.createElement("dd");
      dd.textContent = fields[keys[i]];
      wrap.appendChild(dt);
      wrap.appendChild(dd);
      cells.appendChild(wrap);
    }
  }

  async function routeSearch(value) {
    const status = el("search-status");
    if (!value) {
      setText(status, "", "");
      return;
    }
    setText(status, "Searching…", "empty");
    const result = await S.resolveSearch(value);
    if (result.kind === "offline") {
      setText(status, "RPC offline", "offline");
      return;
    }
    if (result.kind === "notfound" || result.kind === "empty") {
      setText(status, "Not found", "empty");
      return;
    }
    const dest = S.routeForSearch(result);
    if (dest) {
      window.location.assign(dest);
      return;
    }
    setText(status, "Not found", "empty");
  }

  async function loadLatest() {
    const blockState = el("rpc-state");
    const txState = el("tx-state");
    const blocksBody = el("latest-blocks");
    const txsBody = el("latest-txs");
    const status = await S.loadChainStatus();
    renderStrip(status);
    if (status.offline) {
      setText(blockState, "RPC offline", "offline");
      setText(txState, "RPC offline", "offline");
      clearRows(blocksBody);
      clearRows(txsBody);
      return;
    }
    const recent = await S.loadLatestBlockRows(10);
    if (recent.offline) {
      setText(blockState, "RPC offline", "offline");
      setText(txState, "RPC offline", "offline");
      clearRows(blocksBody);
      clearRows(txsBody);
      return;
    }
    setText(blockState, "", "");
    renderBlockRows(blocksBody, recent.blocks || []);
    const txs = [];
    for (let i = 0; i < (recent.blocks || []).length; i += 1) {
      const fromBlock = S.txRowsFromBlock(recent.blocks[i]);
      for (let j = 0; j < fromBlock.length; j += 1) {
        txs.push(fromBlock[j]);
        if (txs.length >= 20) {
          break;
        }
      }
      if (txs.length >= 20) {
        break;
      }
    }
    setText(txState, txs.length ? "" : "No tx_hashes in recent getblock rows.", txs.length ? "" : "empty");
    renderTxRows(txsBody, txs);
  }

  async function boot() {
    const form = el("search-form");
    const input = el("q");
    const params = new URLSearchParams(window.location.search);
    const q = (params.get("q") || "").trim();
    if (input && q) {
      input.value = q;
      await routeSearch(q);
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
        await routeSearch(value);
      });
    }
    await loadLatest();
  }

  boot();
}());
