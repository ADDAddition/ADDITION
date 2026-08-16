(function () {
  const NAV = [
    ["/explorer/", "navExplorer"],
    ["/status/", "navStatus"],
    ["/rpc/", "navRpc"],
    ["/docs/", "navDocs"],
    ["/wallet/", "navWallet"],
    ["/legal/", "navLegal"]
  ];
  const STRIP_KEYS = ["height", "peers", "network", "pq_mode"];
  const I18N = {
    en: {
      brandTag: "testnet",
      navExplorer: "Explorer",
      navStatus: "Status",
      navRpc: "RPC",
      navDocs: "Docs",
      navWallet: "Wallet",
      navLegal: "Legal",
      stripPending: "Checking RPC…",
      stripOffline: "RPC offline",
      stripOk: "getinfo",
      contact: "Contact",
      notice: "Legal",
      heroTitle: "ADDITION testnet",
      heroLede: "Public read: /rpc?cmd=getinfo on :80 and :38545. Write RPC on 127.0.0.1:8545.",
      heroBody: "Build additiond from this repo (main). Bootstrap 34.27.30.115:28545, then type sync. Height should move.",
      cardJoin: "Node start + sync",
      cardExplorer: "getblock, getblockraw, tx_status",
      cardStatus: "getinfo, monetary_info, crypto_selftest, peers",
      cardRpc: "Public read /rpc?cmd=getinfo",
      cardDocs: "Build, commands, architecture",
      cardWallet: "Write RPC 127.0.0.1:8545",
      cardLegal: "MIT",
      factQuantum: "ML-DSA-87. pq_mode from getinfo.",
      factPrivacy: "SHA3-512 opening (privacy_mint_open / privacy_spend_open).",
      factCompat: ":80 works when 38545 is filtered. addpeer after bootstrap is invalid/duplicate.",
      factWrite: "Write RPC 127.0.0.1:8545. Public mine / createwallet disabled."
    },
    fr: {
      brandTag: "testnet",
      navExplorer: "Explorateur",
      navStatus: "État",
      navRpc: "RPC",
      navDocs: "Docs",
      navWallet: "Portefeuille",
      navLegal: "Mentions",
      stripPending: "RPC…",
      stripOffline: "RPC offline",
      stripOk: "getinfo",
      contact: "Contact",
      notice: "Mentions",
      heroTitle: "ADDITION testnet",
      heroLede: "Lecture publique : /rpc?cmd=getinfo sur :80 et :38545. RPC d’écriture 127.0.0.1:8545.",
      heroBody: "Compiler additiond depuis ce dépôt (main). Bootstrap 34.27.30.115:28545, puis taper sync. La hauteur doit avancer.",
      cardJoin: "Démarrer un nœud + sync",
      cardExplorer: "getblock, getblockraw, tx_status",
      cardStatus: "getinfo, monetary_info, crypto_selftest, peers",
      cardRpc: "Lecture publique /rpc?cmd=getinfo",
      cardDocs: "Build, commandes, architecture",
      cardWallet: "RPC d’écriture 127.0.0.1:8545",
      cardLegal: "MIT",
      factQuantum: "ML-DSA-87. pq_mode depuis getinfo.",
      factPrivacy: "Ouverture SHA3-512 (privacy_mint_open / privacy_spend_open).",
      factCompat: ":80 fonctionne si 38545 est filtré. addpeer après bootstrap est invalid/duplicate.",
      factWrite: "RPC d’écriture 127.0.0.1:8545. mine / createwallet publics désactivés."
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
    const flag = document.getElementById("status-flag");
    if (flag && flag.getAttribute("data-state")) {
      flag.textContent = t(flag.getAttribute("data-state"));
    }
    const nav = document.getElementById("site-nav");
    if (nav) {
      const links = nav.querySelectorAll("a[data-nav]");
      for (let i = 0; i < links.length; i += 1) {
        links[i].textContent = t(links[i].getAttribute("data-nav"));
      }
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

  function logoSvg() {
    return '<svg class="brand-mark" viewBox="0 0 28 28" aria-hidden="true">' +
      '<rect width="28" height="28" fill="#1a3a55"/>' +
      '<path d="M6 20 L14 7 L22 20 H18.6 L14 12.4 L9.4 20 Z" fill="#3d9cf0"/>' +
      "</svg>";
  }

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
    const nav = NAV.map(function (pair) {
      const href = pair[0] + q;
      const key = pair[1];
      const dest = pair[0].replace(/\/+$/, "") || "/";
      const active = path === dest || (dest !== "/" && path.indexOf(dest) === 0);
      return '<a href="' + href + '" data-nav="' + key + '"' +
        (active ? ' class="active"' : "") + ">" + t(key) + "</a>";
    }).join("");
    el.className = "site-chrome";
    el.innerHTML =
      '<div class="site-chrome-inner">' +
        '<a class="brand" href="/' + q + '">' +
          logoSvg() +
          '<span class="brand-text">' +
            "<strong class=\"brand-name\">ADDITION</strong>" +
            '<span class="brand-tag" id="brand-tag">' + t("brandTag") + "</span>" +
          "</span>" +
        "</a>" +
        '<div class="chrome-right">' +
          '<nav id="site-nav">' + nav + "</nav>" +
          '<div class="lang-toggle" role="group" aria-label="Language">' +
            '<button type="button" id="lang-en" aria-pressed="true">EN</button>' +
            '<button type="button" id="lang-fr" aria-pressed="false">FR</button>' +
          "</div>" +
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
    el.className = "site-footer-wrap";
    el.innerHTML =
      "<p>" + t("contact") + ': <a href="mailto:contact@additionblockchain.com">contact@additionblockchain.com</a></p>' +
      '<p><a href="https://github.com/ADDAddition/ADDITION">https://github.com/ADDAddition/ADDITION</a> · MIT · ' +
      '<a href="/legal/">' + t("notice") + "</a></p>";
  }

  function emptyStripCells() {
    return STRIP_KEYS.map(function (key) {
      return "<div><dt>" + key + "</dt><dd></dd></div>";
    }).join("");
  }

  function renderStrip(el, result) {
    const flag = el.querySelector("#status-flag");
    const cells = el.querySelector("#status-cells");
    if (!flag || !cells) {
      return;
    }
    if (!result || result.offline || !result.ok) {
      el.className = "status-strip offline";
      flag.setAttribute("data-state", "stripOffline");
      flag.textContent = t("stripOffline");
      cells.innerHTML = emptyStripCells();
      return;
    }
    const fields = (window.AdditionSite && window.AdditionSite.stripFields)
      ? window.AdditionSite.stripFields(result.fields)
      : pickStrip(result.fields || {});
    el.className = "status-strip ok";
    flag.setAttribute("data-state", "stripOk");
    flag.textContent = t("stripOk");
    cells.innerHTML = STRIP_KEYS.map(function (key) {
      const value = Object.prototype.hasOwnProperty.call(fields, key) ? fields[key] : "";
      return "<div><dt>" + key + "</dt><dd>" + escapeText(value) + "</dd></div>";
    }).join("");
  }

  function pickStrip(fields) {
    const out = {};
    for (let i = 0; i < STRIP_KEYS.length; i += 1) {
      const key = STRIP_KEYS[i];
      if (fields && Object.prototype.hasOwnProperty.call(fields, key)) {
        out[key] = fields[key];
      }
    }
    return out;
  }

  function escapeText(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
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

  function ensureStatusStrip(header) {
    let el = document.getElementById("site-status");
    if (!el) {
      el = document.createElement("div");
      el.id = "site-status";
      header.insertAdjacentElement("afterend", el);
    }
    el.className = "status-strip pending";
    el.innerHTML =
      '<div class="status-strip-inner">' +
        '<span class="status-flag" id="status-flag" data-state="stripPending">' + t("stripPending") + "</span>" +
        '<dl class="status-cells" id="status-cells">' + emptyStripCells() + "</dl>" +
      "</div>";
    fetchGetinfo().then(function (result) {
      renderStrip(el, result);
    });
  }

  const header = document.getElementById("site-header");
  const footer = document.getElementById("site-footer");
  if (header) {
    fillHeader(header);
    ensureStatusStrip(header);
  }
  if (footer) {
    fillFooter(footer);
  }
  applyI18n();
}());
