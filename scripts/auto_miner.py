import json
import time
import urllib.request

def mine():
    url = "http://127.0.0.1:8545"
    # Mining to your address: 2748817dadca12c1a5e411b0bc513b5e4a4f0218
    payload = {
        "jsonrpc": "2.0",
        "method": "mine",
        "params": ["2748817dadca12c1a5e411b0bc513b5e4a4f0218"],
        "id": 1
    }
    headers = {'Content-Type': 'application/json'}
    
    print("========================================")
    print("   ADDITION AUTO-MINER STARTED")
    print("   Target: 2748817dadca12c1a5e411b0bc513b5e4a4f0218")
    print("========================================")

    while True:
        try:
            req = urllib.request.Request(url, data=json.dumps(payload).encode('utf-8'), headers=headers)
            with urllib.request.urlopen(req, timeout=10) as response:
                resp_data = json.loads(response.read().decode())
                if "result" in resp_data:
                    print(f"[{time.strftime('%H:%M:%S')}] Success: {resp_data['result']}")
                else:
                    print(f"[{time.strftime('%H:%M:%S')}] RPC Error: {resp_data.get('error')}")
        except Exception as e:
            print(f"[{time.strftime('%H:%M:%S')}] Connection error: {e} (Is the daemon running?)")
        
        time.sleep(1)

if __name__ == "__main__":
    mine()
