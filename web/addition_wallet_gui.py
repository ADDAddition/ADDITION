import os
import socket
import threading
import tkinter as tk
from tkinter import ttk, messagebox


# version so we know which script is running
WALLET_VERSION = "2.0"

RPC_HOST = "127.0.0.1"
RPC_PORT = 8545
RPC_TOKEN = os.getenv("ADDITION_RPC_TOKEN", "").strip()


def _utf8_to_hex(s: str) -> str:
    return s.encode("utf-8").hex()


class RpcClient:
    def __init__(self, host: str, port: int, token: str = ""):
        self.host = host
        self.port = port
        self.token = token.strip()

    def call(self, command: str, timeout: float = 6.0) -> str:
        wire = command.strip()
        if self.token:
            wire = f"{self.token} {wire}"
        with socket.create_connection((self.host, self.port), timeout=timeout) as s:
            s.sendall((wire + "\n").encode("utf-8"))
            data = s.recv(65535)
            return data.decode("utf-8", errors="ignore").strip()


class AdditionWalletGUI:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("ADDITION Wallet GUI")
        self.root.geometry("980x700")
        self.root.configure(bg="#0b0c10")

        self.rpc = RpcClient(RPC_HOST, RPC_PORT, RPC_TOKEN)
        self.current_address = ""
        self.current_pub = ""
        self.current_priv = ""
        # update window title with version and path for clarity
        fname = os.path.abspath(__file__)
        self.root.title(f"ADDITION Wallet GUI v{WALLET_VERSION} - {fname}")

        self.style = ttk.Style()
        self.style.theme_use("clam")

        self._build_ui()
        self._start_poll_thread()

    def _build_ui(self):
        header = tk.Frame(self.root, bg="#111217", height=72)
        header.pack(fill="x")

        tk.Label(
            header,
            text="ADDITION",
            bg="#111217",
            fg="#ff3b3b",
            font=("Segoe UI", 24, "bold"),
            padx=16,
        ).pack(side="left")

        self.status_label = tk.Label(
            header,
            text="RPC: connecting...",
            bg="#111217",
            fg="#b0b0b0",
            font=("Consolas", 10),
        )
        self.status_label.pack(side="right", padx=16)

        self.tabs = ttk.Notebook(self.root)
        self.tabs.pack(fill="both", expand=True, padx=16, pady=16)

        self.tab_dashboard = tk.Frame(self.tabs, bg="#0b0c10")
        self.tab_send = tk.Frame(self.tabs, bg="#0b0c10")
        self.tab_mine = tk.Frame(self.tabs, bg="#0b0c10")
        self.tab_stake = tk.Frame(self.tabs, bg="#0b0c10")

        self.tabs.add(self.tab_dashboard, text="Dashboard")
        self.tabs.add(self.tab_send, text="Send")
        self.tabs.add(self.tab_mine, text="Mine")
        self.tabs.add(self.tab_stake, text="Stake")

        self._build_dashboard_tab()
        self._build_send_tab()
        self._build_mine_tab()
        self._build_stake_tab()

    def _entry(self, parent, width=100):
        e = tk.Entry(parent, bg="#171a21", fg="#ffffff", insertbackground="#ffffff", bd=0, width=width)
        return e

    def _build_dashboard_tab(self):
        card = tk.Frame(self.tab_dashboard, bg="#111217", padx=18, pady=18)
        card.pack(fill="x", pady=10)

        tk.Label(card, text="Current wallet", bg="#111217", fg="#c8c8c8", font=("Segoe UI", 11)).pack(anchor="w")

        self.addr_entry = self._entry(card)
        self.addr_entry.pack(fill="x", ipady=8, pady=(8, 6))

        self.pub_entry = self._entry(card)
        self.pub_entry.pack(fill="x", ipady=8, pady=6)

        self.priv_entry = self._entry(card)
        self.priv_entry.pack(fill="x", ipady=8, pady=6)

        btns = tk.Frame(card, bg="#111217")
        btns.pack(fill="x", pady=(10, 0))

        tk.Button(btns, text="Create wallet", command=self.create_wallet, bg="#ff3b3b", fg="white", bd=0, padx=16, pady=10).pack(side="left")
        tk.Button(btns, text="Load fields", command=self.load_wallet_fields, bg="#333a4a", fg="white", bd=0, padx=16, pady=10).pack(side="left", padx=10)
        tk.Button(btns, text="Refresh balance", command=self.refresh_balance, bg="#333a4a", fg="white", bd=0, padx=16, pady=10).pack(side="left")

        bal = tk.Frame(self.tab_dashboard, bg="#111217", padx=18, pady=18)
        bal.pack(fill="x", pady=10)
        tk.Label(bal, text="Balance", bg="#111217", fg="#c8c8c8", font=("Segoe UI", 11)).pack(anchor="w")
        self.balance_label = tk.Label(bal, text="0 ADD", bg="#111217", fg="#ff3b3b", font=("Consolas", 28, "bold"))
        self.balance_label.pack(anchor="w", pady=(6, 0))

        info = tk.Frame(self.tab_dashboard, bg="#111217", padx=18, pady=18)
        info.pack(fill="x", pady=10)
        tk.Label(info, text="Node info", bg="#111217", fg="#c8c8c8", font=("Segoe UI", 11)).pack(anchor="w")
        self.info_label = tk.Label(info, text="-", bg="#111217", fg="#ffffff", font=("Consolas", 10), justify="left")
        self.info_label.pack(anchor="w", pady=(8, 0))

    def _build_send_tab(self):
        wrap = tk.Frame(self.tab_send, bg="#111217", padx=18, pady=18)
        wrap.pack(fill="x", pady=10)

        tk.Label(wrap, text="To address", bg="#111217", fg="#c8c8c8").pack(anchor="w")
        self.send_to = self._entry(wrap)
        self.send_to.pack(fill="x", ipady=8, pady=(6, 12))

        row = tk.Frame(wrap, bg="#111217")
        row.pack(fill="x")

        col1 = tk.Frame(row, bg="#111217")
        col2 = tk.Frame(row, bg="#111217")
        col3 = tk.Frame(row, bg="#111217")
        col1.pack(side="left", fill="x", expand=True)
        col2.pack(side="left", fill="x", expand=True, padx=10)
        col3.pack(side="left", fill="x", expand=True)

        tk.Label(col1, text="Amount", bg="#111217", fg="#c8c8c8").pack(anchor="w")
        self.send_amount = self._entry(col1, width=20)
        self.send_amount.pack(fill="x", ipady=8, pady=(6, 0))

        tk.Label(col2, text="Fee", bg="#111217", fg="#c8c8c8").pack(anchor="w")
        self.send_fee = self._entry(col2, width=20)
        self.send_fee.insert(0, "1")
        self.send_fee.pack(fill="x", ipady=8, pady=(6, 0))

        tk.Label(col3, text="Nonce", bg="#111217", fg="#c8c8c8").pack(anchor="w")
        self.send_nonce = self._entry(col3, width=20)
        self.send_nonce.insert(0, "1")
        self.send_nonce.pack(fill="x", ipady=8, pady=(6, 0))

        tk.Button(wrap, text="Send transaction", command=self.send_transaction, bg="#ff3b3b", fg="white", bd=0, padx=18, pady=10).pack(fill="x", pady=(16, 0))

    def _build_mine_tab(self):
        wrap = tk.Frame(self.tab_mine, bg="#111217", padx=18, pady=18)
        wrap.pack(fill="x", pady=10)

        tk.Label(wrap, text="Reward address (optional)", bg="#111217", fg="#c8c8c8").pack(anchor="w")
        self.mine_addr = self._entry(wrap)
        self.mine_addr.pack(fill="x", ipady=8, pady=(6, 12))

        tk.Button(wrap, text="Mine block", command=self.mine_block, bg="#ff3b3b", fg="white", bd=0, padx=18, pady=10).pack(fill="x")
        # note: mining can take a while; run in background thread to keep UI responsive

    def _build_stake_tab(self):
        wrap = tk.Frame(self.tab_stake, bg="#111217", padx=18, pady=18)
        wrap.pack(fill="x", pady=10)

        tk.Label(wrap, text="Stake amount", bg="#111217", fg="#c8c8c8").pack(anchor="w")
        self.stake_amount = self._entry(wrap)
        self.stake_amount.pack(fill="x", ipady=8, pady=(6, 12))

        row = tk.Frame(wrap, bg="#111217")
        row.pack(fill="x")
        tk.Button(row, text="Stake", command=self.stake, bg="#ff3b3b", fg="white", bd=0, padx=18, pady=10).pack(side="left", fill="x", expand=True)
        tk.Button(row, text="Unstake", command=self.unstake, bg="#333a4a", fg="white", bd=0, padx=18, pady=10).pack(side="left", fill="x", expand=True, padx=10)
        tk.Button(row, text="Claim rewards", command=self.claim_rewards, bg="#333a4a", fg="white", bd=0, padx=18, pady=10).pack(side="left", fill="x", expand=True)

    def _rpc(self, command: str) -> str:
        try:
            return self.rpc.call(command)
        except Exception as e:
            messagebox.showerror("RPC Error", f"Could not contact daemon:\n{e}")
            return f"error: {e}"

    def create_wallet(self):
        res = self._rpc("createwallet")
        if res.startswith("error"):
            messagebox.showerror("Wallet", res)
            return

        parts = {}
        for item in res.split():
            if "=" in item:
                k, v = item.split("=", 1)
                parts[k] = v

        self.current_address = parts.get("address", "")
        self.current_pub = parts.get("pub", "")
        self.current_priv = parts.get("priv", "")

        self.addr_entry.delete(0, tk.END)
        self.addr_entry.insert(0, self.current_address)
        self.pub_entry.delete(0, tk.END)
        self.pub_entry.insert(0, self.current_pub)
        self.priv_entry.delete(0, tk.END)
        self.priv_entry.insert(0, self.current_priv)

        self.mine_addr.delete(0, tk.END)
        self.mine_addr.insert(0, self.current_address)

        messagebox.showinfo("Wallet", "Wallet created. Save your private key securely.")

    def load_wallet_fields(self):
        self.current_address = self.addr_entry.get().strip()
        self.current_pub = self.pub_entry.get().strip()
        self.current_priv = self.priv_entry.get().strip()
        if not self.current_address or not self.current_pub or not self.current_priv:
            messagebox.showwarning("Wallet", "Fill address/pub/priv first.")
            return
        self.mine_addr.delete(0, tk.END)
        self.mine_addr.insert(0, self.current_address)
        messagebox.showinfo("Wallet", "Wallet fields loaded.")

    def refresh_balance(self):
        addr = self.addr_entry.get().strip()
        if not addr:
            return
        res = self._rpc(f"getbalance {addr}")
        if res.startswith("error"):
            self.balance_label.config(text="ERR")
        else:
            self.balance_label.config(text=f"{res} ADD")

    def send_transaction(self):
        from_addr = self.addr_entry.get().strip()
        pub = self.pub_entry.get().strip()
        priv = self.priv_entry.get().strip()
        to_addr = self.send_to.get().strip()
        amount = self.send_amount.get().strip()
        fee = self.send_fee.get().strip() or "1"
        nonce = self.send_nonce.get().strip() or "1"

        if not all([from_addr, pub, priv, to_addr, amount]):
            messagebox.showwarning("Send", "Missing fields.")
            return

        sign_res = self._rpc(f"tx_build {from_addr} {pub} {to_addr} {amount} {fee} {nonce}")
        if sign_res.startswith("error"):
            messagebox.showerror("Send", sign_res)
            return
        if not sign_res.startswith("sign_hash="):
            messagebox.showerror("Send", f"Unexpected tx_build response: {sign_res}")
            return

        sign_hash = sign_res.split("=", 1)[1]
        sign_hex = _utf8_to_hex(sign_hash)
        sig_res = self._rpc(f"sign_message {priv} {sign_hex}")
        if sig_res.startswith("error"):
            messagebox.showerror("Send", sig_res)
            return
        if not sig_res.startswith("pq="):
            messagebox.showerror("Send", f"Unexpected sign_message response: {sig_res}")
            return

        sig_hex_raw = sig_res[3:]
        res = self._rpc(f"sendtx_signed_hash {from_addr} {pub} {to_addr} {amount} {fee} {nonce} {sig_hex_raw}")
        if not res.startswith("error"):
            messagebox.showinfo("Send", f"Transaction sent. tx_hash={res}")
            try:
                self.send_nonce.delete(0, tk.END)
                self.send_nonce.insert(0, str(int(nonce) + 1))
            except Exception:
                pass
            self.refresh_balance()
        else:
            messagebox.showerror("Send", res)

    def mine_block(self):
        addr = self.mine_addr.get().strip()
        cmd = f"mine {addr}" if addr else "mine"
        res = self._rpc(cmd)
        if res.startswith("error"):
            messagebox.showerror("Mine", res)
        else:
            messagebox.showinfo("Mine", res)
            self.refresh_balance()

    def stake(self):
        addr = self.addr_entry.get().strip()
        amount = self.stake_amount.get().strip()
        if not addr or not amount:
            messagebox.showwarning("Stake", "Address/amount missing.")
            return
        res = self._rpc(f"stake {addr} {amount}")
        if res == "ok":
            messagebox.showinfo("Stake", "Staked successfully.")
        else:
            messagebox.showerror("Stake", res)

    def unstake(self):
        addr = self.addr_entry.get().strip()
        amount = self.stake_amount.get().strip()
        if not addr or not amount:
            messagebox.showwarning("Unstake", "Address/amount missing.")
            return
        res = self._rpc(f"unstake {addr} {amount}")
        if res == "ok":
            messagebox.showinfo("Unstake", "Unstaked successfully.")
        else:
            messagebox.showerror("Unstake", res)

    def claim_rewards(self):
        addr = self.addr_entry.get().strip()
        if not addr:
            messagebox.showwarning("Claim", "Address missing.")
            return
        res = self._rpc(f"stake_claim {addr}")
        if res.startswith("error"):
            messagebox.showerror("Claim", res)
        else:
            messagebox.showinfo("Claim", f"Claimed: {res} ADD")

    def _poll(self):
        while True:
            try:
                info = self.rpc.call("getinfo", timeout=2.0)
                self.status_label.config(text=f"RPC OK | {info}", fg="#3ddc84")
            except Exception:
                self.status_label.config(text="RPC OFFLINE", fg="#ff3b3b")
            self.root.after(0, self.refresh_balance)
            threading.Event().wait(4)

    def _start_poll_thread(self):
        t = threading.Thread(target=self._poll, daemon=True)
        t.start()


if __name__ == "__main__":
    root = tk.Tk()
    app = AdditionWalletGUI(root)
    root.mainloop()
