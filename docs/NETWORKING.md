# NUS Native Networking — Milestone 16

Milestone 16 extends the synchronous native networking layer with TCP servers, IPv4 DNS resolution, portable error reasons and basic socket configuration. The implementation remains ownership-aware, IPv4-first and MSVC-free on Windows.

## Built-in networking types

```nus
IpAddr
SocketAddr
NetworkError
TcpStream
TcpListener
UdpSocket
```

`IpAddr`, `SocketAddr` and `NetworkError` are `Copy`. `TcpStream`, `TcpListener` and `UdpSocket` own operating-system handles and are non-`Copy`; moving them transfers ownership and dropping the final owner closes the handle.

On 64-bit targets the bootstrap ABI is:

```text
IpAddr       size 4  align 4
SocketAddr   size 8  align 4
NetworkError size 8  align 4
TcpStream    size 8  align 8
TcpListener  size 8  align 8
UdpSocket    size 8  align 8
```

## DNS

The first resolver is deliberately IPv4-only and returns the first address reported by the host resolver:

```nus
fn resolve(host: &string) -> Result<IpAddr, NetworkError> {
    dns_resolve_ipv4(host)
}
```

The bootstrap backend uses the native host resolver (`gethostbyname`) so DNS works without a NUS runtime daemon or external dependency. The public result stays a portable `IpAddr`; platform-specific resolver structures never enter the language ABI.

## TCP clients

Milestone 15 client APIs remain available:

```nus
fn connect(address: &SocketAddr) -> Result<TcpStream, NetworkError> {
    tcp_connect(address)
}

fn transmit(stream: &mut TcpStream, data: &Vec<u8>) -> Result<usize, NetworkError> {
    tcp_send(stream, data)
}

fn receive(stream: &mut TcpStream) -> Result<Vec<u8>, NetworkError> {
    tcp_receive(stream, 4096)
}
```

A zero-length receive is EOF rather than an error.

## TCP listeners and accept

A listener is created with an address, backlog and `SO_REUSEADDR` policy:

```nus
fn serve(address: &SocketAddr) -> Result<TcpListener, NetworkError> {
    tcp_listen(address, 128, true)
}
```

The lowering sequence is:

```text
socket
  ↓
setsockopt(SO_REUSEADDR)
  ↓
bind
  ↓
listen
  ↓
TcpListener owner
```

Accepting a client requires exclusive access to the listener:

```nus
fn accept_one(listener: &mut TcpListener) -> Result<TcpStream, NetworkError> {
    tcp_accept(listener)
}
```

The accepted stream is a new owned `TcpStream`. The listener remains alive and can accept additional connections.

## Socket options

TCP streams can configure Nagle behavior:

```nus
let _ = tcp_set_nodelay(&mut stream, true)?;
```

and combined read/write timeout in milliseconds:

```nus
let _ = tcp_set_timeout(&mut stream, 1500u32)?;
```

The bootstrap functions return `Result<i32, NetworkError>` with `0` as the success payload. This is temporary until `Result<unit, E>` has a dedicated native representation.

`tcp_set_timeout` maps milliseconds to the platform ABI: Winsock receives a millisecond integer; POSIX receives a `timeval` and configures both `SO_RCVTIMEO` and `SO_SNDTIMEO`.

## UDP

The M15 UDP API remains unchanged:

```nus
udp_bind(&local)
udp_send_to(&mut socket, &remote, &bytes)
udp_receive(&mut socket, 2048)
```

Payloads remain `Vec<u8>` rather than `string` so binary protocols preserve arbitrary bytes and embedded zeroes.

## Portable NetworkError

`NetworkError` is now two 32-bit fields internally:

```text
{ operation_code, reason_code }
```

The operation code identifies which NUS networking operation failed:

```text
1   networking runtime initialization
2   socket creation
3   connect
4   bind
5   send/sendto
6   receive
7   listen
8   accept
9   DNS resolution
10  socket option
```

The reason code is portable across POSIX and Winsock:

```text
0  unknown / uncategorized
1  timeout / would-block timeout
2  connection refused
3  address already in use
4  host not found
```

Inspect them with:

```nus
network_error_code(&error)
network_error_reason(&error)
```

NUS reads `errno` or `WSAGetLastError` internally only long enough to map common failures. Native OS error numbers are not exposed in the language ABI, so code does not need platform-specific constants.

## Windows and POSIX lowering

POSIX lowering uses libc socket APIs including `socket`, `bind`, `listen`, `accept`, `setsockopt`, `connect`, `send`, `sendto`, `recv`, `close`, `gethostbyname`, `htonl`, `htons` and `ntohl`.

Windows GNU lowering uses Winsock equivalents plus `WSAStartup`, `WSAGetLastError` and `closesocket`. `NativeToolchain` automatically links `ws2_32`; the supported Windows path remains LLVM/Clang + MinGW-w64 with no MSVC dependency.

## Current boundaries

Milestone 16 is still synchronous and IPv4-only. The DNS bootstrap returns only the first IPv4 result and uses the legacy host resolver; a future resolver layer will move to a richer address-list API. Also deferred are IPv6, peer/local endpoint queries, UDP sender metadata, half-close/shutdown, non-blocking mode, polling/reactors, async/await networking, TLS and higher-level protocols.

## Milestone 17 non-blocking mode and reactor

Sockets can now opt into non-blocking mode with `tcp_set_nonblocking`, `tcp_listener_set_nonblocking` and `udp_set_nonblocking`. POSIX uses `fcntl`/`O_NONBLOCK`; Windows uses `ioctlsocket(FIONBIO)`.

`Reactor` is an owned runtime value that stores registrations and waits for readiness with `poll()` on POSIX or `WSAPoll()` on Windows. `reactor_register_tcp`, `reactor_register_listener` and `reactor_register_udp` attach a user token and desired read/write interests; `reactor_wait` fills readiness flags queried through the `reactor_event_*` functions.

A registered resource is protected by an internal NLL resource pin. The pin prevents move/drop of the socket owner while the Reactor can reference its OS handle but intentionally allows `&mut` I/O after readiness. Reactor/poll failures use network operation code `11`.

## Milestone 18 async TCP operations

The first socket futures are `tcp_accept_async(&mut TcpListener)`, `tcp_receive_async(&mut TcpStream, usize)`, and `tcp_send_async(&mut TcpStream, &Vec<u8>)`. They force the underlying socket into non-blocking mode and return a suspendable `Future<Result<...>>`. A would-block result is represented as `Pending` and records readable/writable interest in the Future frame; `block_on` or a parent async function waits with the platform poller before retrying. Socket/buffer arguments remain borrowed until the Future's last live use.
