(function () {
  const PRIMARY = [
    ["/", "Explore", "explore"],
    ["/wallet/", "Wallet", "wallet"],
    ["/join/", "Get started", "join"],
    ["/status/", "Status", "status"]
  ];

  const MORE = [
    ["/download/", "Download"],
    ["/local/", "Local tools"],
    ["/rpc/", "Public RPC"],
    ["/docs/", "Docs"]
  ];

  function rpcQuerySuffix() {
    const params = new URLSearchParams(window.location.search);
    const rpc = params.get("rpc");
    if (!rpc) {
      return "";
    }
    return "?rpc=" + encodeURIComponent(rpc);
  }

  function withQ(hrefBase) {
    const q = rpcQuerySuffix();
    if (hrefBase === "/") {
      return "/" + q;
    }
    return hrefBase + q;
  }

  function currentPath() {
    return window.location.pathname.replace(/\/+$/, "") || "/";
  }

  function isActive(hrefBase, path) {
    const dest = hrefBase.replace(/\/+$/, "") || "/";
    if (dest === "/") {
      return path === "/" || path === "/explorer"
        || path === "/block" || path === "/tx" || path === "/address";
    }
    return path === dest || path.indexOf(dest) === 0;
  }

  function injectHead() {
    const head = document.head;
    if (!head || head.getAttribute("data-addition-pwa") === "1") {
      return;
    }
    head.setAttribute("data-addition-pwa", "1");

    function ensureLink(rel, href, attrs) {
      if (head.querySelector('link[rel="' + rel + '"][href="' + href + '"]')) {
        return;
      }
      const link = document.createElement("link");
      link.rel = rel;
      link.href = href;
      if (attrs) {
        const keys = Object.keys(attrs);
        for (let i = 0; i < keys.length; i += 1) {
          link.setAttribute(keys[i], attrs[keys[i]]);
        }
      }
      head.appendChild(link);
    }

    function ensureMeta(name, content) {
      let el = head.querySelector('meta[name="' + name + '"]');
      if (!el) {
        el = document.createElement("meta");
        el.setAttribute("name", name);
        head.appendChild(el);
      }
      el.setAttribute("content", content);
    }

    ensureLink("manifest", "/manifest.webmanifest");
    ensureLink("apple-touch-icon", "/apple-touch-icon.png");
    ensureMeta("theme-color", "#f3f6fa");
    ensureMeta("apple-mobile-web-app-capable", "yes");
    ensureMeta("apple-mobile-web-app-status-bar-style", "default");
    ensureMeta("apple-mobile-web-app-title", "ADDITION");
    ensureMeta("mobile-web-app-capable", "yes");

    if (!head.querySelector('link[href*="fonts.googleapis.com"]')) {
      ensureLink("preconnect", "https://fonts.googleapis.com");
      ensureLink("preconnect", "https://fonts.gstatic.com", { crossorigin: "anonymous" });
      ensureLink(
        "stylesheet",
        "https://fonts.googleapis.com/css2?family=Outfit:wght@400;500;600;700&family=IBM+Plex+Mono:wght@400;500&display=swap"
      );
    }

    if ("serviceWorker" in navigator) {
      window.addEventListener("load", function () {
        navigator.serviceWorker.register("/sw.js").catch(function () {});
      });
    }
  }

  function navLinks(items, path, className) {
    const parts = [];
    for (let i = 0; i < items.length; i += 1) {
      const hrefBase = items[i][0];
      const label = items[i][1];
      const active = isActive(hrefBase, path);
      parts.push(
        '<a href="' + withQ(hrefBase) + '"' +
        (active ? ' class="active"' : "") +
        (className ? ' data-tab="' + (items[i][2] || "") + '"' : "") +
        ">" + label + "</a>"
      );
    }
    return parts.join("");
  }

  function bottomTabIcon(key) {
    const icons = {
      explore: '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="11" cy="11" r="7" fill="none" stroke="currentColor" stroke-width="2"/><path d="M20 20l-3.5-3.5" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>',
      wallet: '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3" y="6" width="18" height="13" rx="2" fill="none" stroke="currentColor" stroke-width="2"/><path d="M16 12h5" fill="none" stroke="currentColor" stroke-width="2"/><circle cx="16.5" cy="12.5" r="1" fill="currentColor"/></svg>',
      join: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3v12M8 11l4 4 4-4" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><path d="M5 19h14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>',
      status: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 19V9M10 19V5M16 19v-7M22 19H2" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>'
    };
    return icons[key] || "";
  }

  function fillHeader(el) {
    const path = currentPath();
    const desktopNav = navLinks(PRIMARY, path, false);
    const moreNav = navLinks(MORE, path, false);

    el.className = "site-chrome";
    el.innerHTML =
      '<div class="site-chrome-inner">' +
      '<a class="brand" href="' + withQ("/") + '">' +
      '<img class="brand-logo" src="/logo-transparent.png" alt="ADDITION">' +
      '<span class="brand-text"><span class="brand-name">ADDITION</span>' +
      '<span class="brand-tag">Layer 1 · ML-DSA-87</span></span></a>' +
      '<div class="chrome-right">' +
      '<span class="net-badge" title="Public product is ADDITION_MAINNET_V1">MAINNET</span>' +
      '<nav class="site-nav-desktop" id="site-nav" aria-label="Primary">' + desktopNav + "</nav>" +
      '<details class="more-menu">' +
      '<summary aria-label="More pages">More</summary>' +
      '<div class="more-menu-panel">' + moreNav + "</div>" +
      "</details>" +
      '<button type="button" class="menu-toggle" id="menu-toggle" aria-expanded="false" aria-controls="mobile-drawer">Menu</button>' +
      "</div></div>" +
      '<div class="mobile-drawer" id="mobile-drawer" hidden>' +
      '<nav class="mobile-drawer-nav" aria-label="All pages">' +
      desktopNav + moreNav +
      "</nav></div>";

    const toggle = document.getElementById("menu-toggle");
    const drawer = document.getElementById("mobile-drawer");
    if (toggle && drawer) {
      toggle.addEventListener("click", function () {
        const open = drawer.hasAttribute("hidden");
        if (open) {
          drawer.removeAttribute("hidden");
          toggle.setAttribute("aria-expanded", "true");
        } else {
          drawer.setAttribute("hidden", "");
          toggle.setAttribute("aria-expanded", "false");
        }
      });
    }
  }

  function fillBottomTabs() {
    if (document.getElementById("bottom-tabs")) {
      return;
    }
    const path = currentPath();
    // Wallet page owns its Trust-like Home / Receive / Send / Activity tabs.
    if (path === "/wallet") {
      return;
    }
    const nav = document.createElement("nav");
    nav.id = "bottom-tabs";
    nav.className = "bottom-tabs";
    nav.setAttribute("aria-label", "Primary mobile");
    const parts = [];
    for (let i = 0; i < PRIMARY.length; i += 1) {
      const hrefBase = PRIMARY[i][0];
      const label = PRIMARY[i][1];
      const key = PRIMARY[i][2];
      const active = isActive(hrefBase, path);
      parts.push(
        '<a href="' + withQ(hrefBase) + '" class="bottom-tab' + (active ? " active" : "") + '">' +
        '<span class="bottom-tab-icon">' + bottomTabIcon(key) + "</span>" +
        '<span class="bottom-tab-label">' + label + "</span></a>"
      );
    }
    nav.innerHTML = parts.join("");
    document.body.appendChild(nav);
    document.body.classList.add("has-bottom-tabs");
  }

  function ensureAnimatedBackground() {
    if (document.getElementById("site-bg")) {
      return;
    }
    const bg = document.createElement("div");
    bg.id = "site-bg";
    bg.className = "site-bg";
    bg.setAttribute("aria-hidden", "true");
    bg.innerHTML =
      '<div class="site-bg-mesh"></div>' +
      '<div class="site-bg-mesh-2"></div>' +
      '<div class="site-bg-dots"></div>';
    const root = document.body;
    if (root.firstChild) {
      root.insertBefore(bg, root.firstChild);
    } else {
      root.appendChild(bg);
    }
  }

  function fillFooter(el) {
    el.className = "site-footer-wrap";
    el.innerHTML =
      '<p>ADDITION · public product is <strong>MAINNET</strong> · keys stay on your device or local node</p>' +
      '<p><a href="/download/">Download</a> · <a href="/docs/">Docs</a> · ' +
      '<a href="mailto:contact@additionblockchain.com">contact@additionblockchain.com</a></p>';
  }

  injectHead();
  ensureAnimatedBackground();

  const header = document.getElementById("site-header");
  const footer = document.getElementById("site-footer");
  if (header) {
    fillHeader(header);
  }
  if (footer) {
    fillFooter(footer);
  }
  fillBottomTabs();
}());
