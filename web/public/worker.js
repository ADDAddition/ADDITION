export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const routes = {
      "/": "/index.html",
      "/explorer": "/explorer.html",
      "/status": "/status.html",
      "/contracts": "/contracts.html",
      "/swap": "/swap.html",
      "/evm": "/evm.html",
    };
    if (url.pathname === "/rpc" || url.pathname === "/local-rpc") {
      const upstream = env.PUBLIC_RPC_HTTP;
      if (!upstream) {
        return new Response("RPC offline", {
          status: 503,
          headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" },
        });
      }
      if (url.pathname === "/local-rpc") {
        return new Response("error: local RPC proxy is not available on this host", {
          status: 403,
          headers: { "content-type": "text/plain; charset=utf-8" },
        });
      }
      const dest = new URL(upstream);
      dest.search = url.search;
      return fetch(dest.toString(), { method: request.method, headers: request.headers, body: request.body });
    }
    if (routes[url.pathname]) {
      url.pathname = routes[url.pathname];
    }
    return env.ASSETS.fetch(new Request(url.toString(), request));
  },
};
