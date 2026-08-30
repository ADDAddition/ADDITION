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
};

function rpcPath(url) {
  return url.pathname.replace(/\/$/, "") || "/";
}

function isRpcApi(url) {
  const path = rpcPath(url);
  if (path === "/api/rpc" || path === "/local-rpc") {
    return true;
  }
  if (path === "/jsonrpc") {
    return true;
  }
  return path === "/rpc" && url.searchParams.has("cmd");
}

function rpcOffline() {
  return new Response("RPC offline", {
    status: 503,
    headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" },
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

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (isRpcApi(url)) {
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
