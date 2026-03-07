import os
import socket
import threading
import subprocess
import time
import tkinter as tk
from tkinter import ttk, messagebox

# version identifier so users can verify they are running the patched copy
WALLET_VERSION = "2.0"

RPC_HOST = "127.0.0.1"
RPC_PORT = 8545
RPC_TOKEN = os.getenv("ADDITION_RPC_TOKEN", "").strip()


class RpcClient:
    def __init__(self):
        self._lock = threading.Lock()
        self._token = RPC_TOKEN

    def call(self, command: str, timeout: float = 30.0) -> str:
        # raise default timeout to 30s to accommodate mining
        with self._lock:
            last_error = None
            for _ in range(2):
                try:
                    wire = command.strip()
                    if self._token:
                        wire = f"{self._token} {wire}"
                    with socket.create_connection((RPC_HOST, RPC_PORT), timeout=timeout) as s:
                        s.settimeout(timeout)
                        writer = s.makefile("w", encoding="utf-8", newline="\n")
                        reader = s.makefile("r", encoding="utf-8", newline="\n")
                        try:
                            writer.write(wire + "\n")
                            writer.flush()
                            line = reader.readline()
                            if not line:
                                return "error: empty rpc response"
                            return line.strip()
                        finally:
                            reader.close()
                            writer.close()
                except Exception as e:
                    last_error = e
            raise last_error


class AdditionWalletPro:
    def __init__(self, root: tk.Tk):
        self.root = root
        # show version and file path in title for clarity
        fname = os.path.abspath(__file__)
        self.root.title(f"Addition Wallet Pro (ADD) v{WALLET_VERSION} - {fname}")
        self.root.geometry("1180x780")
        self.root.configure(bg="#0d1018")

        self.rpc = RpcClient()
        self._poll_lock = threading.Lock()
        self.addr = tk.StringVar()
        self.pub = tk.StringVar()
        self.priv = tk.StringVar()
        self._mining_stop = threading.Event()
        self._last_mine_info = {}

        self._build()
        self._ensure_backend_running_async()
        # perform an initial status check immediately
        self._start_poll()

    def _rpc_is_up(self, timeout: float = 1.5) -> bool:
        try:
            with socket.create_connection((RPC_HOST, RPC_PORT), timeout=timeout):
                return True
        except Exception:
            return False

    def _find_launcher_script(self):
        # wallet file: .../web/addition_wallet_pro.py -> project root is parent of /web
        project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        candidates = [
            os.path.join(project_root, "scripts", "launch_official_mainnet.ps1"),
            os.path.join(project_root, "scripts", "start_mainnet.ps1"),
            r"C:\Users\admin\ADDITION_FINAL\scripts\launch_official_mainnet.ps1",
            r"C:\Users\admin\ADDITION_FINAL\scripts\start_mainnet.ps1",
        ]
        for path in candidates:
            if os.path.exists(path):
                return path
        return ""

    def _ensure_backend_running_async(self):
        def worker():
            if self._rpc_is_up():
                self.root.after(0, lambda: self._append_mine_log("[wallet] daemon already running"))
                return

            script = self._find_launcher_script()
            if not script:
                self.root.after(0, lambda: self._append_mine_log("[wallet] no launcher script found"))
                return

            try:
                flags = 0
                if hasattr(subprocess, "CREATE_NEW_CONSOLE"):
                    flags = subprocess.CREATE_NEW_CONSOLE
                subprocess.Popen(
                    ["powershell", "-ExecutionPolicy", "Bypass", "-File", script],
                    creationflags=flags,
                    shell=False,
                )
                self.root.after(0, lambda: self._append_mine_log(f"[wallet] starting daemon via {os.path.basename(script)}"))
            except Exception as e:
                self.root.after(0, lambda: self._append_mine_log(f"[wallet] failed to start daemon: {e}"))
                return

            # wait briefly for rpc to come up
            for _ in range(20):
                if self._rpc_is_up(timeout=1.0):
                    self.root.after(0, lambda: self._append_mine_log("[wallet] daemon online (RPC UP)"))
                    return
                time.sleep(0.5)
            self.root.after(0, lambda: self._append_mine_log("[wallet] daemon start timeout (RPC still down)"))

        threading.Thread(target=worker, daemon=True).start()

    def _build(self):
        head = tk.Frame(self.root, bg="#13192a", height=72)
        head.pack(fill="x")
        tk.Label(head, text="Addition Wallet Pro", fg="#ff4040", bg="#13192a", font=("Segoe UI", 22, "bold")).pack(side="left", padx=14)
        tk.Label(head, text="ADD Privacy", fg="#ff6b6b", bg="#ffffff", font=("Segoe UI", 11, "bold"), padx=8, pady=3).pack(side="left", padx=6)
        self.status = tk.Label(head, text="RPC: checking...", fg="#b8c0d9", bg="#13192a", font=("Consolas", 10))
        self.status.pack(side="right", padx=14)

        tabs = ttk.Notebook(self.root)
        tabs.pack(fill="both", expand=True, padx=12, pady=12)

        self.tab_dash = tk.Frame(tabs, bg="#0d1018")
        self.tab_assets = tk.Frame(tabs, bg="#0d1018")
        self.tab_privacy = tk.Frame(tabs, bg="#0d1018")
        self.tab_ops = tk.Frame(tabs, bg="#0d1018")

        tabs.add(self.tab_dash, text="Dashboard")
        tabs.add(self.tab_assets, text="Tokens & NFTs")
        tabs.add(self.tab_privacy, text="Privacy")
        tabs.add(self.tab_ops, text="Ops")

        self._dash()
        self._assets()
        self._privacy()
        self._ops()

    def _entry(self, parent):
        return tk.Entry(parent, bg="#171d30", fg="white", insertbackground="white", bd=0)

    def _copy_to_clipboard(self, text):
        """Copy text to clipboard and show feedback"""
        if text.strip():
            self.root.clipboard_clear()
            self.root.clipboard_append(text.strip())
            messagebox.showinfo("Copied", "Copied to clipboard!")
        else:
            messagebox.showwarning("Empty", "Nothing to copy")

    def _rpc(self, cmd):
        try:
            return self.rpc.call(cmd)
        except Exception as e:
            # IMPORTANT: do not call tkinter dialogs from worker threads.
            # Returning an error string keeps UI thread-safe and prevents crashes.
            return f"error: {e}"

    def _set_status(self, text: str, color: str):
        self.status.config(text=text, fg=color)

    def _dash(self):
        f = tk.Frame(self.tab_dash, bg="#121829", padx=14, pady=14)
        f.pack(fill="x", pady=8)

        tk.Label(f, text="Address", bg="#121829", fg="#cfd7ef").pack(anchor="w")
        row_addr = tk.Frame(f, bg="#121829")
        row_addr.pack(fill="x", pady=4)
        self.e_addr = self._entry(row_addr)
        self.e_addr.pack(side="left", fill="x", expand=True, ipady=8)
        tk.Button(row_addr, text="Copy", command=lambda: self._copy_to_clipboard(self.e_addr.get()), bg="#2b3554", fg="white", bd=0, padx=8, pady=8).pack(side="left", padx=4)

        tk.Label(f, text="Public key", bg="#121829", fg="#cfd7ef").pack(anchor="w")
        row_pub = tk.Frame(f, bg="#121829")
        row_pub.pack(fill="x", pady=4)
        self.e_pub = self._entry(row_pub)
        self.e_pub.pack(side="left", fill="x", expand=True, ipady=8)
        tk.Button(row_pub, text="Copy", command=lambda: self._copy_to_clipboard(self.e_pub.get()), bg="#2b3554", fg="white", bd=0, padx=8, pady=8).pack(side="left", padx=4)

        tk.Label(f, text="Private key", bg="#121829", fg="#cfd7ef").pack(anchor="w")
        row_priv = tk.Frame(f, bg="#121829")
        row_priv.pack(fill="x", pady=4)
        self.e_priv = self._entry(row_priv)
        self.e_priv.pack(side="left", fill="x", expand=True, ipady=8)
        tk.Button(row_priv, text="Copy", command=lambda: self._copy_to_clipboard(self.e_priv.get()), bg="#2b3554", fg="white", bd=0, padx=8, pady=8).pack(side="left", padx=4)

        row = tk.Frame(f, bg="#121829")
        row.pack(fill="x", pady=8)
        tk.Button(row, text="Create Wallet", command=self.create_wallet, bg="#ff3b3b", fg="white", bd=0, padx=12, pady=8).pack(side="left")
        tk.Button(row, text="Load Fields", command=self.load_fields, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left", padx=8)
        tk.Button(row, text="Refresh", command=self.refresh_metrics, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left")

        m = tk.Frame(self.tab_dash, bg="#121829", padx=14, pady=14)
        m.pack(fill="x", pady=8)
        self.lbl_balance = tk.Label(m, text="Balance: -", bg="#121829", fg="#ff4a4a", font=("Consolas", 20, "bold"))
        self.lbl_balance.pack(anchor="w")
        self.lbl_info = tk.Label(m, text="Info: -", bg="#121829", fg="#e8edff", font=("Consolas", 10), justify="left")
        self.lbl_info.pack(anchor="w", pady=8)

    def _assets(self):
        f = tk.Frame(self.tab_assets, bg="#121829", padx=14, pady=14)
        f.pack(fill="x", pady=8)

        tk.Label(f, text="Token create: symbol owner max_supply initial_mint", bg="#121829", fg="#cfd7ef").pack(anchor="w")
        self.tk_create = self._entry(f)
        self.tk_create.pack(fill="x", ipady=8, pady=4)
        tk.Button(f, text="Create Token", command=self.token_create, bg="#ff3b3b", fg="white", bd=0, padx=12, pady=8).pack(anchor="w")

        tk.Label(f, text="Token create ex: symbol name owner max_supply initial_mint decimals burnable_0_1 dev_wallet_or_dash dev_allocation", bg="#121829", fg="#cfd7ef").pack(anchor="w", pady=(10, 0))
        self.tk_create_ex = self._entry(f)
        self.tk_create_ex.pack(fill="x", ipady=8, pady=4)
        tk.Button(f, text="Create Token Ex", command=self.token_create_ex, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(anchor="w")

        tk.Label(f, text="Token transfer: symbol from to amount", bg="#121829", fg="#cfd7ef").pack(anchor="w", pady=(10, 0))
        self.tk_transfer = self._entry(f)
        self.tk_transfer.pack(fill="x", ipady=8, pady=4)
        tk.Button(f, text="Transfer Token", command=self.token_transfer, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(anchor="w")

        tk.Label(f, text="Token policy: symbol caller_owner treasury_or_dash transfer_fee_bps burn_fee_bps paused_0_1", bg="#121829", fg="#cfd7ef").pack(anchor="w", pady=(10, 0))
        self.tk_policy = self._entry(f)
        self.tk_policy.pack(fill="x", ipady=8, pady=4)
        tk.Button(f, text="Set Token Policy", command=self.token_set_policy, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(anchor="w")

        tk.Label(f, text="Token limits: symbol caller_owner max_tx max_wallet", bg="#121829", fg="#cfd7ef").pack(anchor="w", pady=(10, 0))
        self.tk_limits = self._entry(f)
        self.tk_limits.pack(fill="x", ipady=8, pady=4)
        tk.Button(f, text="Set Limits", command=self.token_set_limits, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(anchor="w")

        tk.Label(f, text="Token blacklist: symbol caller_owner wallet blocked_0_1", bg="#121829", fg="#cfd7ef").pack(anchor="w", pady=(10, 0))
        self.tk_blacklist = self._entry(f)
        self.tk_blacklist.pack(fill="x", ipady=8, pady=4)
        tk.Button(f, text="Set Blacklist", command=self.token_blacklist, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(anchor="w")

        tk.Label(f, text="Token fee exempt: symbol caller_owner wallet exempt_0_1", bg="#121829", fg="#cfd7ef").pack(anchor="w", pady=(10, 0))
        self.tk_exempt = self._entry(f)
        self.tk_exempt.pack(fill="x", ipady=8, pady=4)
        tk.Button(f, text="Set Fee Exempt", command=self.token_fee_exempt, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(anchor="w")

        tk.Label(f, text="Token burn: symbol from amount", bg="#121829", fg="#cfd7ef").pack(anchor="w", pady=(10, 0))
        self.tk_burn = self._entry(f)
        self.tk_burn.pack(fill="x", ipady=8, pady=4)
        tk.Button(f, text="Burn Token", command=self.token_burn, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(anchor="w")

        tk.Label(f, text="Token info: symbol", bg="#121829", fg="#cfd7ef").pack(anchor="w", pady=(10, 0))
        self.tk_info = self._entry(f)
        self.tk_info.pack(fill="x", ipady=8, pady=4)
        tk.Button(f, text="Get Token Info", command=self.token_info, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(anchor="w")

        tk.Label(f, text="NFT mint: collection token_id owner metadata", bg="#121829", fg="#cfd7ef").pack(anchor="w", pady=(10, 0))
        self.nft_mint = self._entry(f)
        self.nft_mint.pack(fill="x", ipady=8, pady=4)
        tk.Button(f, text="Mint NFT", command=self.nft_mint_cmd, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(anchor="w")

    def _privacy(self):
        f = tk.Frame(self.tab_privacy, bg="#121829", padx=14, pady=14)
        f.pack(fill="x", pady=8)

        tk.Label(f, text="Privacy mint ZK: owner amount commitment nullifier proof_hex vk_hex", bg="#121829", fg="#cfd7ef").pack(anchor="w")
        self.p_mint_zk = self._entry(f)
        self.p_mint_zk.pack(fill="x", ipady=8, pady=4)
        tk.Button(f, text="Privacy Mint ZK", command=self.privacy_mint_zk, bg="#ff3b3b", fg="white", bd=0, padx=12, pady=8).pack(anchor="w")

        tk.Label(f, text="Privacy spend ZK: owner note_id recipient amount nullifier proof_hex vk_hex", bg="#121829", fg="#cfd7ef").pack(anchor="w", pady=(10, 0))
        self.p_spend_zk = self._entry(f)
        self.p_spend_zk.pack(fill="x", ipady=8, pady=4)
        tk.Button(f, text="Privacy Spend ZK", command=self.privacy_spend_zk, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(anchor="w")

        tk.Label(f, text="Privacy verifier command", bg="#121829", fg="#cfd7ef").pack(anchor="w", pady=(10, 0))
        self.p_verifier = self._entry(f)
        self.p_verifier.insert(0, "python tools/zk_verify_wrapper.py")
        self.p_verifier.pack(fill="x", ipady=8, pady=4)

        row = tk.Frame(f, bg="#121829")
        row.pack(fill="x", pady=6)
        tk.Button(row, text="Set Verifier", command=self.privacy_set_verifier, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left")
        tk.Button(row, text="Strict ON", command=lambda: self.privacy_set_strict(True), bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left", padx=8)
        tk.Button(row, text="Strict OFF", command=lambda: self.privacy_set_strict(False), bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left", padx=8)

        tk.Label(f, text="Private message (best-effort): owner recipient amount", bg="#121829", fg="#cfd7ef").pack(anchor="w", pady=(10, 0))
        self.p_msg = self._entry(f)
        self.p_msg.pack(fill="x", ipady=8, pady=4)
        tk.Button(f, text="Send Private Message (Privacy Note)", command=self.privacy_message_send, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(anchor="w")

    def _ops(self):
        f = tk.Frame(self.tab_ops, bg="#121829", padx=14, pady=14)
        f.pack(fill="x", pady=8)

        tk.Label(f, text="Mine to address", bg="#121829", fg="#cfd7ef").pack(anchor="w")
        self.mine_addr = self._entry(f)
        self.mine_addr.pack(fill="x", ipady=8, pady=4)
        row_mine = tk.Frame(f, bg="#121829")
        row_mine.pack(fill="x", pady=6)
        tk.Button(row_mine, text="Mine Once", command=self.mine, bg="#ff3b3b", fg="white", bd=0, padx=12, pady=8).pack(side="left")
        tk.Button(row_mine, text="Start Auto Mine", command=self.start_auto_mine, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left", padx=8)
        tk.Button(row_mine, text="Stop Auto Mine", command=self.stop_auto_mine, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left", padx=8)
        tk.Button(row_mine, text="Open miner_tool.exe", command=self.open_miner_tool, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left", padx=8)

        tk.Label(f, text="Auto-mine interval (sec)", bg="#121829", fg="#cfd7ef").pack(anchor="w")
        self.mine_interval = self._entry(f)
        self.mine_interval.insert(0, "2")
        self.mine_interval.pack(fill="x", ipady=8, pady=4)

        tk.Label(f, text="Mining console (real RPC output)", bg="#121829", fg="#ff6b6b").pack(anchor="w", pady=(8, 0))
        self.mine_console = tk.Text(f, height=10, bg="#000000", fg="#ff4040", insertbackground="#ff4040", bd=0)
        self.mine_console.pack(fill="x", pady=6)

        stats = tk.Frame(f, bg="#121829")
        stats.pack(fill="x", pady=4)
        self.mine_stats = tk.Label(
            stats,
            text="shares=- hashrate=- H/s block_found=-",
            bg="#121829",
            fg="#ffb3b3",
            font=("Consolas", 10),
            justify="left",
            anchor="w",
        )
        self.mine_stats.pack(fill="x")

        tk.Label(f, text="\nStaking: address amount", bg="#121829", fg="#cfd7ef").pack(anchor="w")
        self.stk_addr = self._entry(f)
        self.stk_addr.pack(fill="x", ipady=8, pady=4)
        self.stk_amt = self._entry(f)
        self.stk_amt.pack(fill="x", ipady=8, pady=4)
        row_stk = tk.Frame(f, bg="#121829")
        row_stk.pack(fill="x", pady=6)
        tk.Button(row_stk, text="Stake", command=self.stake_cmd, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left")
        tk.Button(row_stk, text="Unstake", command=self.unstake_cmd, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left", padx=8)
        tk.Button(row_stk, text="Claim", command=self.stake_claim_cmd, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left", padx=8)
        tk.Button(row_stk, text="Staked Of", command=self.staked_of_cmd, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left", padx=8)

        tk.Label(f, text="\nSigned best-route swap: token_in token_out trader amount_in min_out deadline_unix max_hops", bg="#121829", fg="#cfd7ef").pack(anchor="w")
        self.swap_signed = self._entry(f)
        self.swap_signed.pack(fill="x", ipady=8, pady=4)

        row = tk.Frame(f, bg="#121829")
        row.pack(fill="x", pady=6)
        tk.Button(row, text="Build Sign Payload", command=self.swap_build_payload, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left")
        tk.Button(row, text="Sign Payload", command=self.swap_sign_payload, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left", padx=8)
        tk.Button(row, text="Submit Signed Swap", command=self.swap_submit_signed, bg="#ff3b3b", fg="white", bd=0, padx=12, pady=8).pack(side="left", padx=8)

        tk.Label(f, text="Payload", bg="#121829", fg="#cfd7ef").pack(anchor="w")
        self.swap_payload = self._entry(f)
        self.swap_payload.pack(fill="x", ipady=8, pady=4)

        tk.Label(f, text="Signature (hex, sans pq=)", bg="#121829", fg="#cfd7ef").pack(anchor="w")
        self.swap_sig = self._entry(f)
        self.swap_sig.pack(fill="x", ipady=8, pady=4)

        tk.Label(f, text="\nSwap rapide: token_in token_out trader amount_in min_out", bg="#121829", fg="#cfd7ef").pack(anchor="w")
        self.swap_quick = self._entry(f)
        self.swap_quick.pack(fill="x", ipady=8, pady=4)
        row_sw = tk.Frame(f, bg="#121829")
        row_sw.pack(fill="x", pady=6)
        tk.Button(row_sw, text="Swap Quote", command=self.swap_quote_cmd, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left")
        tk.Button(row_sw, text="Swap Execute", command=self.swap_exec_cmd, bg="#2b3554", fg="white", bd=0, padx=12, pady=8).pack(side="left", padx=8)

    def create_wallet(self):
        r = self._rpc("createwallet")
        if r.startswith("error"):
            return messagebox.showerror("Wallet", r)
        kv = {}
        for x in r.split():
            if "=" in x:
                k, v = x.split("=", 1)
                kv[k] = v
        if not kv.get("address") or not kv.get("pub") or not kv.get("priv"):
            return messagebox.showerror("Wallet", f"invalid createwallet response:\n{r}")
        self.e_addr.delete(0, tk.END); self.e_addr.insert(0, kv.get("address", ""))
        self.e_pub.delete(0, tk.END); self.e_pub.insert(0, kv.get("pub", ""))
        self.e_priv.delete(0, tk.END); self.e_priv.insert(0, kv.get("priv", ""))
        self.mine_addr.delete(0, tk.END); self.mine_addr.insert(0, kv.get("address", ""))
        self.refresh_metrics()

    def load_fields(self):
        self.addr.set(self.e_addr.get().strip())
        self.pub.set(self.e_pub.get().strip())
        self.priv.set(self.e_priv.get().strip())
        self.refresh_metrics()

    def refresh_metrics(self):
        addr = self.e_addr.get().strip()
        if addr:
            b = self._rpc(f"getbalance {addr}")
            self.lbl_balance.config(text=f"Balance: {b} ADD")
        else:
            self.lbl_balance.config(text="Balance: -")
        info = self._rpc("getinfo")
        mon = self._rpc("monetary_info")
        self.lbl_info.config(text=f"{info}\n{mon}")
        self._update_mining_stats_from_info(info)

    @staticmethod
    def _parse_kv_line(line: str):
        out = {}
        for tok in line.split():
            if "=" in tok:
                k, v = tok.split("=", 1)
                out[k.strip()] = v.strip()
        return out

    def _update_mining_stats_from_info(self, info_line: str):
        kv = self._parse_kv_line(info_line)
        if not kv:
            return
        self._last_mine_info = kv

        try:
            iters = int(float(kv.get("last_mine_iterations", "0") or "0"))
        except Exception:
            iters = 0
        try:
            ms = int(float(kv.get("last_mine_ms", "0") or "0"))
        except Exception:
            ms = 0
        try:
            valid_hashes = int(float(kv.get("last_valid_hashes", "0") or "0"))
        except Exception:
            valid_hashes = 0
        try:
            total_blocks_found = int(float(kv.get("total_blocks_found", "0") or "0"))
        except Exception:
            total_blocks_found = 0

        # real hashrate from real counters collected by miner
        hashrate = 0.0 if ms <= 0 else (float(iters) * 1000.0 / float(ms))
        found = "yes" if total_blocks_found > 0 and ms > 0 else "no"

        self.mine_stats.config(
            text=(
                f"shares(valid_hashes)={valid_hashes} "
                f"hashrate={hashrate:.2f} H/s "
                f"block_found={found} "
                f"mine_ms={ms} "
                f"iters={iters} "
                f"blocks_total={total_blocks_found} "
                f"accepted={kv.get('accepted_blocks', '0')} "
                f"rejected={kv.get('rejected_blocks', '0')} "
                f"txs={kv.get('last_mine_txs', '0')}"
            )
        )

    def token_create(self):
        r = self._rpc("token_create " + self.tk_create.get().strip())
        messagebox.showinfo("Token", r)

    def token_create_ex(self):
        r = self._rpc("token_create_ex " + self.tk_create_ex.get().strip())
        messagebox.showinfo("Token", r)

    def token_transfer(self):
        r = self._rpc("token_transfer " + self.tk_transfer.get().strip())
        messagebox.showinfo("Token", r)

    def token_set_policy(self):
        r = self._rpc("token_set_policy " + self.tk_policy.get().strip())
        messagebox.showinfo("Token", r)

    def token_set_limits(self):
        r = self._rpc("token_set_limits " + self.tk_limits.get().strip())
        messagebox.showinfo("Token", r)

    def token_blacklist(self):
        r = self._rpc("token_blacklist " + self.tk_blacklist.get().strip())
        messagebox.showinfo("Token", r)

    def token_fee_exempt(self):
        r = self._rpc("token_fee_exempt " + self.tk_exempt.get().strip())
        messagebox.showinfo("Token", r)

    def token_burn(self):
        r = self._rpc("token_burn " + self.tk_burn.get().strip())
        messagebox.showinfo("Token", r)

    def token_info(self):
        r = self._rpc("token_info " + self.tk_info.get().strip())
        messagebox.showinfo("Token", r)

    def nft_mint_cmd(self):
        r = self._rpc("nft_mint " + self.nft_mint.get().strip())
        messagebox.showinfo("NFT", r)

    def privacy_set_verifier(self):
        cmd = self.p_verifier.get().strip()
        r = self._rpc("privacy_set_verifier " + cmd)
        messagebox.showinfo("Privacy", r)

    def privacy_set_strict(self, enabled: bool):
        r = self._rpc("privacy_strict_mode " + ("on" if enabled else "off"))
        messagebox.showinfo("Privacy", r)

    def privacy_mint_zk(self):
        r = self._rpc("privacy_mint_zk " + self.p_mint_zk.get().strip())
        messagebox.showinfo("Privacy", r)

    def privacy_spend_zk(self):
        r = self._rpc("privacy_spend_zk " + self.p_spend_zk.get().strip())
        messagebox.showinfo("Privacy", r)

    def privacy_message_send(self):
        parts = self.p_msg.get().strip().split()
        if len(parts) < 3:
            return messagebox.showerror("Privacy", "usage: owner recipient amount")
        owner, recipient, amount = parts[:3]
        note = self._rpc(f"privacy_mint {owner} {amount}")
        if note.startswith("error"):
            return messagebox.showerror("Privacy", note)
        spent = self._rpc(f"privacy_spend {owner} {note} {recipient} {amount}")
        messagebox.showinfo("Privacy", spent)

    def mine(self):
        # run mining in background thread to avoid blocking UI
        addr = self.mine_addr.get().strip()
        def do_mine():
            try:
                r = self.rpc.call(f"mine {addr}" if addr else "mine")
            except Exception as e:
                r = f"error: {e}"
            def on_ui():
                if r.startswith("mined"):
                    self._append_mine_log(f"[BLOCK FOUND] {r}")
                else:
                    self._append_mine_log(r)
                self.refresh_metrics()
            self.root.after(0, on_ui)
        threading.Thread(target=do_mine, daemon=True).start()
        self._append_mine_log("[wallet] mining request sent")

    def _append_mine_log(self, line: str):
        ts = time.strftime("%H:%M:%S")
        self.mine_console.insert(tk.END, f"[{ts}] {line}\n")
        self.mine_console.see(tk.END)

    def _auto_mine_worker(self, addr: str, interval_sec: float):
        self._append_mine_log(f"[wallet] auto mine started interval={interval_sec:.2f}s")
        while not self._mining_stop.is_set():
            try:
                r = self.rpc.call(f"mine {addr}" if addr else "mine", timeout=60.0)
            except Exception as e:
                r = f"error: {e}"
            def on_ui(msg=r):
                if msg.startswith("mined"):
                    self._append_mine_log(f"[BLOCK FOUND] {msg}")
                else:
                    self._append_mine_log(msg)
                self.refresh_metrics()
            self.root.after(0, on_ui)
            if self._mining_stop.wait(interval_sec):
                break
        self.root.after(0, lambda: self._append_mine_log("[wallet] auto mine stopped"))

    def start_auto_mine(self):
        if not self._mining_stop.is_set() and hasattr(self, "_mine_thread") and self._mine_thread.is_alive():
            self._append_mine_log("[wallet] auto mine already running")
            return
        addr = self.mine_addr.get().strip()
        try:
            interval_sec = float(self.mine_interval.get().strip() or "2")
        except Exception:
            interval_sec = 2.0
        if interval_sec < 0.5:
            interval_sec = 0.5
        self._mining_stop.clear()
        self._mine_thread = threading.Thread(target=self._auto_mine_worker, args=(addr, interval_sec), daemon=True)
        self._mine_thread.start()

    def stop_auto_mine(self):
        self._mining_stop.set()

    def open_miner_tool(self):
        tool_path = r"C:\Users\admin\Desktop\miner_tool.exe"
        if not os.path.exists(tool_path):
            self._append_mine_log("[wallet] miner_tool.exe not found on Desktop")
            return
        try:
            subprocess.Popen([tool_path], shell=False)
            self._append_mine_log("[wallet] miner_tool.exe launched (design view)")
        except Exception as e:
            self._append_mine_log(f"[wallet] failed to launch miner_tool.exe: {e}")

    def stake_cmd(self):
        addr = self.stk_addr.get().strip()
        amt = self.stk_amt.get().strip()
        r = self._rpc(f"stake {addr} {amt}")
        messagebox.showinfo("Stake", r)

    def unstake_cmd(self):
        addr = self.stk_addr.get().strip()
        amt = self.stk_amt.get().strip()
        r = self._rpc(f"unstake {addr} {amt}")
        messagebox.showinfo("Unstake", r)

    def stake_claim_cmd(self):
        addr = self.stk_addr.get().strip()
        r = self._rpc(f"stake_claim {addr}")
        messagebox.showinfo("Stake Claim", r)

    def staked_of_cmd(self):
        addr = self.stk_addr.get().strip()
        r = self._rpc(f"staked {addr}")
        messagebox.showinfo("Staked", r)

    def swap_quote_cmd(self):
        parts = self.swap_quick.get().strip().split()
        if len(parts) < 5:
            return messagebox.showerror("Swap", "usage: token_in token_out trader amount_in min_out")
        token_in, token_out, _trader, amount_in, _min_out = parts[:5]
        r = self._rpc(f"swap_quote {token_in} {token_out} {amount_in}")
        messagebox.showinfo("Swap Quote", r)

    def swap_exec_cmd(self):
        parts = self.swap_quick.get().strip().split()
        if len(parts) < 5:
            return messagebox.showerror("Swap", "usage: token_in token_out trader amount_in min_out")
        token_in, token_out, trader, amount_in, min_out = parts[:5]
        r = self._rpc(f"swap_exact_in {token_in} {token_out} {trader} {amount_in} {min_out}")
        messagebox.showinfo("Swap Execute", r)

    @staticmethod
    def _hex_utf8(s: str) -> str:
        return s.encode("utf-8").hex()

    def _parse_swap_signed_args(self):
        parts = self.swap_signed.get().strip().split()
        if len(parts) < 7:
            raise ValueError("need: token_in token_out trader amount_in min_out deadline_unix max_hops")
        token_in, token_out, trader, amount_in, min_out, deadline_unix, max_hops = parts[:7]
        return token_in, token_out, trader, amount_in, min_out, deadline_unix, max_hops

    def swap_build_payload(self):
        try:
            token_in, token_out, trader, amount_in, min_out, deadline_unix, max_hops = self._parse_swap_signed_args()
        except Exception as e:
            return messagebox.showerror("Signed Swap", str(e))

        cmd = f"swap_best_route_sign_payload {token_in} {token_out} {trader} {amount_in} {min_out} {deadline_unix} {max_hops}"
        r = self._rpc(cmd)
        if r.startswith("error"):
            return messagebox.showerror("Signed Swap", r)
        self.swap_payload.delete(0, tk.END)
        self.swap_payload.insert(0, r)
        messagebox.showinfo("Signed Swap", "Payload ready. Sign it now.")

    def swap_sign_payload(self):
        priv = self.e_priv.get().strip()
        payload = self.swap_payload.get().strip()
        if not priv or not payload:
            return messagebox.showerror("Signed Swap", "Missing private key or payload")

        payload_hex = self._hex_utf8(payload)
        r = self._rpc(f"sign_message {priv} {payload_hex}")
        if r.startswith("error"):
            return messagebox.showerror("Signed Swap", r)
        sig = r
        if sig.startswith("pq="):
            sig = sig[3:]
        self.swap_sig.delete(0, tk.END)
        self.swap_sig.insert(0, sig)
        messagebox.showinfo("Signed Swap", "Payload signed.")

    def swap_submit_signed(self):
        pub = self.e_pub.get().strip()
        sig = self.swap_sig.get().strip()
        if not pub or not sig:
            return messagebox.showerror("Signed Swap", "Missing public key or signature")

        try:
            token_in, token_out, trader, amount_in, min_out, deadline_unix, max_hops = self._parse_swap_signed_args()
        except Exception as e:
            return messagebox.showerror("Signed Swap", str(e))

        cmd = (
            f"swap_best_route_exact_in_signed {token_in} {token_out} {trader} {amount_in} {min_out} "
            f"{deadline_unix} {max_hops} {pub} {sig}"
        )
        r = self._rpc(cmd)
        if r.startswith("error"):
            return messagebox.showerror("Signed Swap", r)
        messagebox.showinfo("Signed Swap", r)

    def _poll(self):
        while True:
            try:
                with self._poll_lock:
                    h = self._rpc("getinfo")
                    if h.startswith("error"):
                        self.root.after(0, self._set_status, "RPC: OFFLINE", "#ff4d4d")
                    else:
                        self.root.after(0, self._set_status, "RPC: ONLINE", "#6be48f")
            except Exception:
                # keep poll thread alive even on unexpected errors
                self.root.after(0, self._set_status, "RPC: OFFLINE", "#ff4d4d")
            threading.Event().wait(4)

    def _start_poll(self):
        threading.Thread(target=self._poll, daemon=True).start()


if __name__ == "__main__":
    root = tk.Tk()
    app = AdditionWalletPro(root)
    root.mainloop()
