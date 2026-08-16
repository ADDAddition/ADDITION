(function () {
  const NAV = [
    ["/", "Explorer"],
    ["/status/", "Status"],
    ["/join/", "Join"]
  ];

  function rpcQuerySuffix() {
    const params = new URLSearchParams(window.location.search);
    const rpc = params.get("rpc");
    if (!rpc) {
      return "";
    }
    return "?rpc=" + encodeURIComponent(rpc);
  }

  function fillHeader(el) {
    const path = window.location.pathname.replace(/\/+$/, "") || "/";
    const q = rpcQuerySuffix();
    const parts = [];
    for (let i = 0; i < NAV.length; i += 1) {
      const hrefBase = NAV[i][0];
      const label = NAV[i][1];
      const dest = hrefBase.replace(/\/+$/, "") || "/";
      let active = path === dest || (dest !== "/" && path.indexOf(dest) === 0);
      if (dest === "/" && (path === "/" || path === "/explorer")) {
        active = true;
      }
      const href = hrefBase === "/" ? ("/" + q) : (hrefBase + q);
      if (i > 0) {
        parts.push('<span class="nav-sep"> | </span>');
      }
      parts.push(
        '<a href="' + href + '"' + (active ? ' class="active"' : "") + ">" + label + "</a>"
      );
    }
    el.className = "site-chrome";
    el.innerHTML =
      '<div class="site-chrome-inner"><nav id="site-nav">' + parts.join("") + "</nav></div>";
  }

  function fillFooter(el) {
    el.className = "site-footer-wrap";
    el.innerHTML =
      '<p>testnet · <a href="mailto:contact@additionblockchain.com">contact@additionblockchain.com</a></p>';
  }

  const header = document.getElementById("site-header");
  const footer = document.getElementById("site-footer");
  if (header) {
    fillHeader(header);
  }
  if (footer) {
    fillFooter(footer);
  }
}());
