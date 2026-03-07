const API = 'http://127.0.0.1:8080';
let deferredPrompt = null;

window.addEventListener('beforeinstallprompt', (e) => {
  e.preventDefault();
  deferredPrompt = e;
  const btn = document.getElementById('installBtn');
  if (btn) btn.hidden = false;
});

document.getElementById('installBtn').addEventListener('click', async () => {
  if (!deferredPrompt) return;
  deferredPrompt.prompt();
  await deferredPrompt.userChoice;
  deferredPrompt = null;
});

if ('serviceWorker' in navigator) {
  navigator.serviceWorker.register('/sw.js').catch(() => {});
}

const enc = new TextEncoder();
const dec = new TextDecoder();

function b64u(bytes) {
  const b64 = btoa(String.fromCharCode(...new Uint8Array(bytes)));
  return b64.replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/g, '');
}

function b64uToBytes(s) {
  const b64 = s.replace(/-/g, '+').replace(/_/g, '/');
  const pad = b64 + '==='.slice((b64.length + 3) % 4);
  const bin = atob(pad);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

async function ensureLocalPrekey(self) {
  const k = `dh-prekey:${self}`;
  const existing = localStorage.getItem(k);
  if (existing) return JSON.parse(existing);
  const keyPair = await crypto.subtle.generateKey({ name: 'ECDH', namedCurve: 'P-256' }, true, ['deriveBits']);
  const pubRaw = await crypto.subtle.exportKey('raw', keyPair.publicKey);
  const privJwk = await crypto.subtle.exportKey('jwk', keyPair.privateKey);
  const payload = { pub: b64u(pubRaw), priv: privJwk };
  localStorage.setItem(k, JSON.stringify(payload));
  return payload;
}

async function importPeerPublicKey(pubB64u) {
  const raw = b64uToBytes(pubB64u);
  return crypto.subtle.importKey('raw', raw, { name: 'ECDH', namedCurve: 'P-256' }, false, []);
}

async function importLocalPrivateKey(privJwk) {
  return crypto.subtle.importKey('jwk', privJwk, { name: 'ECDH', namedCurve: 'P-256' }, false, ['deriveBits']);
}

async function deriveDhSecret(self, peer) {
  const localRaw = localStorage.getItem(`dh-prekey:${self}`);
  const peerRaw = localStorage.getItem(`dh-peer-prekey:${peer}`);
  if (!localRaw || !peerRaw) return '';
  const local = JSON.parse(localRaw);
  const peerRec = JSON.parse(peerRaw);
  const priv = await importLocalPrivateKey(local.priv);
  const peerPub = await importPeerPublicKey(peerRec.pub);
  const bits = await crypto.subtle.deriveBits({ name: 'ECDH', public: peerPub }, priv, 256);
  const digest = await crypto.subtle.digest('SHA-256', bits);
  return Array.from(new Uint8Array(digest)).map((b) => b.toString(16).padStart(2, '0')).join('');
}

function loadRatchet(self, peer) {
  const key = `ratchet:${[self, peer].sort().join('|')}`;
  try {
    const raw = localStorage.getItem(key);
    if (!raw) return { send: 0, recv: 0 };
    const parsed = JSON.parse(raw);
    return { send: Number(parsed.send || 0), recv: Number(parsed.recv || 0) };
  } catch {
    return { send: 0, recv: 0 };
  }
}

function saveRatchet(self, peer, state) {
  const key = `ratchet:${[self, peer].sort().join('|')}`;
  localStorage.setItem(key, JSON.stringify({ send: state.send || 0, recv: state.recv || 0 }));
}

function saveIdentity() {
  localStorage.setItem('addr', document.getElementById('addr').value.trim());
  localStorage.setItem('peer', document.getElementById('peer').value.trim());
  localStorage.setItem('idPub', document.getElementById('idPub').value.trim());
  localStorage.setItem('idPriv', document.getElementById('idPriv').value.trim());
  localStorage.setItem('pub', document.getElementById('pub').value.trim());
  localStorage.setItem('priv', document.getElementById('priv').value.trim());
  localStorage.setItem('session', document.getElementById('session').value.trim());
}

function loadIdentity() {
  document.getElementById('addr').value = localStorage.getItem('addr') || '';
  document.getElementById('peer').value = localStorage.getItem('peer') || '';
  document.getElementById('idPub').value = localStorage.getItem('idPub') || '';
  document.getElementById('idPriv').value = localStorage.getItem('idPriv') || '';
  document.getElementById('pub').value = localStorage.getItem('pub') || '';
  document.getElementById('priv').value = localStorage.getItem('priv') || '';
  document.getElementById('session').value = localStorage.getItem('session') || '';
}

async function deriveKey(secret) {
  const base = await crypto.subtle.importKey('raw', enc.encode(secret), 'PBKDF2', false, ['deriveKey']);
  return crypto.subtle.deriveKey(
    { name: 'PBKDF2', salt: enc.encode('addition-chat-salt-v1'), iterations: 200000, hash: 'SHA-256' },
    base,
    { name: 'AES-GCM', length: 256 },
    false,
    ['encrypt', 'decrypt']
  );
}

async function deriveChainKey(baseSecret, counter) {
  const input = `${baseSecret}|ctr=${counter}`;
  const digest = await crypto.subtle.digest('SHA-256', enc.encode(input));
  return Array.from(new Uint8Array(digest)).map((b) => b.toString(16).padStart(2, '0')).join('');
}

async function encryptForPeer(plain, self, peer) {
  const state = loadRatchet(self, peer);
  const dh = await deriveDhSecret(self, peer);
  const base = [self, peer].sort().join('|') + '|dh=' + dh;
  const chainKey = await deriveChainKey(base, state.send);
  const key = await deriveKey(chainKey);
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const ct = await crypto.subtle.encrypt({ name: 'AES-GCM', iv }, key, enc.encode(plain));
  const payload = `${state.send}.${btoa(String.fromCharCode(...iv))}.${btoa(String.fromCharCode(...new Uint8Array(ct)))}`;
  state.send += 1;
  saveRatchet(self, peer, state);
  return payload;
}

async function decryptFromPeer(payload, self, peer) {
  const [ctrStr, ivB64, ctB64] = payload.split('.');
  const ctr = parseInt(ctrStr || '0', 10) || 0;
  const iv = Uint8Array.from(atob(ivB64), (c) => c.charCodeAt(0));
  const ct = Uint8Array.from(atob(ctB64), (c) => c.charCodeAt(0));
  const dh = await deriveDhSecret(self, peer);
  const base = [self, peer].sort().join('|') + '|dh=' + dh;
  const chainKey = await deriveChainKey(base, ctr);
  const key = await deriveKey(chainKey);
  const pt = await crypto.subtle.decrypt({ name: 'AES-GCM', iv }, key, ct);
  const state = loadRatchet(self, peer);
  if (ctr >= state.recv) {
    state.recv = ctr + 1;
    saveRatchet(self, peer, state);
  }
  return dec.decode(pt);
}

async function apiPost(path, body) {
  const session = document.getElementById('session').value.trim();
  const res = await fetch(API + path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', ...(session ? { 'X-Session-Token': session } : {}) },
    body: JSON.stringify(body || {})
  });
  return res.json();
}

async function authenticate() {
  const address = document.getElementById('addr').value.trim();
  const pub = document.getElementById('idPub').value.trim();
  const priv = document.getElementById('idPriv').value.trim();
  if (!address || !pub || !priv) return alert('missing address/pub/priv');

  const chRes = await fetch(API + '/api/auth/challenge', {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ address })
  });
  const ch = await chRes.json();
  if (!ch.ok || !ch.challenge) return alert(ch.error || 'challenge failed');

  const sig = prompt('Sign this challenge with your ADDITION identity key and paste sig (without pq=):\n\n' + ch.challenge);
  if (!sig) return;

  const vr = await fetch(API + '/api/auth/verify', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ address, pubkey: pub, sig: sig.trim() })
  });
  const vj = await vr.json();
  if (!vj.ok || !vj.session) return alert(vj.error || 'verify failed');
  document.getElementById('session').value = vj.session;
  localStorage.setItem('session', vj.session);
  alert('authenticated');
}

async function publishPrekey() {
  const self = document.getElementById('addr').value.trim();
  const idPub = document.getElementById('idPub').value.trim();
  const idPriv = document.getElementById('idPriv').value.trim();
  if (!self) return alert('address required');
  if (!idPub || !idPriv) return alert('identity keys required');

  const pre = await ensureLocalPrekey(self);
  const signedMsg = `x3dh-signed-prekey|addr=${self}|spk=${pre.pub}`;
  const sig = prompt('Sign this exact X3DH prekey message with your identity key and paste sig (without pq=):\n\n' + signedMsg);
  if (!sig) return;

  const oneTime = [];
  for (let i = 0; i < 8; i++) {
    const kp = await crypto.subtle.generateKey({ name: 'ECDH', namedCurve: 'P-256' }, true, ['deriveBits']);
    const pubRaw = await crypto.subtle.exportKey('raw', kp.publicKey);
    const privJwk = await crypto.subtle.exportKey('jwk', kp.privateKey);
    const pub = b64u(pubRaw);
    localStorage.setItem(`dh-otk:${self}:${pub}`, JSON.stringify({ priv: privJwk }));
    oneTime.push(pub);
  }

  const r = await apiPost('/api/ratchet/prekey/register', {
    identity_pub: idPub,
    signed_prekey: pre.pub,
    signed_prekey_sig: sig.trim(),
    one_time_prekeys: oneTime
  });
  if (!r.ok) return alert(r.error || 'prekey publish failed');
  alert('x3dh bundle published');
}

async function syncPeerPrekey() {
  const peer = document.getElementById('peer').value.trim();
  if (!peer) return alert('peer required');
  const r = await apiPost('/api/ratchet/prekey/get', { address: peer });
  if (!r.ok || !r.signed_prekey) return alert(r.error || 'peer bundle not found');

  const idPub = (r.identity_pub || '').trim();
  const spk = (r.signed_prekey || '').trim();
  const sig = (r.signed_prekey_sig || '').trim();
  const otk = (r.one_time_prekey || '').trim();

  const signedMsg = `x3dh-signed-prekey|addr=${peer}|spk=${spk}`;
  const msgHex = Array.from(new TextEncoder().encode(signedMsg)).map((b) => b.toString(16).padStart(2, '0')).join('');
  const verifyPrompt = prompt('Verify peer prekey in daemon:\nverify_message <peer_identity_pub> <msg_hex> <sig>\n\nmsg_hex:\n' + msgHex + '\n\nPaste result (true/false):');
  if ((verifyPrompt || '').trim() !== 'true') return alert('peer signed prekey verification failed');

  localStorage.setItem(`dh-peer-prekey:${peer}`, JSON.stringify({ identity_pub: idPub, pub: spk, one_time_prekey: otk, sig, updated: Date.now() }));
  alert('peer bundle synced');
}

async function sendMsg() {
  const self = document.getElementById('addr').value.trim();
  const peer = document.getElementById('peer').value.trim();
  const plain = document.getElementById('plain').value;
  const ttl = parseInt(document.getElementById('ttl').value || '300', 10);
  if (!self || !peer || !plain) return alert('missing fields');

  const cipher = await encryptForPeer(plain, self, peer);
  const clientNonce = crypto.randomUUID();
  const r = await apiPost('/api/pm/send', {
    recipient: peer,
    ciphertext: cipher,
    client_nonce: clientNonce,
    ttl_sec: ttl,
    policy: 'x3dh-aesgcm-ratchet'
  });
  if (!r.ok) return alert(r.error || r.raw || 'send failed');
  document.getElementById('plain').value = '';
  await refreshInbox();
}

async function refreshInbox() {
  const self = document.getElementById('addr').value.trim();
  if (!self) return;
  const r = await apiPost('/api/pm/inbox', {});
  const ul = document.getElementById('inbox');
  ul.innerHTML = '';
  if (!r.ok) {
    ul.innerHTML = '<li>inbox unavailable</li>';
    return;
  }
  const raw = (r.raw || '').trim();
  if (!raw) {
    ul.innerHTML = '<li>empty</li>';
    return;
  }
  const rows = raw.split(',');
  for (const row of rows) {
    const [id, sender, expire] = row.split(':');
    const li = document.createElement('li');
    li.innerHTML = `<div><b>${sender}</b> -> expires ${expire}</div><button>Open</button>`;
    li.querySelector('button').onclick = async () => {
      const x = await apiPost('/api/pm/fetch', { msg_id: id });
      if (!x.ok) return alert(x.error || x.raw || 'fetch failed');
      const c = (x.data && x.data.ciphertext) || '';
      if (!c) return alert('ciphertext blob unavailable');
      try {
        const txt = await decryptFromPeer(c, self, sender);
        alert('Decrypted message:\n\n' + txt);
      } catch {
        alert('decrypt failed');
      }
    };
    ul.appendChild(li);
  }
}

async function quoteSwap() {
  const token_in = document.getElementById('swapIn').value.trim();
  const token_out = document.getElementById('swapOut').value.trim();
  const amount_in = parseInt(document.getElementById('swapAmount').value || '0', 10);
  const r = await apiPost('/api/swap/quote', { token_in, token_out, amount_in });
  document.getElementById('quoteOut').textContent = JSON.stringify(r, null, 2);
}

document.getElementById('saveIdentity').onclick = saveIdentity;
document.getElementById('authBtn').onclick = authenticate;
document.getElementById('prekeyBtn').onclick = publishPrekey;
document.getElementById('syncPeerBtn').onclick = syncPeerPrekey;
document.getElementById('sendBtn').onclick = sendMsg;
document.getElementById('refreshBtn').onclick = refreshInbox;
document.getElementById('quoteBtn').onclick = quoteSwap;

loadIdentity();