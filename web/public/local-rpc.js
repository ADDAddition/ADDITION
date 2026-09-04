(function (global) {
  const PUBLIC_SEED_RPC = "http://34.27.30.115:38546/rpc";

  function parseFields(line) {
    if (global.AdditionSite && typeof global.AdditionSite.parseFields === "function") {
      return global.AdditionSite.parseFields(line);
    }
    const fields = {};
    if (!line || typeof line !== "string") {
      return fields;
    }
    const parts = line.trim().split(/\s+/);
    for (let i = 0; i < parts.length; i += 1) {
      const eq = parts[i].indexOf("=");
      if (eq <= 0) {
        continue;
      }
      fields[parts[i].slice(0, eq)] = parts[i].slice(eq + 1);
    }
    return fields;
  }

  function field(line, key) {
    const fields = parseFields(line);
    if (Object.prototype.hasOwnProperty.call(fields, key)) {
      return fields[key];
    }
    return "";
  }

  function looksOffline(raw, status) {
    if (!raw) {
      return true;
    }
    if (raw.charAt(0) === "<" || raw.indexOf("<!DOCTYPE") === 0) {
      return true;
    }
    if (raw === "RPC offline" || raw.indexOf("error: local RPC") === 0) {
      return true;
    }
    if (raw.indexOf("error: public read RPC") === 0) {
      return true;
    }
    if (status === 503) {
      return true;
    }
    return false;
  }

  function isPublicDisabled(raw) {
    return !!(raw && raw.indexOf("error: command disabled on public RPC") === 0);
  }

  function isLocalProxyMissing(raw, status) {
    if (status === 403 && raw && raw.indexOf("local RPC proxy is not available") !== -1) {
      return true;
    }
    if (raw && raw.indexOf("error: local RPC proxy is loopback-only") === 0) {
      return true;
    }
    return false;
  }

  async function fetchRpc(base, command, timeoutMs) {
    const ctrl = timeoutMs ? new AbortController() : null;
    const timer = timeoutMs ? setTimeout(function () { ctrl.abort(); }, timeoutMs) : null;
    try {
      const url = base.indexOf("?") === -1
        ? base + "?cmd=" + encodeURIComponent(command)
        : base + "&cmd=" + encodeURIComponent(command);
      const res = await fetch(url, {
        cache: "no-store",
        signal: ctrl ? ctrl.signal : undefined
      });
      const raw = (await res.text()).trim();
      if (looksOffline(raw, res.status) || isLocalProxyMissing(raw, res.status)) {
        return { ok: false, offline: true, disabled: false, raw: "RPC offline", fields: {} };
      }
      if (isPublicDisabled(raw)) {
        return { ok: false, offline: false, disabled: true, raw: raw, fields: {} };
      }
      return {
        ok: res.ok && raw.indexOf("error:") !== 0,
        offline: false,
        disabled: false,
        raw: raw,
        fields: parseFields(raw)
      };
    } catch (e) {
      return { ok: false, offline: true, disabled: false, raw: "RPC offline", fields: {} };
    } finally {
      if (timer) {
        clearTimeout(timer);
      }
    }
  }

  function canUseDirectSeed() {
    // HTTPS pages cannot call the plain HTTP seed (mixed content).
    return global.location && global.location.protocol === "http:";
  }

  async function cmd(command, timeoutMs) {
    // Prefer public seed write (CoS open on 38546) via same-origin /api/rpc.
    // Fall back to loopback /local-rpc when public write is offline or filtered.
    // Proxy path form: /local-rpc?cmd=<command>
    const publicResult = await fetchRpc("/api/rpc", command, timeoutMs);
    if (!publicResult.offline && !publicResult.disabled) {
      return Object.assign(publicResult, { via: "public" });
    }

    if (canUseDirectSeed() && (publicResult.offline || publicResult.disabled)) {
      const seedResult = await fetchRpc(PUBLIC_SEED_RPC, command, timeoutMs);
      if (!seedResult.offline && !seedResult.disabled) {
        return Object.assign(seedResult, { via: "public" });
      }
    }

    const localResult = await fetchRpc("/local-rpc", command, timeoutMs);
    if (!localResult.offline) {
      return Object.assign(localResult, { via: "local" });
    }

    return {
      ok: false,
      offline: true,
      disabled: false,
      via: "none",
      raw: "RPC offline",
      fields: {}
    };
  }

  function setButtons(enabled) {
    const buttons = document.querySelectorAll("main button");
    for (let i = 0; i < buttons.length; i += 1) {
      buttons[i].disabled = !enabled;
    }
  }

  function setState(el, result) {
    if (!el) {
      return;
    }
    if (!result || result.offline) {
      el.className = "offline";
      el.textContent = "RPC offline";
      return;
    }
    el.className = "ok";
    if (result.via === "public") {
      el.textContent = "Public mainnet RPC answered (38546 write)";
    } else {
      el.textContent = "Local write RPC answered";
    }
  }

  function showResult(rawEl, fieldsEl, result) {
    const raw = result && result.raw ? result.raw : "RPC offline";
    if (rawEl) {
      rawEl.textContent = raw;
    }
    if (!fieldsEl) {
      return;
    }
    if (global.AdditionSite && typeof global.AdditionSite.renderFields === "function") {
      if (!result || result.offline || raw.indexOf("error:") === 0) {
        global.AdditionSite.renderFields(fieldsEl, {}, raw);
      } else {
        global.AdditionSite.renderFields(fieldsEl, result.fields, "Node returned no key=value fields.");
      }
    }
  }

  function val(id) {
    const el = document.getElementById(id);
    return el ? String(el.value || "").trim() : "";
  }

  function setVal(id, value) {
    const el = document.getElementById(id);
    if (el && value) {
      el.value = value;
    }
  }

  async function ping(stateEl) {
    const result = await cmd("getinfo");
    setState(stateEl, result);
    setButtons(!result.offline);
    return result;
  }

  global.AdditionLocal = {
    cmd: cmd,
    field: field,
    parseFields: parseFields,
    setButtons: setButtons,
    setState: setState,
    showResult: showResult,
    val: val,
    setVal: setVal,
    ping: ping
  };
}(window));
