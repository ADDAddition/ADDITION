(function (global) {
  const PUBLIC_READ_COMMANDS = {
    getinfo: { args: "", usage: "getinfo" },
    monetary_info: { args: "", usage: "monetary_info" },
    crypto_selftest: { args: "", usage: "crypto_selftest" },
    tx_status: { args: "<tx_hash>", usage: "tx_status <tx_hash>" },
    peers: { args: "", usage: "peers" },
    getblock: { args: "<height_or_hash>", usage: "getblock <height_or_hash>" },
    getblockhash: { args: "<height>", usage: "getblockhash <height>" },
    getblockraw: { args: "<height>", usage: "getblockraw <height>" }
  };

  const STRIP_KEYS = [
    "network",
    "network_id",
    "height",
    "peers",
    "pq_mode",
    "pow_algorithm",
    "privacy_claim",
    "max_supply",
    "emitted",
    "remaining",
    "next_reward"
  ];

  function fieldPresent(fields, key) {
    return !!(fields && Object.prototype.hasOwnProperty.call(fields, key)
      && fields[key] !== undefined && fields[key] !== null && fields[key] !== "");
  }

  function fieldOrNull(fields, key) {
    if (!fieldPresent(fields, key)) {
      return null;
    }
    return fields[key];
  }

  function rpcUrlFromPage() {
    const params = new URLSearchParams(window.location.search);
    const fromQuery = params.get("rpc");
    if (fromQuery) {
      return fromQuery.replace(/\/$/, "");
    }
    if (window.ADDITION_RPC_HTTP) {
      return String(window.ADDITION_RPC_HTTP).replace(/\/$/, "");
    }
    return "/api/rpc";
  }

  function rpcQuerySuffix() {
    const params = new URLSearchParams(window.location.search);
    const rpc = params.get("rpc");
    if (!rpc) {
      return "";
    }
    return "rpc=" + encodeURIComponent(rpc);
  }

  function withRpc(href) {
    const suffix = rpcQuerySuffix();
    if (!suffix) {
      return href;
    }
    return href.indexOf("?") === -1 ? href + "?" + suffix : href + "&" + suffix;
  }

  function parseFields(line) {
    const fields = {};
    if (!line || typeof line !== "string") {
      return fields;
    }
    const text = line.trim();
    if (!text || text.indexOf("=") === -1) {
      return fields;
    }
    const parts = text.split(/\s+/);
    for (let i = 0; i < parts.length; i += 1) {
      const eq = parts[i].indexOf("=");
      if (eq <= 0) {
        continue;
      }
      const key = parts[i].slice(0, eq);
      const value = parts[i].slice(eq + 1);
      if (key) {
        fields[key] = value;
      }
    }
    return fields;
  }

  async function rpcCommand(cmd) {
    const base = rpcUrlFromPage();
    const url = base.indexOf("?") === -1
      ? base + "?cmd=" + encodeURIComponent(cmd)
      : base + "&cmd=" + encodeURIComponent(cmd);
    try {
      const res = await fetch(url, { method: "GET", cache: "no-store" });
      if (!res.ok) {
        const errText = (await res.text()).trim();
        const looksLikeHtml = !errText
          || errText.charAt(0) === "<"
          || errText.indexOf("<!DOCTYPE") === 0
          || errText.toLowerCase().indexOf("<html") === 0;
        return {
          ok: false,
          offline: true,
          raw: looksLikeHtml ? "RPC offline" : errText,
          fields: {}
        };
      }
      const raw = (await res.text()).trim();
      if (!raw) {
        return { ok: false, offline: true, raw: "RPC offline", fields: {} };
      }
      if (raw.charAt(0) === "<" || raw.indexOf("<!DOCTYPE") === 0) {
        return { ok: false, offline: true, raw: "RPC offline", fields: {} };
      }
      if (raw === "RPC offline" || raw.indexOf("error: public read RPC") === 0) {
        return { ok: false, offline: true, raw: raw, fields: {} };
      }
      return { ok: true, offline: false, raw: raw, fields: parseFields(raw) };
    } catch (e) {
      return { ok: false, offline: true, raw: "RPC offline", fields: {} };
    }
  }

  function renderFields(target, fields, emptyText) {
    target.innerHTML = "";
    const keys = Object.keys(fields);
    if (keys.length === 0) {
      const p = document.createElement("p");
      p.className = "empty";
      p.textContent = emptyText || "No fields returned.";
      target.appendChild(p);
      return;
    }
    const dl = document.createElement("dl");
    for (let i = 0; i < keys.length; i += 1) {
      const dt = document.createElement("dt");
      dt.textContent = keys[i];
      const dd = document.createElement("dd");
      const value = fields[keys[i]];
      if (keys[i] === "tx_hashes" && value) {
        const hashes = String(value).split(",").filter(Boolean);
        for (let j = 0; j < hashes.length; j += 1) {
          if (j > 0) {
            dd.appendChild(document.createTextNode(", "));
          }
          const a = document.createElement("a");
          a.href = txHref(hashes[j]);
          a.textContent = hashes[j];
          dd.appendChild(a);
        }
      } else if ((keys[i] === "hash" || keys[i] === "previous_hash") && value) {
        const a = document.createElement("a");
        a.href = blockHref(value);
        a.textContent = value;
        dd.appendChild(a);
      } else if (keys[i] === "block_height" && value) {
        const a = document.createElement("a");
        a.href = blockHref(value);
        a.textContent = value;
        dd.appendChild(a);
      } else if (keys[i] === "tx_hash" && value) {
        const a = document.createElement("a");
        a.href = txHref(value);
        a.textContent = value;
        dd.appendChild(a);
      } else {
        dd.textContent = value;
      }
      dl.appendChild(dt);
      dl.appendChild(dd);
    }
    target.appendChild(dl);
  }

  function parseHeight(fields) {
    if (!fieldPresent(fields, "height")) {
      return null;
    }
    const n = Number(fields.height);
    if (!Number.isFinite(n) || n < 0) {
      return null;
    }
    return Math.floor(n);
  }

  async function loadRecentBlocks(height, count) {
    const max = typeof count === "number" && count > 0 ? count : 5;
    if (height === null || height === undefined) {
      return { offline: false, blocks: [] };
    }
    const tip = Number(height);
    if (!Number.isFinite(tip) || tip < 0) {
      return { offline: false, blocks: [] };
    }
    const start = Math.max(0, tip - (max - 1));
    const blocks = [];
    for (let h = tip; h >= start; h -= 1) {
      const result = await rpcCommand("getblock " + h);
      if (result.offline) {
        return { offline: true, blocks: [] };
      }
      if (!result.ok || result.raw.indexOf("error:") === 0) {
        blocks.push({ height: h, ok: false, raw: result.raw || "error: block not found", fields: {} });
        continue;
      }
      blocks.push({ height: h, ok: true, raw: result.raw, fields: result.fields });
    }
    return { offline: false, blocks: blocks };
  }

  function renderRecentBlocks(target, recent, emptyText) {
    target.innerHTML = "";
    if (!recent || recent.offline) {
      const p = document.createElement("p");
      p.className = "empty";
      p.textContent = "RPC offline";
      target.appendChild(p);
      return;
    }
    if (!recent.blocks || recent.blocks.length === 0) {
      const p = document.createElement("p");
      p.className = "empty";
      p.textContent = emptyText || "No getblock results.";
      target.appendChild(p);
      return;
    }
    for (let i = 0; i < recent.blocks.length; i += 1) {
      const block = recent.blocks[i];
      const wrap = document.createElement("div");
      wrap.className = "recent-block";
      const title = document.createElement("h3");
      title.textContent = "getblock " + block.height;
      wrap.appendChild(title);
      if (block.ok && block.fields && Object.keys(block.fields).length > 0) {
        const fields = document.createElement("div");
        renderFields(fields, block.fields, "Node returned no key=value fields.");
        wrap.appendChild(fields);
      }
      const pre = document.createElement("pre");
      pre.className = "raw";
      pre.textContent = block.raw || "";
      wrap.appendChild(pre);
      target.appendChild(wrap);
    }
  }

  function setStatus(el, result) {
    if (!el) {
      return;
    }
    if (result.offline) {
      el.className = "offline";
      el.textContent = "RPC offline";
      return;
    }
    el.className = "ok";
    el.textContent = result.ok ? "RPC answered" : (result.raw || "RPC error");
  }

  function stripFields(fields) {
    const out = {};
    if (!fields) {
      return out;
    }
    for (let i = 0; i < STRIP_KEYS.length; i += 1) {
      const key = STRIP_KEYS[i];
      if (Object.prototype.hasOwnProperty.call(fields, key)) {
        out[key] = fields[key];
      }
    }
    return out;
  }

  function mergeFields() {
    const out = {};
    for (let i = 0; i < arguments.length; i += 1) {
      const fields = arguments[i];
      if (!fields) {
        continue;
      }
      const keys = Object.keys(fields);
      for (let j = 0; j < keys.length; j += 1) {
        out[keys[j]] = fields[keys[j]];
      }
    }
    return out;
  }

  function isHeightQuery(q) {
    return /^\d+$/.test(String(q || "").trim());
  }

  function isHexQuery(q) {
    return /^[0-9a-fA-F]+$/.test(String(q || "").trim());
  }

  function isAddressQuery(q) {
    const id = String(q || "").trim();
    return id.length === 128 && isHexQuery(id);
  }

  function blockSearchCommand(q) {
    const id = String(q || "").trim();
    if (!id) {
      return "";
    }
    return "getblock " + id;
  }

  function blockHref(id) {
    return withRpc("/block/?q=" + encodeURIComponent(String(id || "").trim()));
  }

  function txHref(id) {
    return withRpc("/tx/?q=" + encodeURIComponent(String(id || "").trim()));
  }

  function addressHref(id) {
    return withRpc("/address/?q=" + encodeURIComponent(String(id || "").trim()));
  }

  function explorerHref(id) {
    return withRpc("/?q=" + encodeURIComponent(String(id || "").trim()));
  }

  function formatBlockTime(value) {
    if (value === undefined || value === null || value === "") {
      return "";
    }
    const n = Number(value);
    if (!Number.isFinite(n) || n <= 0) {
      return "";
    }
    const d = new Date(n * 1000);
    if (Number.isNaN(d.getTime())) {
      return "";
    }
    return d.toISOString().replace("T", " ").replace(/\.\d+Z$/, " UTC");
  }

  function blockRowFromFields(fields) {
    const row = {};
    if (!fields) {
      return row;
    }
    if (Object.prototype.hasOwnProperty.call(fields, "height")) {
      row.height = fields.height;
    }
    if (Object.prototype.hasOwnProperty.call(fields, "hash")) {
      row.hash = fields.hash;
    }
    if (Object.prototype.hasOwnProperty.call(fields, "tx_count")) {
      row.tx_count = fields.tx_count;
    }
    if (Object.prototype.hasOwnProperty.call(fields, "timestamp")) {
      row.timestamp = fields.timestamp;
      row.time = formatBlockTime(fields.timestamp);
    }
    if (Object.prototype.hasOwnProperty.call(fields, "tx_hashes")) {
      row.tx_hashes = fields.tx_hashes;
    }
    return row;
  }

  function txRowsFromBlock(row) {
    const out = [];
    if (!row || !row.tx_hashes) {
      return out;
    }
    const hashes = String(row.tx_hashes).split(",").filter(Boolean);
    for (let i = 0; i < hashes.length; i += 1) {
      out.push({
        tx_hash: hashes[i],
        height: row.height || "",
        index: String(i)
      });
    }
    return out;
  }

  function explorerAllowed(cmd) {
    const token = String(cmd || "").trim().split(/\s+/)[0];
    return Object.prototype.hasOwnProperty.call(PUBLIC_READ_COMMANDS, token);
  }

  async function explorerCommand(cmd) {
    if (!explorerAllowed(cmd)) {
      return { ok: false, offline: false, raw: "Not found", fields: {} };
    }
    return rpcCommand(cmd);
  }

  async function loadLatestBlockRows(count) {
    const max = typeof count === "number" && count > 0 ? count : 10;
    const info = await explorerCommand("getinfo");
    if (info.offline || !info.ok) {
      return { offline: true, blocks: [], info: info };
    }
    const height = parseHeight(info.fields);
    if (height === null) {
      return { offline: false, blocks: [], info: info };
    }
    const start = Math.max(0, height - (max - 1));
    const blocks = [];
    for (let h = height; h >= start; h -= 1) {
      const result = await explorerCommand("getblock " + h);
      if (result.offline) {
        return { offline: true, blocks: [], info: info };
      }
      if (!result.ok || !result.raw || result.raw.indexOf("error:") === 0) {
        continue;
      }
      const row = blockRowFromFields(result.fields);
      if (!Object.prototype.hasOwnProperty.call(row, "height")
        && !Object.prototype.hasOwnProperty.call(row, "hash")) {
        continue;
      }
      blocks.push(row);
    }
    return { offline: false, blocks: blocks, info: info };
  }

  async function loadLatestTxRows(blockCount, txLimit) {
    const maxBlocks = typeof blockCount === "number" && blockCount > 0 ? blockCount : 10;
    const maxTx = typeof txLimit === "number" && txLimit > 0 ? txLimit : 20;
    const recent = await loadLatestBlockRows(maxBlocks);
    if (recent.offline) {
      return { offline: true, txs: [] };
    }
    const txs = [];
    for (let i = 0; i < (recent.blocks || []).length; i += 1) {
      const fromBlock = txRowsFromBlock(recent.blocks[i]);
      for (let j = 0; j < fromBlock.length; j += 1) {
        txs.push(fromBlock[j]);
        if (txs.length >= maxTx) {
          return { offline: false, txs: txs };
        }
      }
    }
    return { offline: false, txs: txs };
  }

  async function loadChainStatus() {
    const info = await explorerCommand("getinfo");
    if (info.offline || !info.ok) {
      return { offline: true, ok: false, fields: {}, raw: info.raw || "RPC offline" };
    }
    const monetary = await explorerCommand("monetary_info");
    if (monetary.offline) {
      return { offline: true, ok: false, fields: {}, raw: "RPC offline" };
    }
    const fields = mergeFields(info.fields || {}, monetary.ok ? monetary.fields : {});
    return {
      offline: false,
      ok: true,
      fields: fields,
      raw: info.raw,
      strip: stripFields(fields)
    };
  }

  async function resolveSearch(q) {
    const id = String(q || "").trim();
    if (!id) {
      return { kind: "empty" };
    }
    if (isHeightQuery(id)) {
      const block = await explorerCommand("getblock " + id);
      if (block.offline) {
        return { kind: "offline" };
      }
      if (block.ok && block.raw && block.raw.indexOf("error:") !== 0
        && block.fields && Object.keys(block.fields).length > 0) {
        return { kind: "block", id: id, fields: block.fields, raw: block.raw };
      }
      return { kind: "notfound" };
    }
    if (!isHexQuery(id)) {
      return { kind: "notfound" };
    }
    const block = await explorerCommand("getblock " + id);
    if (block.offline) {
      return { kind: "offline" };
    }
    if (block.ok && block.raw && block.raw.indexOf("error:") !== 0
      && block.fields && Object.keys(block.fields).length > 0) {
      return { kind: "block", id: id, fields: block.fields, raw: block.raw };
    }
    const tx = await explorerCommand("tx_status " + id);
    if (tx.offline) {
      return { kind: "offline" };
    }
    if (tx.ok && tx.fields && tx.fields.status && tx.fields.status !== "unknown") {
      return { kind: "tx", id: id, fields: tx.fields, raw: tx.raw };
    }
    if (isAddressQuery(id)) {
      return { kind: "address", id: id };
    }
    if (tx.ok && tx.fields && tx.fields.status === "unknown") {
      return { kind: "tx", id: id, fields: tx.fields, raw: tx.raw };
    }
    return { kind: "notfound" };
  }

  function routeForSearch(result) {
    if (!result || !result.kind || result.kind === "empty" || result.kind === "offline"
      || result.kind === "notfound") {
      return "";
    }
    if (result.kind === "block") {
      return blockHref(result.id);
    }
    if (result.kind === "tx") {
      return txHref(result.id);
    }
    if (result.kind === "address") {
      return addressHref(result.id);
    }
    return "";
  }

  function publicReadCommands() {
    return PUBLIC_READ_COMMANDS;
  }

  global.AdditionSite = {
    rpcUrlFromPage: rpcUrlFromPage,
    parseFields: parseFields,
    parseHeight: parseHeight,
    fieldPresent: fieldPresent,
    fieldOrNull: fieldOrNull,
    rpcCommand: rpcCommand,
    loadRecentBlocks: loadRecentBlocks,
    loadLatestBlockRows: loadLatestBlockRows,
    loadLatestTxRows: loadLatestTxRows,
    loadChainStatus: loadChainStatus,
    renderRecentBlocks: renderRecentBlocks,
    renderFields: renderFields,
    setStatus: setStatus,
    stripFields: stripFields,
    mergeFields: mergeFields,
    isHeightQuery: isHeightQuery,
    isHexQuery: isHexQuery,
    isAddressQuery: isAddressQuery,
    blockSearchCommand: blockSearchCommand,
    formatBlockTime: formatBlockTime,
    blockRowFromFields: blockRowFromFields,
    txRowsFromBlock: txRowsFromBlock,
    explorerAllowed: explorerAllowed,
    explorerCommand: explorerCommand,
    resolveSearch: resolveSearch,
    routeForSearch: routeForSearch,
    blockHref: blockHref,
    txHref: txHref,
    addressHref: addressHref,
    explorerHref: explorerHref,
    withRpc: withRpc,
    publicReadCommands: publicReadCommands,
    STRIP_KEYS: STRIP_KEYS
  };
}(window));
