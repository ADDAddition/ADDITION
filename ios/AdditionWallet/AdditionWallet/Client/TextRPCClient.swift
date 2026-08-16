import Darwin
import Foundation

protocol RPCTransport: Sendable {
    func send(_ wire: String) throws -> String
}

struct TextRPCClient: Sendable {
    let endpoint: WriteEndpoint
    let timeout: TimeInterval
    let transport: RPCTransport?

    init(endpoint: WriteEndpoint, timeout: TimeInterval = 8, transport: RPCTransport? = nil) throws {
        _ = try WriteRPCPolicy.assertWriteEndpoint(endpoint.host)
        self.endpoint = endpoint
        self.timeout = timeout
        self.transport = transport
    }

    func call(_ command: String, write: Bool = true) throws -> String {
        _ = try WriteRPCPolicy.assertCommand(command, write: write)
        let wire = endpoint.token.isEmpty ? command : "\(endpoint.token) \(command)"
        if wire.count > WriteRPCPolicy.maxRPCLine {
            throw AdditionError.rpc("RPC command exceeds 32768-byte TEXT RPC limit")
        }
        let reply: String
        if let transport {
            do {
                reply = try transport.send(wire)
            } catch let error as AdditionError {
                throw error
            } catch {
                throw AdditionError.rpcOffline
            }
        } else if endpoint.scheme == "http" || endpoint.scheme == "https" {
            reply = try HTTPReadRPC.get(command: command, url: "\(endpoint.scheme)://\(endpoint.host):\(endpoint.port)/rpc", timeout: timeout, write: true)
        } else {
            reply = try SocketTransport.send(wire: wire, host: endpoint.host, port: endpoint.port, timeout: timeout)
        }
        let text = reply.trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty || text == "RPC offline" {
            throw AdditionError.rpcOffline
        }
        return text
    }
}

enum SocketTransport {
    static func send(wire: String, host: String, port: Int, timeout: TimeInterval) throws -> String {
        var hints = addrinfo(
            ai_flags: AI_NUMERICSERV,
            ai_family: AF_UNSPEC,
            ai_socktype: SOCK_STREAM,
            ai_protocol: IPPROTO_TCP,
            ai_addrlen: 0,
            ai_canonname: nil,
            ai_addr: nil,
            ai_next: nil
        )
        var info: UnsafeMutablePointer<addrinfo>?
        let portText = String(port)
        let resolve = portText.withCString { portPtr in
            host.withCString { hostPtr in
                getaddrinfo(hostPtr, portPtr, &hints, &info)
            }
        }
        guard resolve == 0, let first = info else {
            throw AdditionError.rpcOffline
        }
        defer { freeaddrinfo(first) }

        var socketFD: Int32 = -1
        var cursor: UnsafeMutablePointer<addrinfo>? = first
        while let current = cursor {
            let fd = Darwin.socket(current.pointee.ai_family, current.pointee.ai_socktype, current.pointee.ai_protocol)
            if fd >= 0 {
                var tv = timeval(tv_sec: Int(timeout), tv_usec: 0)
                Darwin.setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, socklen_t(MemoryLayout<timeval>.size))
                Darwin.setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, socklen_t(MemoryLayout<timeval>.size))
                if Darwin.connect(fd, current.pointee.ai_addr, current.pointee.ai_addrlen) == 0 {
                    socketFD = fd
                    break
                }
                Darwin.close(fd)
            }
            cursor = current.pointee.ai_next
        }
        guard socketFD >= 0 else {
            throw AdditionError.rpcOffline
        }
        defer { Darwin.close(socketFD) }
        var payload = Array(wire.utf8)
        payload.append(0x0A)
        let sent = payload.withUnsafeBytes { buffer -> Int in
            guard let base = buffer.baseAddress else { return -1 }
            return Darwin.send(socketFD, base, buffer.count, 0)
        }
        if sent < 0 {
            throw AdditionError.rpcOffline
        }
        var chunks = [UInt8]()
        var buf = [UInt8](repeating: 0, count: 4096)
        while true {
            let n = Darwin.recv(socketFD, &buf, buf.count, 0)
            if n <= 0 {
                break
            }
            chunks.append(contentsOf: buf[0..<n])
            if chunks.contains(0x0A) {
                break
            }
            if chunks.count > WriteRPCPolicy.maxRPCLine {
                break
            }
        }
        guard let line = String(bytes: chunks, encoding: .utf8) else {
            throw AdditionError.rpcOffline
        }
        return line.split(whereSeparator: { $0.isNewline }).first.map(String.init) ?? ""
    }
}

enum HTTPReadRPC {
    static func get(command: String, url: String, timeout: TimeInterval, write: Bool) throws -> String {
        _ = try WriteRPCPolicy.assertCommand(command, write: write)
        if !write && !WriteRPCPolicy.isKnownPublicReadEndpoint(url) {
            throw AdditionError.publicReadRefused
        }
        if write {
            _ = try WriteRPCPolicy.assertWriteEndpoint(url)
        }
        var components = URLComponents(string: url)
        if components?.queryItems == nil {
            components?.queryItems = []
        }
        var items = components?.queryItems ?? []
        items.removeAll { $0.name == "cmd" }
        items.append(URLQueryItem(name: "cmd", value: command))
        components?.queryItems = items
        guard let target = components?.url else {
            throw AdditionError.rpcOffline
        }
        var request = URLRequest(url: target, timeoutInterval: timeout)
        request.httpMethod = "GET"
        request.cachePolicy = .reloadIgnoringLocalCacheData
        let semaphore = DispatchSemaphore(value: 0)
        var result: Result<String, Error> = .failure(AdditionError.rpcOffline)
        let task = URLSession.shared.dataTask(with: request) { data, _, error in
            if error != nil {
                result = .failure(AdditionError.rpcOffline)
            } else if let data, let body = String(data: data, encoding: .utf8) {
                result = .success(body)
            } else {
                result = .failure(AdditionError.rpcOffline)
            }
            semaphore.signal()
        }
        task.resume()
        _ = semaphore.wait(timeout: .now() + timeout + 1)
        switch result {
        case .success(let body):
            let text = body.trimmingCharacters(in: .whitespacesAndNewlines)
            if text.isEmpty || text == "RPC offline" {
                throw AdditionError.rpcOffline
            }
            return text
        case .failure:
            throw AdditionError.rpcOffline
        }
    }
}
