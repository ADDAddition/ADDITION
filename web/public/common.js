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

  function setStatus(el, result) {
    if (!el) {
      return;
    }
    if (result.offline) {
      el.className = "offline";
      el.textContent = "RPC offline";
      return;
    }
    el.className = "";
    el.textContent = result.ok ? "RPC answered" : (result.raw || "RPC error");
  }

  global.AdditionSite = {
    rpcUrlFromPage: rpcUrlFromPage,
    parseFields: parseFields,
    rpcCommand: rpcCommand,
    renderFields: renderFields,
    setStatus: setStatus
  };
}(window));
