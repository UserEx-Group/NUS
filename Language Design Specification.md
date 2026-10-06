# Language Design Specification v0.1

**Status:** Draft  
**Versão:** 0.1  
**Objetivo:** definir o núcleo semântico e sintático necessário para iniciar a implementação do compilador.

---

# 1. Princípio fundamental

A linguagem é uma **Network-Oriented Systems Programming Language** voltada principalmente para:

- programação de sistemas;
- networking;
- serviços de rede;
- concorrência;
- sistemas distribuídos;
- simulação;
- ferramentas de infraestrutura;
- software próximo ao sistema operacional.

Seu princípio central é:

> **High-level by default. Low-level by choice. Safe by default.**

A linguagem deve oferecer abstrações de alto nível sem impedir controle de baixo nível.

---

# 2. Modelo de execução

A linguagem será primariamente:

**AOT — Ahead-of-Time compiled**

Fluxo:

```text
Source
  ↓
Lexer
  ↓
Parser
  ↓
AST
  ↓
Semantic Analysis
  ↓
HIR
  ↓
MIR
  ↓
Optimization
  ↓
Backend
  ↓
Native executable
```

Backends previstos:

```text
LLVM          prioridade inicial
WebAssembly   futuro
Cranelift     possível
Backend own   experimental/futuro
```

A arquitetura do compilador não deverá depender conceitualmente do LLVM.

---

# 3. Implementação inicial do compilador

O compilador bootstrap será escrito em:

**C++20**

Motivos:

- controle de memória;
- boa interoperabilidade com LLVM;
- performance;
- familiaridade com sistemas;
- possibilidade de compartilhar componentes com projetos existentes;
- facilidade para construir lexer, parser e estruturas intermediárias.

Estrutura prevista:

```text
compiler/
├── lexer/
├── parser/
├── ast/
├── semantic/
├── hir/
├── mir/
├── codegen/
├── diagnostics/
└── driver/
```

---

# 4. Extensão de arquivos

Enquanto a linguagem não possuir nome definitivo:

```text
.ux
```

será usada provisoriamente.

Exemplo:

```text
main.ux
network.ux
packet.ux
```

---

# 5. Sintaxe geral

A linguagem utilizará:

- `{}` para blocos;
- `()` para chamadas;
- `[]` para indexação;
- `<>` para generics;
- `:` para tipos;
- `;` para terminar statements;
- `//` para comentários de linha;
- `/* */` para comentários de bloco.

Exemplo:

```text
fn main() {
    let port = 8080;

    print("Listening on", port);
}
```

---

# 6. Declaração de variáveis

Valores são imutáveis por padrão.

```text
let port = 8080;
```

Variáveis mutáveis exigem:

```text
let mut attempts = 0;
```

Tipos podem ser explícitos:

```text
let port: u16 = 8080;
```

ou inferidos:

```text
let port = 8080;
```

---

# 7. Tipos primitivos

## Inteiros

```text
i8
i16
i32
i64
isize

u8
u16
u32
u64
usize
```

Inicialmente não teremos `i128`/`u128` como requisito obrigatório.

Poderão ser adicionados posteriormente.

---

# 8. Floating point

```text
f32
f64
```

O padrão para literais decimais será:

```text
f64
```

quando não houver contexto suficiente para inferência.

---

# 9. Outros tipos fundamentais

```text
bool
char
string
bytes
unit
never
```

Exemplos:

```text
let enabled: bool = true;
let name: string = "router";
let raw: bytes = b"HTTP";
```

---

# 10. Strings

Strings serão:

- UTF-8;
- imutáveis por padrão;
- length-aware;
- nunca terminadas implicitamente em `\0`.

```text
let name = "router";
```

Uma string não deve ser automaticamente equivalente a `char*`.

Interoperação com C exigirá conversão explícita.

---

# 11. Arrays

Arrays estáticos:

```text
let bytes: [u8; 4] = [192, 168, 0, 1];
```

O tamanho faz parte do tipo:

```text
[u8; 4]
```

é diferente de:

```text
[u8; 16]
```

---

# 12. Slices

Visualizações sobre sequências usarão:

```text
[u8]
```

Exemplo conceitual:

```text
fn checksum(data: [u8]) -> u32 {
    ...
}
```

Slices carregam:

```text
pointer
length
```

e realizam bounds checking por padrão.

---

# 13. Tuples

```text
let connection = ("localhost", 8080);
```

Tipos:

```text
(string, u16)
```

Acesso inicialmente:

```text
connection.0
connection.1
```

---

# 14. Estruturas

```text
struct Packet {
    source: Address;
    destination: Address;
    payload: Bytes;
}
```

Construção:

```text
let packet = Packet {
    source: src,
    destination: dst,
    payload: data,
};
```

---

# 15. Métodos

Métodos podem ser associados a tipos.

```text
impl Packet {
    fn size(self) -> usize {
        return self.payload.len();
    }
}
```

Uso:

```text
packet.size();
```

---

# 16. Herança

Não existirá herança tradicional de classes.

Não haverá:

```text
class Dog extends Animal
```

como mecanismo fundamental.

A linguagem favorecerá:

```text
composition
+
traits
+
generics
```

---

# 17. Traits

Interfaces comportamentais serão representadas por `trait`.

```text
trait Serializable {
    fn serialize(self) -> Bytes;
}
```

Implementação:

```text
impl Serializable for Packet {
    fn serialize(self) -> Bytes {
        ...
    }
}
```

Traits não armazenam estado diretamente.

---

# 18. Generics

```text
fn first<T>(values: [T]) -> T? {
    ...
}
```

Tipos genéricos:

```text
struct Queue<T> {
    ...
}
```

Generics deverão ser preferencialmente monomorfizados.

Assim:

```text
Queue<Packet>
```

pode gerar código especializado em compile-time.

---

# 19. Optional values

`null` não fará parte de qualquer referência automaticamente.

Usaremos:

```text
T?
```

como açúcar sintático para:

```text
Option<T>
```

Exemplo:

```text
let client: Client? = findClient(id);
```

Estados possíveis:

```text
Some(client)
None
```

---

# 20. Pattern matching

```text
match client {
    Some(client) => client.send(message),
    None => log("Client not found"),
}
```

`match` deverá ser exaustivo.

O compilador detectará casos ausentes.

---

# 21. Enums

Enums podem armazenar dados.

```text
enum Packet {
    TCP(TcpPacket),
    UDP(UdpPacket),
    ICMP(IcmpPacket),
}
```

Uso:

```text
match packet {
    TCP(packet) => handleTcp(packet),
    UDP(packet) => handleUdp(packet),
    ICMP(packet) => handleIcmp(packet),
}
```

---

# 22. Funções

```text
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
```

Funções sem retorno explícito:

```text
fn logPacket(packet: Packet) {
    print(packet);
}
```

equivalem a:

```text
fn logPacket(packet: Packet) -> unit
```

---

# 23. Functions as values

Funções poderão ser armazenadas e passadas:

```text
let handler = processPacket;

run(handler);
```

Closures serão suportadas:

```text
let double = |x| x * 2;
```

A implementação completa de closures poderá ocorrer depois do MVP.

---

# 24. Controle de fluxo

## If

```text
if latency > 100ms {
    warn("High latency");
} else {
    log("OK");
}
```

---

# 25. If como expressão

```text
let state = if connected {
    "online"
} else {
    "offline"
};
```

Isso evita operadores ternários especiais.

---

# 26. While

```text
while server.running() {
    server.poll();
}
```

---

# 27. Loop

Loop infinito:

```text
loop {
    process();
}
```

---

# 28. For

```text
for packet in packets {
    process(packet);
}
```

Ranges:

```text
for i in 0..10 {
    print(i);
}
```

Intervalo inclusivo:

```text
0..=10
```

---

# 29. Break e continue

```text
break;
continue;
```

`break` poderá eventualmente retornar valor:

```text
let result = loop {
    if ready {
        break value;
    }
};
```

---

# 30. Erros

Falhas esperadas serão representadas por:

```text
Result<T, E>
```

Exemplo:

```text
fn connect(address: Address)
    -> Result<Connection, NetworkError>
{
    ...
}
```

---

# 31. Propagação de erro

Operador:

```text
?
```

Exemplo:

```text
fn start() -> Result<Server, Error> {
    let socket = Socket.open()?;
    socket.bind(8080)?;

    return Ok(Server(socket));
}
```

---

# 32. Panic

`panic` existirá para situações onde continuar a execução não é razoável.

```text
panic("Impossible state");
```

Não deverá substituir tratamento normal de erros.

---

# 33. Exceções

A v0.1 **não terá exception handling tradicional**.

Não teremos inicialmente:

```text
try
catch
throw
```

Erros recuperáveis deverão utilizar `Result`.

---

# 34. Overflow

Overflow silencioso será proibido por padrão.

```text
let x: u8 = 255;
x + 1;
```

deve falhar de forma controlada.

Operações explícitas estarão disponíveis:

```text
x.wrapping_add(1);
x.saturating_add(1);
x.checked_add(1);
```

Isso reduz uma categoria importante de falhas em software de sistemas e networking.

---

# 35. Conversões numéricas

Conversões potencialmente perigosas serão explícitas.

Não:

```text
let small: u8 = large;
```

mas:

```text
let small = large as u8;
```

Conversões verificadas poderão usar:

```text
u8.try_from(large)?;
```

---

# 36. Memória — objetivo

O gerenciamento de memória deve oferecer:

- segurança;
- destruição determinística;
- ausência de GC global obrigatório;
- performance previsível;
- sintaxe mais simples que Rust.

A proposta inicial será baseada em:

**scope ownership + move semantics + borrowing inferido.**

---

# 37. Ownership

Todo recurso possui um proprietário.

```text
let packet = Packet(...);
```

`packet` controla seu recurso.

Quando sai de escopo:

```text
{
    let packet = Packet(...);
}
```

seus recursos são liberados automaticamente.

---

# 38. Move semantics

Objetos que possuem recursos são movidos por padrão.

```text
let packet = createPacket();

send(packet);
```

Se `send` consumir o pacote:

```text
packet
```

não poderá ser utilizado posteriormente.

O compilador detectará isso.

---

# 39. Copy types

Tipos pequenos poderão implementar:

```text
Copy
```

Exemplos:

```text
u32
bool
AddressV4
```

Copiá-los não transfere ownership.

---

# 40. Borrowing

Funções poderão receber uma referência sem assumir ownership.

Sintaxe provisória:

```text
fn inspect(packet: &Packet) {
    ...
}
```

Referência mutável:

```text
fn update(packet: &mut Packet) {
    ...
}
```

---

# 41. Regra de borrowing

Em determinado momento deverá existir:

```text
qualquer quantidade de &T
```

ou:

```text
uma única &mut T
```

mas nunca ambos simultaneamente.

Isso elimina grande parte dos data races e invalid memory accesses.

---

# 42. Lifetimes

Um objetivo explícito é:

> **Usuários comuns não devem escrever lifetime annotations.**

O compilador deverá inferir os lifetimes na grande maioria dos casos.

A v0.1 não terá inicialmente sintaxe equivalente a:

```text
'a
'b
```

do Rust em código comum.

Se encontrarmos casos impossíveis de expressar sem isso, o design será reconsiderado.

---

# 43. Heap

Alocação explícita:

```text
Box<T>
```

Exemplo:

```text
let node = Box.new(Node(...));
```

Containers poderão utilizar heap internamente:

```text
Vec<T>
Map<K, V>
String
```

---

# 44. Shared ownership

Ownership compartilhado será explícito.

Single-thread:

```text
Rc<T>
```

Multi-thread:

```text
Arc<T>
```

Não haverá referência contada escondida para qualquer objeto.

---

# 45. Destruição determinística

Recursos serão destruídos ao saírem do escopo.

Isso deve funcionar naturalmente para:

- memória;
- sockets;
- arquivos;
- locks;
- handles;
- processos.

---

# 46. Drop

Tipos poderão definir comportamento de destruição:

```text
impl Drop for Connection {
    fn drop(&mut self) {
        self.close();
    }
}
```

---

# 47. Defer

Também teremos:

```text
defer file.close();
```

Exemplo:

```text
let file = File.open(path)?;
defer file.close();
```

`defer` será executado ao deixar o escopo.

Ele não deve substituir RAII, mas complementá-lo.

---

# 48. Unsafe

Operações que não podem ser comprovadas seguras pelo compilador exigirão:

```text
unsafe {
    ...
}
```

Exemplo:

```text
unsafe {
    let ptr = memory.alloc<u8>(1024);
}
```

---

# 49. Dentro de unsafe

Poderão existir:

- raw pointers;
- syscalls;
- FFI unsafe;
- acesso arbitrário a memória;
- operações de hardware;
- casts não verificados.

`unsafe` não desliga todas as verificações da linguagem.

Ele apenas permite determinadas operações privilegiadas.

---

# 50. Raw pointers

Sintaxe provisória:

```text
*u8
*mut u8
```

Eles não oferecem:

- lifetime checking;
- null safety automática;
- bounds checking.

Portanto só podem ser manipulados diretamente em contexto `unsafe`.

---

# 51. Concorrência

Concorrência é um conceito fundamental.

As principais abstrações serão:

```text
task
spawn
await
scope
channel
parallel
```

---

# 52. Async functions

```text
async fn fetch() -> Packet {
    ...
}
```

Uso:

```text
let packet = await fetch();
```

Internamente, `async fn` gera uma state machine.

---

# 53. Spawn

```text
spawn monitor();
```

Uma task deverá pertencer a um escopo estruturado, salvo uso explicitamente detached.

---

# 54. Structured concurrency

```text
scope {
    spawn collect();
    spawn monitor();
    spawn serve();
}
```

Ao deixar o escopo, todas as tasks precisam estar:

```text
completed
cancelled
joined
```

Nenhuma task é silenciosamente esquecida.

---

# 55. Parallel

Para paralelismo explícito:

```text
parallel {
    analyzeA();
    analyzeB();
    analyzeC();
}
```

O bloco só termina quando suas operações terminarem.

---

# 56. Channels

```text
let packets = channel<Packet>();

spawn capture(packets.sender());
spawn analyze(packets.receiver());
```

Channels deverão ser uma das formas preferenciais de comunicação concorrente.

---

# 57. Compartilhamento entre threads

Uma task executada em outra thread somente poderá capturar valores seguros para transferência.

Conceitualmente teremos capacidades equivalentes a:

```text
Send
Sync
```

Os nomes definitivos poderão mudar.

O compilador deverá verificar isso automaticamente.

---

# 58. Mutexes

Estado compartilhado continuará disponível:

```text
let state = Mutex.new(State());
```

Uso:

```text
let guard = state.lock();
```

O lock deve ser liberado determinísticamente.

---

# 59. Atomics

Tipos:

```text
AtomicBool
AtomicU32
AtomicU64
```

e outros necessários.

Memory ordering deverá ser explícito em APIs avançadas.

---

# 60. Networking

Networking será um componente oficial e fundamental.

Porém, uma decisão importante da v0.1 é:

> `network`, `service`, `node` e `link` ainda não serão keywords obrigatórias do núcleo.

Primeiro construiremos APIs sólidas.

Depois poderemos adicionar sintaxe específica sobre essas abstrações.

Isso evita congelar uma DSL ruim cedo demais.

---

# 61. Tipos de rede

A biblioteca oficial deverá possuir:

```text
IpAddress
Ipv4Address
Ipv6Address
MacAddress
Port
SocketAddress
TcpSocket
TcpListener
TcpStream
UdpSocket
Interface
Packet
```

---

# 62. TCP

Exemplo de API inicial:

```text
let listener = TcpListener.bind(":8080")?;

loop {
    let connection = listener.accept()?;

    spawn handle(connection);
}
```

---

# 63. Async networking

```text
let listener = await TcpListener.bind(":8080")?;

loop {
    let connection = await listener.accept()?;

    spawn handle(connection);
}
```

O runtime deverá utilizar mecanismos adequados ao sistema operacional.

Exemplos:

```text
epoll
io_uring
kqueue
IOCP
```

conforme a plataforma.

---

# 64. Protocolos

Protocolos de alto nível serão módulos independentes.

Exemplo:

```text
use net.http;
use net.mqtt;
```

Eles não devem inflar executáveis que não os utilizem.

---

# 65. Units

Algumas unidades terão literais especiais.

Tempo:

```text
5ns
10us
30ms
5s
2min
```

Dados:

```text
128bytes
4KiB
10MiB
```

Networking:

```text
100bps
10Kbps
100Mbps
10Gbps
```

---

# 66. Units são tipos

```text
let timeout: Duration = 500ms;
let rate: BitRate = 100Mbps;
let size: DataSize = 1500bytes;
```

Não deverão ser simples aliases numéricos.

Isso impede:

```text
timeout + bandwidth
```

por erro.

---

# 67. Simulação

A linguagem possuirá uma biblioteca oficial de simulação determinística.

```text
use sim;
```

Ela suportará:

- discrete-event simulation;
- virtual time;
- deterministic scheduler;
- reproducible randomness;
- network latency;
- packet loss;
- bandwidth;
- jitter;
- failures.

---

# 68. Virtual time

Durante uma simulação:

```text
sleep(5s);
```

não precisa significar cinco segundos reais.

O scheduler pode avançar diretamente o relógio virtual.

---

# 69. Seed

```text
let simulation = Simulation {
    seed: 42,
};
```

Executar novamente com a mesma configuração deverá produzir o mesmo resultado, salvo componentes explicitamente não determinísticos.

---

# 70. Interfaces reais e simuladas

Um objetivo central será permitir interfaces comuns.

Conceitualmente:

```text
trait Network {
    async fn connect(
        self,
        address: SocketAddress
    ) -> Result<Connection, NetworkError>;
}
```

Poderemos ter:

```text
SystemNetwork
SimulatedNetwork
```

Ambas implementando:

```text
Network
```

Assim o mesmo código poderá funcionar contra uma rede real ou simulada.

---

# 71. Network DSL

No futuro, após validarmos as abstrações, poderemos adicionar:

```text
network lab {
    node server;
    node client;

    link server <-> client {
        latency: 20ms;
        bandwidth: 100Mbps;
    }
}
```

Mas isso **não faz parte do MVP do parser**.

---

# 72. Services DSL

Igualmente:

```text
service api {
    listen tcp :8080;
}
```

é uma direção desejada.

Porém inicialmente poderá ser equivalente a APIs da standard library.

Primeiro validamos a semântica.

Depois criamos açúcar sintático.

---

# 73. Módulos

Arquivos representarão módulos.

Exemplo:

```text
src/
├── main.ux
├── packet.ux
└── network/
    └── tcp.ux
```

Import:

```text
use packet;
use network.tcp;
```

---

# 74. Visibilidade

Tudo será privado por padrão.

```text
fn helper() {
}
```

Público:

```text
pub fn connect() {
}
```

Estruturas:

```text
pub struct Packet {
    pub source: Address;
    destination: Address;
}
```

---

# 75. Constantes

```text
const DEFAULT_PORT: u16 = 8080;
```

Constantes devem ser avaliadas em compile-time quando possível.

---

# 76. Compile-time execution

A v0.1 não terá um sistema arbitrário de `comptime`.

A avaliação em compile-time será inicialmente restrita a:

- constantes;
- tamanhos de arrays;
- literals;
- operações puras simples;
- generics.

Metaprogramação mais poderosa poderá ser adicionada posteriormente.

---

# 77. Macros

Macros não fazem parte do MVP.

Especialmente não teremos inicialmente macros de tokens extremamente poderosas.

O objetivo é evitar criar duas linguagens simultaneamente.

---

# 78. FFI com C

Será recurso obrigatório.

Exemplo conceitual:

```text
extern "C" {
    fn strlen(value: *u8) -> usize;
}
```

Chamadas consideradas inseguras exigirão:

```text
unsafe {
    strlen(ptr);
}
```

---

# 79. ABI

A linguagem terá sua própria ABI inicialmente instável.

Para interfaces estáveis externas:

```text
extern "C"
```

deverá ser utilizado.

---

# 80. Layout de estruturas

Por padrão, o compilador pode escolher layout eficiente.

Para interoperabilidade:

```text
@repr(C)
struct Header {
    ...
}
```

Para protocolos:

```text
@packed
struct Header {
    version: u8;
    flags: u8;
    length: u16;
}
```

---

# 81. Endianness

Conversões devem ser explícitas:

```text
value.to_be();
value.to_le();
```

ou:

```text
u16.from_be(bytes);
```

Isso é especialmente importante para networking.

---

# 82. Segurança no parsing de pacotes

APIs oficiais deverão favorecer parsing bounds-safe.

Em vez de:

```text
ptr + 17
```

deveremos poder escrever:

```text
let reader = PacketReader.new(data);

let version = reader.read<u8>()?;
let length = reader.read_be<u16>()?;
```

---

# 83. Main

Programa executável:

```text
fn main() {
    ...
}
```

Se puder falhar:

```text
fn main() -> Result<unit, Error> {
    ...
}
```

---

# 84. Package manifest

Nome provisório:

```text
lang.toml
```

Exemplo:

```text
[package]
name = "router-monitor"
version = "0.1.0"

[dependencies]
mqtt = "1.0"
```

O formato definitivo pode mudar.

---

# 85. CLI

A toolchain deve possuir uma única interface principal.

Nome provisório:

```text
lang
```

Comandos:

```text
lang new
lang build
lang run
lang check
lang test
lang fmt
lang lint
lang bench
lang doc
lang add
```

---

# 86. Build profiles

```text
debug
release
```

Debug deve priorizar:

```text
diagnostics
runtime checks
fast compilation
```

Release:

```text
optimization
LTO
dead-code elimination
smaller binaries
```

Checks de segurança fundamentais não devem desaparecer arbitrariamente em release.

---

# 87. Formatter

A linguagem terá formatter oficial.

Portanto, debates sobre estilo deverão ser minimizados.

Exemplo:

```text
lang fmt
```

O formatter define o estilo canônico.

---

# 88. Diagnostics

Mensagens de erro são parte da linguagem.

Exemplo:

```text
error[E0214]: use of moved value `packet`

  src/main.ux:18:10

14 | send(packet);
   |      ------ value moved here

18 | log(packet);
   |     ^^^^^^ value used after move

help:
    pass `&packet` if `send` does not need ownership
```

---

# 89. Warnings

Warnings importantes deverão incluir:

- unused values;
- unreachable code;
- suspicious casts;
- ignored Result;
- unused mutable;
- shadowing suspeito;
- unsafe operation desnecessária.

---

# 90. Result não pode ser ignorado silenciosamente

Se:

```text
socket.send(data);
```

retornar `Result`, ignorá-lo deve gerar erro ou warning forte.

Preferível:

```text
socket.send(data)?;
```

ou:

```text
let _ = socket.send(data);
```

O segundo deixa explícita a intenção.

---

# 91. No implicit network I/O

Operações que podem bloquear ou executar I/O não devem se esconder atrás de getters inocentes.

Evitar:

```text
let x = server.clients;
```

executando uma consulta de rede.

I/O deve ser semanticamente aparente.

---

# 92. Standard library

Módulos iniciais:

```text
core
std.io
std.fs
std.net
std.time
std.thread
std.task
std.sync
std.collections
std.process
std.system
std.sim
```

---

# 93. Core

`core` deve funcionar sem runtime complexo.

Deve conter:

```text
primitive types
Option
Result
traits fundamentais
memory primitives
slice
array
```

Isso permitirá futuramente ambientes `no_std`.

---

# 94. No standard library

Programas avançados poderão eventualmente usar:

```text
@no_std
```

para ambientes:

- kernels;
- embedded;
- bootloaders;
- runtimes próprios.

Não é requisito do primeiro MVP.

---

# 95. Segurança versus performance

O compilador poderá eliminar verificações quando provar que são desnecessárias.

Exemplo:

```text
for i in 0..array.len() {
    array[i]
}
```

O optimizer poderá remover bounds checks redundantes.

Portanto:

> segurança sintática não implica necessariamente custo de runtime.

---

# 96. Filosofia de abstração

Deveremos manter três níveis claros.

```text
High-level
──────────────────
Service
Protocol
Task
Simulation

Mid-level
──────────────────
TCP
UDP
Socket
Channel
File

Low-level
──────────────────
Raw pointer
Syscall
Memory
Hardware
```

Um desenvolvedor poderá atravessar essas camadas dentro do mesmo projeto.

---

# 97. Prioridades técnicas

Quando houver conflito:

```text
Correctness
     ↓
Safety
     ↓
Predictability
     ↓
Clarity
     ↓
Performance
     ↓
Convenience
```

Isso não significa aceitar software lento.

Significa que performance deve vir de implementação e otimização, não de comportamentos perigosos implícitos.

---

# 98. Recursos explicitamente fora da v0.1

Não implementaremos inicialmente:

- classes;
- herança;
- reflection completa;
- macros avançadas;
- exceptions;
- garbage collector global;
- JIT;
- decorators arbitrários;
- dynamic typing;
- operator overloading irrestrito;
- multiple inheritance;
- runtime reflection;
- metaclasses;
- implicit null;
- implicit numeric narrowing.

---

# 99. MVP da linguagem

A primeira versão executável precisará suportar somente:

```text
inteiros
floats
bool
strings

let
let mut

expressões

if
while
for

functions

structs

arrays
slices

Option
Result

basic ownership

basic borrowing

modules

native compilation
```

Depois:

```text
traits
generics
async
channels
networking
simulation
```

---

# 100. Ordem de implementação

## Fase 1 — Frontend mínimo

```text
Lexer
Parser
AST
Diagnostics
```

Programa:

```text
fn main() {
    let x = 10;

    if x > 5 {
        print(x);
    }
}
```

---

## Fase 2 — Semantic analysis

```text
symbol table
scope
type checking
type inference
function resolution
```

---

## Fase 3 — Intermediate Representation

Criar:

```text
HIR
```

seguido de:

```text
MIR
```

MIR deverá simplificar:

- ownership analysis;
- borrow checking;
- optimization;
- code generation.

---

## Fase 4 — Native code

Backend inicial:

```text
LLVM
```

Produzir:

```text
program.exe
```

ou:

```text
program
```

---

## Fase 5 — Ownership

Implementar:

```text
moves
borrows
destruction
Drop
```

---

## Fase 6 — Standard library mínima

```text
String
Vec
Option
Result
File
Time
```

---

## Fase 7 — Concurrency

```text
Task
spawn
scope
channel
async
await
```

---

## Fase 8 — Networking

```text
IP
TCP
UDP
DNS
Socket
async networking
```

---

## Fase 9 — Simulation

```text
virtual clock
event scheduler
simulated network
packet loss
latency
bandwidth
```

---

## Fase 10 — Domain syntax

Somente então avaliar:

```text
network
service
node
link
protocol
simulate
```

como recursos especiais da sintaxe.

---

# 101. Programa representativo

Um objetivo intermediário para a linguagem será compilar código semelhante a:

```text
use std.net.TcpListener;

fn main() -> Result<unit, Error> {
    let listener = TcpListener.bind(":8080")?;

    print("Listening on port 8080");

    scope {
        loop {
            let client = listener.accept()?;

            spawn handleClient(client);
        }
    }

    return Ok(());
}

fn handleClient(mut client: TcpStream)
    -> Result<unit, NetworkError>
{
    let mut buffer = [0u8; 4096];

    loop {
        let received = client.read(&mut buffer)?;

        if received == 0 {
            break;
        }

        client.write(&buffer[0..received])?;
    }

    return Ok(());
}
```

Esse programa representa grande parte da filosofia da linguagem:

- simples;
- compilado;
- seguro;
- eficiente;
- sem GC obrigatório;
- networking direto;
- recursos liberados automaticamente;
- tratamento explícito de erro;
- concorrência estruturada.

---

# 102. Diferencial central

O diferencial não será apenas sintaxe.

Ele será a integração entre:

```text
SYSTEMS
   +
NETWORKING
   +
CONCURRENCY
   +
SIMULATION
   +
SAFETY
```

O objetivo de longo prazo será permitir escrever:

```text
um serviço
```

executá-lo:

```text
em uma rede real
```

testá-lo:

```text
em uma rede simulada
```

submetê-lo a:

```text
latência
packet loss
falhas
congestionamento
```

e observar:

```text
metrics
traces
events
packets
```

sem trocar completamente de ecossistema.

---

# 103. Regra arquitetural

Networking e simulação deverão compartilhar abstrações sempre que isso fizer sentido.

Por exemplo:

```text
Network
Connection
Address
Packet
Clock
Task
```

poderão possuir implementações:

```text
Real
Simulated
```

Isso será um dos fundamentos arquiteturais da linguagem.

---

# 104. Estado da v0.1

## Decisões consideradas estabelecidas

- linguagem compilada;
- tipagem estática;
- type inference;
- imutabilidade por padrão;
- null safety;
- Result para erros;
- sem exception handling tradicional inicialmente;
- ownership;
- borrowing;
- sem GC global obrigatório;
- deterministic destruction;
- traits em vez de inheritance;
- generics;
- async/await;
- structured concurrency;
- networking oficial;
- simulation oficial;
- unsafe explícito;
- C FFI;
- pequeno runtime;
- LLVM como primeiro backend provável.

## Decisões ainda experimentais

- sintaxe exata de referências;
- detalhes do borrow checker;
- implementação de async runtime;
- scheduler;
- sintaxe definitiva de network DSL;
- sintaxe definitiva de service DSL;
- reflection;
- operator overloading;
- closures avançadas;
- package format definitivo;
- nome da linguagem.

---

# 105. Regra para evolução

Nenhuma feature deverá ser adicionada apenas porque existe em:

- C++;
- Rust;
- Go;
- Python;
- Java;
- Zig.

A pergunta sempre deverá ser:

> **Esse recurso contribui diretamente para systems, networks, concurrency, simulation ou safety?**

Se a resposta for não, deverá existir uma justificativa forte para adicioná-lo.

---

# 106. Definição curta

A linguagem pode ser tecnicamente descrita como:

> **Uma linguagem compilada e estaticamente tipada para programação de sistemas e redes, com ownership simplificado, concorrência estruturada e suporte integrado a ambientes reais e simulados.**

Ou, de forma ainda mais curta:

> **A systems language built around networks.**