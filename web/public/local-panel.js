(function (global) {
  const LOGO_SRC = "/assets/addition-logo.svg";
  const PLACEHOLDER_SRC = "/assets/getinfo-placeholder.svg";
  const PLACEHOLDER_ALT = "PLACEHOLDER: RPC offline. No getinfo shot.";
  const LIVE_ALT = "live getinfo from 127.0.0.1:8545";

  function escapeXml(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function wrapText(text, width) {
    const words = String(text || "").trim().split(/\s+/);
    const lines = [];
    let current = "";
    for (let i = 0; i < words.length; i += 1) {
      const word = words[i];
      const next = current ? current + " " + word : word;
      if (next.length > width && current) {
        lines.push(current);
        current = word;
      } else {
        current = next;
      }
    }
    if (current) {
      lines.push(current);
    }
    return lines.slice(0, 14);
  }

  function liveGetinfoSvg(raw) {
    const lines = wrapText(raw, 72);
    const height = 44 + lines.length * 16;
    const width = 560;
    const body = lines.map(function (line, idx) {
      return '<text x="16" y="' + (40 + idx * 16) +
        '" fill="#dce3ea" font-family="ui-monospace, SFMono-Regular, Menlo, Consolas, monospace" font-size="11">' +
        escapeXml(line) + "</text>";
    }).join("");
    return '<svg xmlns="http://www.w3.org/2000/svg" width="' + width +
      '" height="' + height + '" viewBox="0 0 ' + width + " " + height +
      '" role="img" aria-label="' + escapeXml(LIVE_ALT) + '">' +
      "<title>" + escapeXml(LIVE_ALT) + "</title>" +
      '<rect width="' + width + '" height="' + height + '" fill="#0d0f12"/>' +
      '<rect x="1" y="1" width="' + (width - 2) + '" height="' + (height - 2) +
      '" fill="none" stroke="#1c232c"/>' +
      '<text x="16" y="20" fill="#3d9a6a" font-family="ui-monospace, SFMono-Regular, Menlo, Consolas, monospace" font-size="12" font-weight="700">getinfo 127.0.0.1:8545</text>' +
      body +
      "</svg>";
  }

  function applyLogo(img) {
    if (!img) {
      return;
    }
    img.src = LOGO_SRC;
    img.alt = "ADDITION";
    img.width = 64;
    img.height = 64;
  }

  function applyGetinfoShot(img, result) {
    if (!img) {
      return;
    }
    const offline = !result || result.offline || !result.raw ||
      result.raw === "RPC offline" || result.raw.indexOf("error:") === 0;
    if (offline) {
      img.removeAttribute("src");
      img.src = PLACEHOLDER_SRC;
      img.alt = PLACEHOLDER_ALT;
      return;
    }
    img.src = "data:image/svg+xml;charset=utf-8," + encodeURIComponent(liveGetinfoSvg(result.raw));
    img.alt = LIVE_ALT;
  }

  global.AdditionLocalPanel = {
    LOGO_SRC: LOGO_SRC,
    PLACEHOLDER_SRC: PLACEHOLDER_SRC,
    PLACEHOLDER_ALT: PLACEHOLDER_ALT,
    LIVE_ALT: LIVE_ALT,
    liveGetinfoSvg: liveGetinfoSvg,
    applyLogo: applyLogo,
    applyGetinfoShot: applyGetinfoShot
  };
}(window));
