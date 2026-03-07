import json
import os
import time
import socket
from http.server import BaseHTTPRequestHandler, HTTPServer

HOST = "127.0.0.1"
PORT = 9545

CHAIN_ID_HEX = hex(424242)
NETWORK_NAME = "Addition EVM Bridge"
NATIVE_SYMBOL = "ADD"
RPC_URL = f"http://{HOST}:{PORT}"
EXPLORER_URL = "http://127.0.0.1:8080"
CORE_RPC_HOST = "127.0.0.1"
CORE_RPC_PORT = 8545
CORE_RPC_TOKEN = os.getenv("ADDITION_RPC_TOKEN", "").strip()


def core_rpc(command: str, timeout: float = 4.0) -> str:
    wire = command.strip()
    if CORE_RPC_TOKEN:
        wire = f"{CORE_RPC_TOKEN} {wire}"
    with socket.create_connection((CORE_RPC_HOST, CORE_RPC_PORT), timeout=timeout) as s:
        s.sendall((wire + "\n").encode("utf-8"))
        data = s.recv(65535)
        return data.decode("utf-8", errors="ignore").strip()



class Handler(BaseHTTPRequestHandler):
    def _json(self, payload: dict, status: int = 200):
        data = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Methods", "POST, OPTIONS")
        self.end_headers()
        self.wfile.write(data)

    def do_OPTIONS(self):
        self._json({}, status=204)

    def do_POST(self):
        try:
            n = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(n)
            req = json.loads(raw.decode("utf-8"))

            method = req.get("method", "")
            params = req.get("params", [])
            req_id = req.get("id")

            result = self.handle_method(method, params)
            self._json({"jsonrpc": "2.0", "id": req_id, "result": result})
        except Exception as e:
            self._json({
                "jsonrpc": "2.0",
                "id": None,
                "error": {"code": -32000, "message": str(e)},
            }, status=500)

    def handle_method(self, method: str, params):
        if method == "web3_clientVersion":
            return "AdditionEVMBridge/1.0"

        if method == "eth_chainId":
            return CHAIN_ID_HEX

        if method == "net_version":
            return str(int(CHAIN_ID_HEX, 16))

        if method == "eth_blockNumber":
            return hex(0)

        if method == "eth_getBlockByNumber":
            tag = params[0] if params else "latest"
            if tag not in ("latest", "0x0"):
                return None
            return {
                "number": "0x0",
                "hash": "0x" + "0" * 64,
                "parentHash": "0x" + "0" * 64,
                "nonce": "0x0000000000000000",
                "sha3Uncles": "0x" + "0" * 64,
                "logsBloom": "0x" + "0" * 512,
                "transactionsRoot": "0x" + "0" * 64,
                "stateRoot": "0x" + "0" * 64,
                "receiptsRoot": "0x" + "0" * 64,
                "miner": "0x" + "0" * 40,
                "difficulty": "0x1",
                "totalDifficulty": "0x1",
                "extraData": "0x",
                "size": "0x0",
                "gasLimit": "0x1c9c380",
                "gasUsed": "0x0",
                "timestamp": hex(int(time.time())),
                "transactions": [],
                "uncles": []
            }

        if method == "eth_gasPrice":
            return hex(1_000_000_000)

        if method == "eth_maxPriorityFeePerGas":
            return hex(100_000_000)

        if method == "eth_feeHistory":
            return {
                "oldestBlock": "0x0",
                "baseFeePerGas": [hex(1_000_000_000), hex(1_000_000_000)],
                "gasUsedRatio": [0.0],
                "reward": [[hex(100_000_000)]]
            }

        if method == "eth_getBalance":
            return hex(0)

        if method == "eth_accounts":
            return []

        if method == "eth_requestAccounts":
            return []

        if method == "eth_estimateGas":
            return hex(21000)

        if method == "eth_getTransactionCount":
            return hex(0)

        if method == "eth_sendRawTransaction":
            raise Exception("eth_sendRawTransaction disabled in secure mode")

        if False and method == "eth_sendRawTransaction":
            raw = params[0] if params else ""
            if not isinstance(raw, str) or not raw.startswith("0x"):
                raise Exception("invalid raw transaction")

            # Bridge encoding: 0x + UTF-8 hex of
            # from|pub|priv|to|amount|fee|nonce
            try:
                payload = bytes.fromhex(raw[2:]).decode("utf-8")
            except Exception:
                raise Exception("unsupported raw tx encoding for bridge")

            fields = payload.split("|")
            if len(fields) != 7:
                raise Exception("raw tx bridge payload must have 7 fields")

            cmd = "sendtx_hash " + " ".join(fields)
            r = core_rpc(cmd)
            if r.startswith("error:"):
                raise Exception(r)
            return "0x" + r

        if method == "eth_getTransactionReceipt":
            txh = params[0] if params else ""
            if isinstance(txh, str) and txh.startswith("0x"):
                txh = txh[2:]
            if not txh:
                return None

            r = core_rpc(f"tx_status {txh}")
            if r.startswith("status=unknown") or r.startswith("error:"):
                return None

            data = {}
            for item in r.split():
                if "=" in item:
                    k, v = item.split("=", 1)
                    data[k] = v

            if data.get("status") == "mempool":
                return None

            if data.get("status") == "mined":
                h = "0x" + txh
                bh = "0x" + "0" * 64
                return {
                    "transactionHash": h,
                    "transactionIndex": hex(int(data.get("tx_index", "0"))),
                    "blockHash": bh,
                    "blockNumber": hex(int(data.get("block_height", "0"))),
                    "from": "0x" + "0" * 40,
                    "to": "0x" + "0" * 40,
                    "cumulativeGasUsed": hex(21000),
                    "gasUsed": hex(21000),
                    "contractAddress": None,
                    "logs": [],
                    "logsBloom": "0x" + "0" * 512,
                    "status": "0x1",
                    "effectiveGasPrice": hex(1_000_000_000),
                }
            return None

        if method == "eth_getTransactionByHash":
            txh = params[0] if params else ""
            if isinstance(txh, str) and txh.startswith("0x"):
                txh = txh[2:]
            if not txh:
                return None

            r = core_rpc(f"tx_status {txh}")
            if r.startswith("status=unknown") or r.startswith("error:"):
                return None

            data = {}
            for item in r.split():
                if "=" in item:
                    k, v = item.split("=", 1)
                    data[k] = v

            return {
                "hash": "0x" + txh,
                "nonce": "0x0",
                "blockHash": "0x" + "0" * 64,
                "blockNumber": hex(int(data.get("block_height", "0"))) if data.get("status") == "mined" else None,
                "transactionIndex": hex(int(data.get("tx_index", "0"))) if data.get("status") == "mined" else None,
                "from": "0x" + "0" * 40,
                "to": "0x" + "0" * 40,
                "value": "0x0",
                "gas": hex(21000),
                "gasPrice": hex(1_000_000_000),
                "input": "0x",
                "chainId": CHAIN_ID_HEX,
            }

        if method == "eth_getCode":
            return "0x"

        if method == "eth_call":
            return "0x"

        if method == "wallet_addEthereumChain":
            return None

        if method == "wallet_switchEthereumChain":
            return None

        if method == "eth_syncing":
            return False

        raise Exception(f"Method not implemented: {method}")

    def log_message(self, format, *args):
        return


if __name__ == "__main__":
    print(f"Addition EVM bridge running on {RPC_URL}")
    print("Mode: STRICT (no tx simulation)")
    print("Use in MetaMask custom network:")
    print(f"- Network Name: {NETWORK_NAME}")
    print(f"- RPC URL: {RPC_URL}")
    print(f"- Chain ID: {int(CHAIN_ID_HEX, 16)}")
    print(f"- Currency Symbol: {NATIVE_SYMBOL}")
    print(f"- Block Explorer URL: {EXPLORER_URL}")
    HTTPServer((HOST, PORT), Handler).serve_forever()
