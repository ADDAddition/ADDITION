(function (global) {
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

  async function cmd(command, timeoutMs) {
    // Fail-closed: write commands only ever hit loopback via /local-rpc.
    // Never POST createwallet / wallet_send / mine to public :80, 38545, 38546, or public 8545.
    const ctrl = timeoutMs ? new AbortController() : null;
    const timer = timeoutMs ? setTimeout(function () { ctrl.abort(); }, timeoutMs) : null;
    try {
      const res = await fetch("/local-rpc?cmd=" + encodeURIComponent(command), {
        cache: "no-store",
        signal: ctrl ? ctrl.signal : undefined
      });
      const raw = (await res.text()).trim();
      if (!res.ok || !raw || raw.charAt(0) === "<" || raw.indexOf("<!DOCTYPE") === 0) {
        return { ok: false, offline: true, raw: "RPC offline", fields: {} };
      }
      if (raw === "RPC offline" || raw.indexOf("error: local RPC") === 0) {
        return { ok: false, offline: true, raw: raw, fields: {} };
      }
      const offline = raw === "RPC offline";
      return {
        ok: !offline && raw.indexOf("error:") !== 0,
        offline: offline,
        raw: raw,
        fields: parseFields(raw)
      };
    } catch (e) {
      return { ok: false, offline: true, raw: "RPC offline", fields: {} };
    } finally {
      if (timer) {
        clearTimeout(timer);
      }
    }
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
    el.textContent = "Local trusted RPC answered";
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
