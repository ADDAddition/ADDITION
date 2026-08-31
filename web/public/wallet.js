(function () {
  const L = window.AdditionLocal;
  if (!L) {
    return;
  }

  const state = {
    online: false,
    hasWallet: false,
    name: "default",
    address: "",
    balance: null,
    activity: []
  };

  function el(id) {
    return document.getElementById(id);
  }

  function name() {
    const v = L.val("name");
    state.name = v || "default";
    return state.name;
  }

  function showPanel(id) {
    const panels = document.querySelectorAll(".wallet-panel");
    for (let i = 0; i < panels.length; i += 1) {
      panels[i].classList.remove("active");
      panels[i].hidden = true;
    }
    const target = el(id);
    if (target) {
      target.hidden = false;
      target.classList.add("active");
    }
  }

  function setPill(kind, text) {
    const pill = el("conn-pill");
    if (!pill) {
      return;
    }
    pill.className = "pill pill-" + kind;
    pill.textContent = text;
  }

  function pushActivity(title, detail) {
    state.activity.unshift({
      title: title,
      detail: detail || "",
      at: new Date().toISOString()
    });
    if (state.activity.length > 40) {
      state.activity.length = 40;
    }
    renderActivity();
  }

  function renderActivity() {
    const list = el("activity-list");
    if (!list) {
      return;
    }
    list.innerHTML = "";
    if (!state.activity.length) {
      const li = document.createElement("li");
      li.className = "empty";
      li.textContent = "No activity yet.";
      list.appendChild(li);
      return;
    }
    for (let i = 0; i < state.activity.length; i += 1) {
      const item = state.activity[i];
      const li = document.createElement("li");
      const left = document.createElement("div");
      left.innerHTML = "<strong>" + escapeHtml(item.title) + "</strong>" +
        (item.detail ? '<div class="meta">' + escapeHtml(item.detail) + "</div>" : "");
      const right = document.createElement("div");
      right.className = "meta";
      right.textContent = item.at.slice(11, 19) + "Z";
      li.appendChild(left);
      li.appendChild(right);
      list.appendChild(li);
    }
  }

  function escapeHtml(s) {
    return String(s)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function renderHome() {
    el("bal-value").textContent = state.balance === null || state.balance === undefined
      ? "—"
      : String(state.balance);
    el("addr-line").textContent = state.address
      ? shorten(state.address)
      : "No address yet — create or open a wallet";
    el("recv-addr").value = state.address || "";
    const meta = el("home-meta");
    meta.textContent = "Wallet “" + state.name + "” · loopback write only";
  }

  function shorten(addr) {
    if (!addr || addr.length < 20) {
      return addr || "";
    }
    return addr.slice(0, 10) + "…" + addr.slice(-8);
  }

  async function walletInfo() {
    return L.cmd("wallet_info " + name());
  }

  async function refreshBalance() {
    const info = await walletInfo();
    if (info.offline) {
      return info;
    }
    if (info.ok) {
      state.address = info.fields.address || L.field(info.raw, "address") || state.address;
      state.hasWallet = !!state.address;
    }
    const bal = await L.cmd("wallet_balance " + name());
    if (!bal.offline && bal.ok) {
      const rawBal = bal.fields.balance || bal.fields.available || L.field(bal.raw, "balance");
      state.balance = rawBal !== "" ? rawBal : state.balance;
      state.hasWallet = true;
    } else if (bal.raw && bal.raw.indexOf("error:") === 0) {
      state.hasWallet = false;
    }
    renderHome();
    return bal;
  }

  async function enterApp() {
    await refreshBalance();
    if (state.hasWallet) {
      showPanel("panel-home");
      renderHome();
    } else {
      showPanel("panel-setup");
    }
  }

  async function ping() {
    setPill("warn", "Checking…");
    const r = await L.ping(null);
    state.online = !r.offline;
    if (r.offline) {
      setPill("off", "Local node offline");
      L.setButtons(false);
      const retry = el("retry-btn");
      if (retry) {
        retry.disabled = false;
      }
      showPanel("panel-offline");
      return;
    }
    const net = r.fields.network || "local";
    const height = r.fields.height || "?";
    setPill("ok", "Local · " + net + " · h=" + height);
    L.setButtons(true);
    await enterApp();
  }

  function bindNav() {
    const goers = document.querySelectorAll("[data-go]");
    for (let i = 0; i < goers.length; i += 1) {
      goers[i].addEventListener("click", function () {
        const dest = this.getAttribute("data-go");
        if (dest === "receive") {
          showPanel("panel-receive");
        } else if (dest === "send") {
          showPanel("panel-send");
        } else if (dest === "activity") {
          renderActivity();
          showPanel("panel-activity");
        } else if (dest === "more") {
          showPanel("panel-more");
        }
      });
    }
    const backs = document.querySelectorAll("[data-back]");
    for (let j = 0; j < backs.length; j += 1) {
      backs[j].addEventListener("click", function () {
        showPanel("panel-home");
      });
    }
    const moreLink = el("go-more");
    if (moreLink) {
      moreLink.addEventListener("click", function (e) {
        e.preventDefault();
        showPanel("panel-more");
      });
    }
  }

  el("retry-btn").addEventListener("click", function () {
    ping();
  });
  el("refresh-btn").addEventListener("click", async function () {
    await refreshBalance();
    pushActivity("Refreshed balance", String(state.balance) + " ADD");
  });
  el("create-btn").addEventListener("click", async function () {
    const r = await L.cmd("createwallet " + name());
    L.showResult(el("setup-raw"), el("setup-fields"), r);
    el("setup-raw").hidden = false;
    pushActivity("Create wallet", name() + " · " + (r.ok ? "ok" : r.raw));
    if (r.ok) {
      state.hasWallet = true;
      state.address = r.fields.address || L.field(r.raw, "address") || "";
      await refreshBalance();
      showPanel("panel-home");
    }
  });
  el("open-btn").addEventListener("click", async function () {
    const info = await walletInfo();
    L.showResult(el("setup-raw"), el("setup-fields"), info);
    el("setup-raw").hidden = false;
    pushActivity("Open wallet", name());
    if (info.ok) {
      state.hasWallet = true;
      state.address = info.fields.address || L.field(info.raw, "address") || "";
      await refreshBalance();
      showPanel("panel-home");
    }
  });
  el("list-btn").addEventListener("click", async function () {
    const r = await L.cmd("wallet_list");
    L.showResult(el("setup-raw"), el("setup-fields"), r);
    el("setup-raw").hidden = false;
    pushActivity("List wallets", r.ok ? "ok" : r.raw);
  });
  el("copy-addr").addEventListener("click", async function () {
    const addr = el("recv-addr").value;
    if (!addr) {
      return;
    }
    try {
      await navigator.clipboard.writeText(addr);
      pushActivity("Copied address", shorten(addr));
      el("copy-addr").textContent = "Copied";
      setTimeout(function () {
        el("copy-addr").textContent = "Copy address";
      }, 1200);
    } catch (e) {
      el("recv-addr").select();
    }
  });
  el("send-btn").addEventListener("click", async function () {
    let line = "wallet_send " + name() + " " + L.val("to") + " " + L.val("amount");
    if (L.val("fee")) {
      line += " " + L.val("fee");
    }
    const r = await L.cmd(line);
    el("send-raw").textContent = r.raw;
    pushActivity("Send", L.val("amount") + " → " + shorten(L.val("to")) + " · " + (r.ok ? "ok" : "error"));
    if (r.ok) {
      await refreshBalance();
    }
  });
  el("mine-btn").addEventListener("click", async function () {
    const info = await walletInfo();
    if (info.offline || !info.ok) {
      el("mine-raw").textContent = info.raw;
      return;
    }
    const addr = info.fields.address || L.field(info.raw, "address");
    el("mine-raw").textContent = "Mining to " + addr + " …";
    const r = await L.cmd("mine " + addr, 300000);
    el("mine-raw").textContent = r.raw;
    pushActivity("Mine", r.ok ? "block mined" : r.raw);
    await refreshBalance();
    ping();
  });

  async function stakeCmd(line, label) {
    const info = await walletInfo();
    if (info.offline || !info.ok) {
      el("stake-raw").textContent = info.raw;
      return;
    }
    const addr = info.fields.address || L.field(info.raw, "address");
    const r = await L.cmd(line.replace("%ADDR%", addr));
    el("stake-raw").textContent = r.raw;
    pushActivity(label, r.ok ? "ok" : r.raw);
  }

  el("stake-btn").addEventListener("click", function () {
    stakeCmd("stake %ADDR% " + L.val("stake-amt"), "Stake");
  });
  el("unstake-btn").addEventListener("click", function () {
    stakeCmd("unstake %ADDR% " + L.val("stake-amt"), "Unstake");
  });
  el("staked-btn").addEventListener("click", function () {
    stakeCmd("staked %ADDR%", "Staked");
  });
  el("claim-btn").addEventListener("click", function () {
    stakeCmd("stake_claim %ADDR%", "Claim");
  });

  bindNav();
  ping();
}());
