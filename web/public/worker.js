const PAGE_ROUTES = {
  "/": "/index.html",
  "/explorer": "/explorer/index.html",
  "/status": "/status/index.html",
  "/rpc": "/rpc/index.html",
  "/wallet": "/wallet/index.html",
  "/docs": "/docs/index.html",
  "/docs/getting-started": "/docs/getting-started/index.html",
  "/docs/architecture": "/docs/architecture/index.html",
  "/docs/commands": "/docs/commands/index.html",
  "/docs/pouw": "/docs/pouw/index.html",
  "/docs/runbook": "/docs/runbook/index.html",
  "/docs/zk": "/docs/zk/index.html",
  "/contracts": "/contracts/index.html",
  "/swap": "/swap/index.html",
  "/evm": "/evm/index.html",
  "/whitepaper": "/whitepaper/index.html",
  "/legal": "/legal/index.html",
};

function isRpcApi(url) {
  if (url.pathname === "/api/rpc" || url.pathname === "/local-rpc") {
    return true;
  }
  return url.pathname === "/rpc" && url.searchParams.has("cmd");
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (isRpcApi(url)) {
      if (url.pathname === "/local-rpc") {
        return new Response("error: local RPC proxy is not available on this host", {
          status: 403,
          headers: { "content-type": "text/plain; charset=utf-8" },
        });
      }
      const upstream = env.PUBLIC_RPC_HTTP;
      if (!upstream) {
        return new Response("RPC offline", {
          status: 503,
          headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" },
        });
      }
      const dest = new URL(upstream);
      dest.search = url.search;
      return fetch(dest.toString(), { method: request.method, headers: request.headers, body: request.body });
    }
    const mapped = PAGE_ROUTES[url.pathname.replace(/\/$/, "") || "/"];
    if (mapped) {
      url.pathname = mapped;
    }
    return env.ASSETS.fetch(new Request(url.toString(), request));
  },
};
