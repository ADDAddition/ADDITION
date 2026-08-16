(function (global) {
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
      dd.textContent = fields[keys[i]];
      dl.appendChild(dt);
      dl.appendChild(dd);
    }
    target.appendChild(dl);
  }

  function parseHeight(fields) {
    if (!fields || fields.height === undefined) {
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
    const keys = ["height", "peers", "network", "pq_mode"];
    const out = {};
    if (!fields) {
      return out;
    }
    for (let i = 0; i < keys.length; i += 1) {
      const key = keys[i];
      if (Object.prototype.hasOwnProperty.call(fields, key)) {
        out[key] = fields[key];
      }
    }
    return out;
  }

  global.AdditionSite = {
    rpcUrlFromPage: rpcUrlFromPage,
    parseFields: parseFields,
    parseHeight: parseHeight,
    rpcCommand: rpcCommand,
    loadRecentBlocks: loadRecentBlocks,
    renderRecentBlocks: renderRecentBlocks,
    renderFields: renderFields,
    setStatus: setStatus,
    stripFields: stripFields
  };
}(window));
