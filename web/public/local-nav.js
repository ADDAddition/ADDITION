(function () {
  const ITEMS = [
    ["/local/", "Local"],
    ["/wallet/", "Wallet"],
    ["/tokens/", "Tokens"],
    ["/swap/", "Swap"],
    ["/privacy/", "Privacy"],
    ["/evm/", "EVM"],
    ["/contracts/", "KV"]
  ];

  function fill() {
    const header = document.getElementById("site-header");
    if (!header || document.getElementById("local-nav")) {
      return;
    }
    const path = window.location.pathname.replace(/\/+$/, "") || "/";
    const parts = [];
    for (let i = 0; i < ITEMS.length; i += 1) {
      const href = ITEMS[i][0];
      const dest = href.replace(/\/+$/, "") || "/";
      const active = path === dest || (dest !== "/local" && path.indexOf(dest) === 0);
      if (i > 0) {
        parts.push('<span class="nav-sep"> · </span>');
      }
      parts.push(
        '<a href="' + href + '"' + (active ? ' class="active"' : "") + ">" + ITEMS[i][1] + "</a>"
      );
    }
    const nav = document.createElement("nav");
    nav.id = "local-nav";
    nav.className = "local-nav";
    nav.innerHTML = parts.join("");
    header.insertAdjacentElement("afterend", nav);
  }

  fill();
}());
