const PAGE_ROUTES = {
  "/": "/index.html",
  "/explorer": "/explorer/index.html",
  "/block": "/block/index.html",
  "/tx": "/tx/index.html",
  "/address": "/address/index.html",
  "/status": "/status/index.html",
  "/rpc": "/rpc/index.html",
  "/join": "/join/index.html",
  "/download": "/download/index.html",
  "/local": "/local/index.html",
  "/wallet": "/wallet/index.html",
  "/tokens": "/tokens/index.html",
  "/privacy": "/privacy/index.html",
  "/docs": "/docs/index.html",
  "/docs/getting-started": "/docs/getting-started/index.html",
  "/docs/architecture": "/docs/architecture/index.html",
  "/docs/commands": "/docs/commands/index.html",
  "/docs/pouw": "/docs/pouw/index.html",
  "/docs/runbook": "/docs/runbook/index.html",
  "/docs/two-node": "/docs/two-node/index.html",
  "/docs/zk": "/docs/zk/index.html",
  "/contracts": "/contracts/index.html",
  "/swap": "/swap/index.html",
  "/evm": "/evm/index.html",
  "/whitepaper": "/whitepaper/index.html",
  "/legal": "/legal/index.html",
  "/embed": "/embed/index.html",
  "/launch": "/launch/index.html",
};

const LAUNCH_PROBE_COMMANDS = [
  { id: "create_token", cmd: "create_token" },
  { id: "token_create", cmd: "token_create" },
  { id: "token_mint", cmd: "token_mint" },
  { id: "swap_pool_create", cmd: "swap_pool_create" },
  { id: "create_pool", cmd: "create_pool" },
  { id: "presale", cmd: "presale" },
  { id: "airdrop", cmd: "airdrop" },
  { id: "farm", cmd: "farm" },
];

function rpcPath(url) {
  return url.pathname.replace(/\/$/, "") || "/";
}

function isRpcProxy(url) {
  const path = rpcPath(url);
  if (path === "/api/rpc" || path === "/local-rpc") {
    return true;
  }
  if (path === "/jsonrpc") {
    return true;
  }
  return path === "/rpc" && url.searchParams.has("cmd");
}

function isPublicJsonApi(url) {
  const path = rpcPath(url);
  return path === "/api" || path === "/api/info" || path === "/api/capabilities" || path === "/api/token";
}

function rpcOffline() {
  return new Response("RPC offline", {
    status: 503,
    headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" },
  });
}

function jsonResponse(status, body) {
  return new Response(JSON.stringify(body), {
    status: status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      "cache-control": "no-store",
      "access-control-allow-origin": "*",
    },
  });
}

function looksLikeHtml(text) {
  const trimmed = String(text || "").trim();
  if (!trimmed) {
    return true;
  }
  const head = trimmed.slice(0, 15).toLowerCase();
  return trimmed.charAt(0) === "<" || head.indexOf("<!doctype") === 0 || head.indexOf("<html") === 0;
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

async function upstreamCmd(upstream, cmd) {
  const dest = new URL(upstream);
  dest.search = "cmd=" + encodeURIComponent(cmd);
  const res = await fetch(dest.toString(), { method: "GET", cache: "no-store" });
  const text = (await res.text()).trim();
  if (looksLikeHtml(text)) {
    return { ok: false, offline: true, raw: "RPC offline", status: 503, fields: {} };
  }
  if (!res.ok || text === "RPC offline" || text.indexOf("error: public read RPC") === 0) {
    return {
      ok: false,
      offline: text === "RPC offline" || res.status === 503,
      raw: text || "RPC offline",
      status: res.status,
      fields: {},
    };
  }
  return {
    ok: text.indexOf("error:") !== 0,
    offline: false,
    raw: text,
    status: res.status,
    fields: parseFields(text),
  };
}

function classifyProbe(result) {
  if (!result || result.offline) {
    return { available: false, reason: "offline" };
  }
  const raw = String(result.raw || "");
  if (raw.indexOf("command disabled on public RPC") !== -1) {
    return { available: false, reason: "disabled_on_public_rpc" };
  }
  if (raw.indexOf("unknown command") !== -1) {
    return { available: false, reason: "unknown_command" };
  }
  if (raw.indexOf("error: usage") === 0) {
    return { available: true, reason: "usage" };
  }
  if (result.ok) {
    return { available: true, reason: "ok" };
  }
  if (raw.indexOf("error:") === 0) {
    // Node recognized the command enough to return a domain error (e.g. missing args).
    return { available: true, reason: "error_response" };
  }
  return { available: false, reason: "unavailable" };
}

async function buildInfoPayload(upstream, symbol) {
  const info = await upstreamCmd(upstream, "getinfo");
  if (info.offline) {
    return {
      status: 503,
      body: {
        ok: false,
        offline: true,
        brand: "ADDITION",
        error: "RPC offline",
        price_available: false,
        price_usd: null,
      },
    };
  }
  if (!info.ok) {
    return {
      status: 502,
      body: {
        ok: false,
        offline: false,
        brand: "ADDITION",
        error: info.raw || "getinfo failed",
        price_available: false,
        price_usd: null,
      },
    };
  }
  const monetary = await upstreamCmd(upstream, "monetary_info");
  const fields = Object.assign({}, info.fields || {}, monetary.ok ? monetary.fields || {} : {});
  const height = parseHeight(fields);
  const body = {
    ok: true,
    offline: false,
    brand: "ADDITION",
    network: fieldOrNull(fields, "network"),
    network_name: fieldOrNull(fields, "network_name"),
    network_id: fieldOrNull(fields, "network_id"),
    height: height,
    peers: fieldOrNull(fields, "peers"),
    pq_mode: fieldOrNull(fields, "pq_mode"),
    pow_algorithm: fieldOrNull(fields, "pow_algorithm"),
    privacy_claim: fieldOrNull(fields, "privacy_claim"),
    max_supply: fieldOrNull(fields, "max_supply"),
    emitted: fieldOrNull(fields, "emitted"),
    remaining: fieldOrNull(fields, "remaining"),
    next_reward: fieldOrNull(fields, "next_reward"),
    next_halving_height: fieldOrNull(fields, "next_halving_height"),
    price_available: false,
    price_usd: null,
    price_note: "No market price RPC on this node",
    source: {
      getinfo: true,
      monetary_info: !!(monetary && monetary.ok),
    },
    raw_fields: fields,
  };

  if (symbol) {
    const token = await upstreamCmd(upstream, "token_info " + symbol);
    if (token.offline) {
      body.token = { symbol: symbol, available: false, reason: "offline" };
    } else {
      const probe = classifyProbe(token);
      body.token = {
        symbol: symbol,
        available: probe.available && token.ok,
        reason: probe.reason,
        fields: token.ok ? token.fields : {},
        raw: token.raw,
      };
      if (!probe.available) {
        body.token.note = "token_info is not available on the public read path";
      }
    }
  } else {
    body.token = null;
  }

  return { status: 200, body: body };
}

async function buildCapabilitiesPayload(upstream) {
  const probes = {};
  let anyAvailable = false;
  for (let i = 0; i < LAUNCH_PROBE_COMMANDS.length; i += 1) {
    const item = LAUNCH_PROBE_COMMANDS[i];
    const result = await upstreamCmd(upstream, item.cmd);
    const classified = classifyProbe(result);
    probes[item.id] = {
      command: item.cmd,
      available: classified.available,
      reason: classified.reason,
      raw: result.offline ? "RPC offline" : result.raw,
    };
    if (classified.available) {
      anyAvailable = true;
    }
  }
  const info = await upstreamCmd(upstream, "getinfo");
  let networkId = null;
  if (!info.offline && info.ok) {
    networkId = fieldOrNull(info.fields || {}, "network_id");
  }
  return {
    status: info.offline ? 503 : 200,
    body: {
      ok: !info.offline,
      offline: !!info.offline,
      brand: "ADDITION",
      network_id: networkId,
      public_write: true,
      launch_tabs_enabled: anyAvailable,
      note: info.offline
        ? "RPC offline"
        : (anyAvailable
          ? "At least one launch command answered on the public path"
          : "Public write on 38546 includes createwallet/mine/wallet_send/sign/tx_build; Create Token / Presale / Airdrop / Farm stay off unless probed available"),
      probes: probes,
    },
  };
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (isPublicJsonApi(url)) {
      if (request.method === "OPTIONS") {
        return new Response(null, {
          status: 204,
          headers: {
            "access-control-allow-origin": "*",
            "access-control-allow-methods": "GET, OPTIONS",
            "access-control-allow-headers": "Content-Type",
            "cache-control": "no-store",
          },
        });
      }
      if (request.method !== "GET" && request.method !== "HEAD") {
        return jsonResponse(405, { ok: false, error: "method not allowed" });
      }
      const upstream = env.PUBLIC_RPC_URL || env.PUBLIC_RPC_HTTP;
      if (!upstream) {
        return jsonResponse(503, {
          ok: false,
          offline: true,
          brand: "ADDITION",
          error: "RPC offline",
          price_available: false,
          price_usd: null,
        });
      }
      try {
        const path = rpcPath(url);
        if (path === "/api/capabilities") {
          const payload = await buildCapabilitiesPayload(upstream);
          return jsonResponse(payload.status, payload.body);
        }
        const symbol = (url.searchParams.get("symbol") || url.searchParams.get("token") || "").trim();
        const payload = await buildInfoPayload(upstream, symbol || null);
        return jsonResponse(payload.status, payload.body);
      } catch (err) {
        return jsonResponse(503, {
          ok: false,
          offline: true,
          brand: "ADDITION",
          error: "RPC offline",
          price_available: false,
          price_usd: null,
        });
      }
    }

    if (isRpcProxy(url)) {
      if (rpcPath(url) === "/local-rpc") {
        return new Response("error: local RPC proxy is not available on this host", {
          status: 403,
          headers: { "content-type": "text/plain; charset=utf-8" },
        });
      }
      const upstream = env.PUBLIC_RPC_URL || env.PUBLIC_RPC_HTTP;
      if (!upstream) {
        return rpcOffline();
      }
      try {
        const dest = new URL(upstream);
        dest.search = url.search;
        const res = await fetch(dest.toString(), {
          method: request.method,
          headers: request.headers,
          body: request.body,
        });
        const text = await res.text();
        if (looksLikeHtml(text)) {
          return rpcOffline();
        }
        return new Response(text, {
          status: res.ok ? 200 : res.status,
          headers: {
            "content-type": res.headers.get("content-type") || "text/plain; charset=utf-8",
            "cache-control": "no-store",
            "access-control-allow-origin": "*",
          },
        });
      } catch (err) {
        return rpcOffline();
      }
    }

    // /rpc docs page (no ?cmd=) is under run_worker_first=["/rpc*"].
    // Rewriting to /rpc/index.html makes Assets 307 → /rpc/ forever.
    // Serve the directory URL so Assets returns index.html with 200.
    if (rpcPath(url) === "/rpc" && !url.searchParams.has("cmd")) {
      if (!url.pathname.endsWith("/")) {
        const slash = new URL(request.url);
        slash.pathname = "/rpc/";
        return Response.redirect(slash.toString(), 308);
      }
      return env.ASSETS.fetch(request);
    }

    const mapped = PAGE_ROUTES[url.pathname.replace(/\/$/, "") || "/"];
    if (mapped) {
      url.pathname = mapped;
    }
    const asset = await env.ASSETS.fetch(new Request(url.toString(), request));
    if (url.pathname.endsWith(".md") && asset.ok) {
      return new Response(asset.body, {
        status: asset.status,
        statusText: asset.statusText,
        headers: {
          "content-type": "text/markdown; charset=utf-8",
          "cache-control": asset.headers.get("cache-control") || "no-store",
        },
      });
    }
    return asset;
  },
};
