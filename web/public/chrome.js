(function () {
  const NAV = [
    ["/", "navHome"],
    ["/network/", "navNetwork"],
    ["/node/", "navNode"],
    ["/about/", "navAbout"]
  ];
  const KPI_FIXED = ["height", "peers", "health"];
  const I18N = {
    en: {
      brandTag: "testnet",
      navHome: "Home",
      navNetwork: "Network",
      navNode: "Node",
      navAbout: "About",
      kpiPending: "…",
      kpiOffline: "RPC offline",
      kpiUp: "up",
      contact: "Contact",
      more: "Also",
      heroTitle: "ADDITION",
      heroLede: "Public read on :38545. Write RPC on 127.0.0.1:8545.",
      heroBody: "getinfo, getblock, monetary_info, peers. mine and createwallet stay on localhost.",
      cardNetwork: "getinfo + getblock",
      cardNode: "additiond --network testnet",
      cardAbout: "What the node does",
      cardExplorer: "height / hash lookup",
      cardDocs: "Build from this tree",
      cardWallet: "Write RPC 127.0.0.1:8545"
    },
    fr: {
      brandTag: "testnet",
      navHome: "Accueil",
      navNetwork: "Réseau",
      navNode: "Nœud",
      navAbout: "À propos",
      kpiPending: "…",
      kpiOffline: "RPC offline",
      kpiUp: "up",
      contact: "Contact",
      more: "Aussi",
      heroTitle: "ADDITION",
      heroLede: "Lecture publique :38545. RPC d’écriture 127.0.0.1:8545.",
      heroBody: "getinfo, getblock, monetary_info, peers. mine et createwallet restent en localhost.",
      cardNetwork: "getinfo + getblock",
      cardNode: "additiond --network testnet",
      cardAbout: "Ce que fait le nœud",
      cardExplorer: "lookup height / hash",
      cardDocs: "Build depuis cet arbre",
      cardWallet: "RPC d’écriture 127.0.0.1:8545"
    }
  };

  function currentLang() {
    try {
      return localStorage.getItem("addition-lang") === "fr" ? "fr" : "en";
    } catch (e) {
      return "en";
    }
  }

  function t(key) {
    const lang = currentLang();
    const table = I18N[lang] || I18N.en;
    return table[key] || I18N.en[key] || key;
  }

  function applyI18n() {
    const lang = currentLang();
    document.documentElement.lang = lang;
    const nodes = document.querySelectorAll("[data-i18n]");
    for (let i = 0; i < nodes.length; i += 1) {
      const key = nodes[i].getAttribute("data-i18n");
      if (key) {
        nodes[i].textContent = t(key);
      }
    }
    const enBtn = document.getElementById("lang-en");
    const frBtn = document.getElementById("lang-fr");
    if (enBtn) {
      enBtn.setAttribute("aria-pressed", lang === "en" ? "true" : "false");
    }
    if (frBtn) {
      frBtn.setAttribute("aria-pressed", lang === "fr" ? "true" : "false");
    }
    const tag = document.getElementById("brand-tag");
    if (tag) {
      tag.textContent = t("brandTag");
    }
    const nav = document.getElementById("site-nav");
    if (nav) {
      const links = nav.querySelectorAll("a[data-nav]");
      for (let i = 0; i < links.length; i += 1) {
        links[i].textContent = t(links[i].getAttribute("data-nav"));
      }
    }
    const health = document.getElementById("kpi-health");
    if (health && health.getAttribute("data-state")) {
      health.textContent = t(health.getAttribute("data-state"));
    }
  }

  function setLang(lang) {
    try {
      localStorage.setItem("addition-lang", lang === "fr" ? "fr" : "en");
    } catch (e) {
      /* ignore */
    }
    const footer = document.getElementById("site-footer");
    if (footer) {
      fillFooter(footer);
    }
    applyI18n();
  }

  function rpcQuerySuffix() {
    const params = new URLSearchParams(window.location.search);
    const rpc = params.get("rpc");
    if (!rpc) {
      return "";
    }
    return "?rpc=" + encodeURIComponent(rpc);
  }

  function fillOrbs() {
    if (document.querySelector(".bg-orb")) {
      return;
    }
    const one = document.createElement("div");
    one.className = "bg-orb orb-1";
    const two = document.createElement("div");
    two.className = "bg-orb orb-2";
    document.body.insertBefore(one, document.body.firstChild);
    document.body.insertBefore(two, one.nextSibling);
  }

  function fillHeader(el) {
    const path = window.location.pathname.replace(/\/+$/, "") || "/";
    const q = rpcQuerySuffix();
    const nav = NAV.map(function (pair) {
      const href = pair[0] === "/" ? "/" + q.replace(/^\?/, "?") : pair[0] + q;
      const dest = pair[0].replace(/\/+$/, "") || "/";
      const active = path === dest || (dest !== "/" && path.indexOf(dest) === 0);
      return '<a href="' + href + '" data-nav="' + pair[1] + '"' +
        (active ? ' class="active"' : "") + ">" + t(pair[1]) + "</a>";
    }).join("");
    el.className = "topbar";
    el.innerHTML =
      '<a class="brand" href="/' + q + '">' +
        '<img src="/assets/logo-transparent.png" alt="ADDITION" width="44" height="44">' +
        "<div>" +
          "<strong>ADDITION</strong>" +
          '<span id="brand-tag">' + t("brandTag") + "</span>" +
        "</div>" +
      "</a>" +
      '<div class="chrome-right">' +
        '<nav class="nav-links" id="site-nav">' + nav + "</nav>" +
        '<div class="lang-toggle" role="group" aria-label="Language">' +
          '<button type="button" id="lang-en" aria-pressed="true">EN</button>' +
          '<button type="button" id="lang-fr" aria-pressed="false">FR</button>' +
        "</div>" +
      "</div>";
    const enBtn = document.getElementById("lang-en");
    const frBtn = document.getElementById("lang-fr");
    if (enBtn) {
      enBtn.addEventListener("click", function () { setLang("en"); });
    }
    if (frBtn) {
      frBtn.addEventListener("click", function () { setLang("fr"); });
    }
  }

  function fillFooter(el) {
    const q = rpcQuerySuffix();
    el.className = "footer";
    el.innerHTML =
      "<p>" + t("contact") + ': <a href="mailto:contact@additionblockchain.com">contact@additionblockchain.com</a></p>' +
      '<p><a href="https://github.com/ADDAddition/ADDITION">github.com/ADDAddition/ADDITION</a> · MIT</p>' +
      '<p class="footer-links">' + t("more") + ": " +
        '<a href="/explorer/' + q + '">Explorer</a>' +
        '<a href="/status/' + q + '">Status</a>' +
        '<a href="/rpc/' + q + '">RPC</a>' +
        '<a href="/docs/' + q + '">Docs</a>' +
        '<a href="/wallet/' + q + '">Wallet</a>' +
        '<a href="/legal/' + q + '">Legal</a>' +
      "</p>";
  }

  function escapeText(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function tpsFromFields(fields) {
    if (!fields) {
      return "";
    }
    if (Object.prototype.hasOwnProperty.call(fields, "tps")) {
      return String(fields.tps);
    }
    if (Object.prototype.hasOwnProperty.call(fields, "last_tps")) {
      return String(fields.last_tps);
    }
    return "";
  }

  function setKpi(id, value, className) {
    const el = document.getElementById(id);
    if (!el) {
      return;
    }
    el.textContent = value;
    if (className) {
      el.className = className;
    }
  }

  function hideTpsTile(hide) {
    const tile = document.getElementById("kpi-tps-tile");
    if (tile) {
      tile.hidden = !!hide;
    }
  }

  function renderKpis(result) {
    const health = document.getElementById("kpi-health");
    if (!result || result.offline || !result.ok) {
      setKpi("kpi-height", "");
      setKpi("kpi-peers", "");
      setKpi("kpi-tps", "");
      hideTpsTile(true);
      if (health) {
        health.setAttribute("data-state", "kpiOffline");
        health.textContent = t("kpiOffline");
        health.className = "status-danger";
      }
      return;
    }
    const fields = result.fields || {};
    setKpi("kpi-height", Object.prototype.hasOwnProperty.call(fields, "height") ? escapeText(fields.height) : "");
    setKpi("kpi-peers", Object.prototype.hasOwnProperty.call(fields, "peers") ? escapeText(fields.peers) : "");
    const tps = tpsFromFields(fields);
    if (tps === "") {
      setKpi("kpi-tps", "");
      hideTpsTile(true);
    } else {
      hideTpsTile(false);
      setKpi("kpi-tps", escapeText(tps));
    }
    if (health) {
      health.setAttribute("data-state", "kpiUp");
      health.textContent = t("kpiUp");
      health.className = "status-ok";
    }
  }

  function parseFields(line) {
    if (window.AdditionSite && typeof window.AdditionSite.parseFields === "function") {
      return window.AdditionSite.parseFields(line);
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
      const key = parts[i].slice(0, eq);
      const value = parts[i].slice(eq + 1);
      if (key) {
        fields[key] = value;
      }
    }
    return fields;
  }

  async function fetchGetinfo() {
    if (window.AdditionSite && typeof window.AdditionSite.rpcCommand === "function") {
      return window.AdditionSite.rpcCommand("getinfo");
    }
    try {
      const res = await fetch("/api/rpc?cmd=getinfo", { method: "GET", cache: "no-store" });
      const raw = (await res.text()).trim();
      const looksLikeHtml = !raw
        || raw.charAt(0) === "<"
        || raw.indexOf("<!DOCTYPE") === 0
        || raw.toLowerCase().indexOf("<html") === 0;
      if (!res.ok || looksLikeHtml || raw === "RPC offline") {
        return { ok: false, offline: true, raw: "RPC offline", fields: {} };
      }
      return {
        ok: true,
        offline: false,
        raw: raw,
        fields: parseFields(raw)
      };
    } catch (e) {
      return { ok: false, offline: true, raw: "RPC offline", fields: {} };
    }
  }

  function ensureKpis() {
    const height = document.getElementById("kpi-height");
    if (!height) {
      return;
    }
    setKpi("kpi-height", "");
    setKpi("kpi-peers", "");
    setKpi("kpi-tps", "");
    hideTpsTile(true);
    const health = document.getElementById("kpi-health");
    if (health) {
      health.setAttribute("data-state", "kpiPending");
      health.textContent = t("kpiPending");
      health.className = "status-warn";
    }
    fetchGetinfo().then(renderKpis);
  }

  fillOrbs();
  const header = document.getElementById("site-header");
  const footer = document.getElementById("site-footer");
  if (header) {
    fillHeader(header);
  }
  if (footer) {
    fillFooter(footer);
  }
  applyI18n();
  ensureKpis();

  window.AdditionChrome = {
    tpsFromFields: tpsFromFields,
    renderKpis: renderKpis,
    kpiFixed: KPI_FIXED
  };
}());
