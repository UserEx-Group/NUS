# NUS Native Networking — Milestone 15

Milestone 15 introduces the first native networking layer in NUS. The API is intentionally small and synchronous; its purpose is to establish safe ownership, ABI and error semantics before asynchronous I/O and higher-level protocols are added.

## Built-in types

```nus
IpAddr
SocketAddr
NetworkError
TcpStream
UdpSocket
```

`IpAddr`, `SocketAddr` and `NetworkError` are `Copy`. `TcpStream` and `UdpSocket` are owning, non-`Copy` resources. Moving either socket transfers the operating-system handle. Dropping the final owner closes it.

On 64-bit targets the bootstrap ABI is:

```text
IpAddr       size 4  align 4
SocketAddr   size 8  align 4
NetworkError size 4  align 4
TcpStream    size 8  align 8
UdpSocket    size 8  align 8
```

The language representation is independent of the platform's `SOCKET`/file-descriptor width. Native handles are normalized to a 64-bit field in the NUS ABI and converted at the OS boundary.

## Addresses

IPv4 is the only address family in this milestone:

```nus
let ip = ipv4(127, 0, 0, 1);
let address = socket_addr(ip, 8080);
print(socket_port(&address));
```

`IpAddr` stores the IPv4 value in host order. `SocketAddr` stores the port in host order. `htonl`/`htons` are applied only when an OS `sockaddr_in` is constructed.

IPv6, DNS resolution and hostname types are deferred.

## TCP

```nus
fn connect(address: &SocketAddr) -> Result<TcpStream, NetworkError> {
    tcp_connect(address)
}
```

The stream is an owned resource and composes with `?`:

```nus
fn connect_forward(address: &SocketAddr) -> Result<TcpStream, NetworkError> {
    let stream = tcp_connect(address)?;
    ok::<TcpStream, NetworkError>(stream)
}
```

Binary payloads use `Vec<u8>`:

```nus
fn transmit(
    stream: &mut TcpStream,
    buffer: &Vec<u8>
) -> Result<usize, NetworkError> {
    tcp_send(stream, buffer)
}

fn receive(
    stream: &mut TcpStream
) -> Result<Vec<u8>, NetworkError> {
    tcp_receive(stream, 4096)
}
```

`tcp_send` reports the number of bytes actually accepted by the OS; partial sends are therefore visible to user code. `tcp_receive` allocates an owned `Vec<u8>` with capacity equal to the requested maximum and length equal to the number of bytes received. A zero-length receive represents EOF, not an error.

## UDP

A socket is created and bound with:

```nus
let local = socket_addr(ipv4(127, 0, 0, 1), 0);
let socket = udp_bind(&local);
```

Port `0` delegates ephemeral-port selection to the OS.

Datagrams use the same binary buffer type:

```nus
fn transmit(
    socket: &mut UdpSocket,
    remote: &SocketAddr,
    packet: &Vec<u8>
) -> Result<usize, NetworkError> {
    udp_send_to(socket, remote, packet)
}

fn receive(
    socket: &mut UdpSocket
) -> Result<Vec<u8>, NetworkError> {
    udp_receive(socket, 2048)
}
```

The bootstrap `udp_receive` discards sender-address metadata. A future `recv_from` API will return both payload and peer address.

## Stable NUS error codes

`NetworkError` currently wraps a small NUS-defined code rather than exposing platform-specific `errno` or Winsock values:

```text
1  networking runtime initialization failed
2  socket creation failed
3  TCP connect failed
4  bind failed
5  send/sendto failed
6  receive failed
```

The code can be inspected with:

```nus
network_error_code(&error)
```

This keeps programs portable between POSIX and Winsock. Rich error variants and optional native error details are deferred.

## Resource safety

`TcpStream` and `UdpSocket` are non-`Copy`:

```text
socket owner
   │ move
   ▼
new owner
```

Drop glue closes only the live owner. Sockets inside `Result`, enums, structs, vectors or other owned aggregates are recursively closed through the existing drop system.

On POSIX, drop calls `close`. On Windows/MinGW-w64, it calls `closesocket`.

## Platform lowering

POSIX targets lower to:

```text
socket
connect
bind
send
sendto
recv
close
htonl
htons
```

Windows GNU targets lower to Winsock:

```text
WSAStartup
socket
connect
bind
send
sendto
recv
closesocket
htonl
htons
```

`NativeToolchain` automatically links `ws2_32` for Windows targets. NUS remains MSVC-free; the supported Windows path is LLVM/Clang + MinGW-w64.

Winsock initialization is process-scoped and performed lazily once by the generated `nus_net_init` helper.

## Current boundaries

Milestone 15 is synchronous and IPv4-only. Deferred work includes IPv6, DNS, `TcpListener`, accept, socket options, timeouts, non-blocking mode, peer/local address queries, UDP sender metadata, async I/O, polling/reactors, TLS, higher-level protocols and richer OS error information.

These are intentionally deferred until the resource and error semantics introduced here have been proven stable.
