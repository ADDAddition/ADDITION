(function () {
  const links = [
    ["/", "Home"],
    ["/explorer/", "Explorer"],
    ["/status/", "Status"],
    ["/rpc/", "RPC"],
    ["/wallet/", "Wallet"],
    ["/docs/", "Docs"],
    ["/contracts/", "Contracts"],
    ["/swap/", "Swap"],
    ["/evm/", "EVM"],
    ["/whitepaper/", "White paper"],
    ["/legal/", "Legal"]
  ];

  function fillHeader(el) {
    const title = document.body.getAttribute("data-title") || "ADDITION";
    const sub = document.body.getAttribute("data-sub") || "Research prototype / testnet. Not a live mainnet.";
    const nav = links.map(function (pair) {
      return '<a href="' + pair[0] + '">' + pair[1] + "</a>";
    }).join("");
    el.innerHTML =
      "<h1>" + title + "</h1>" +
      "<p>" + sub + "</p>" +
      '<p class="github-line"><a class="github" href="https://github.com/ADDAddition/ADDITION">GitHub: ADDAddition/ADDITION</a></p>' +
      "<nav>" + nav + "</nav>";
  }

  function fillFooter(el) {
    el.innerHTML =
      '<p>Contact: <a href="mailto:contact@additionblockchain.com">contact@additionblockchain.com</a></p>' +
      '<p><a href="https://github.com/ADDAddition/ADDITION">https://github.com/ADDAddition/ADDITION</a> · MIT · <a href="/legal/">Research notice</a></p>';
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
