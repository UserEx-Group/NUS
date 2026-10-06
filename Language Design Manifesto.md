# Language Design Manifesto v0.1

## 1. Visão

Esta linguagem existe para tornar a programação de sistemas, redes, concorrência e simulação mais segura, acessível, previsível e eficiente.

Ela é voltada a desenvolvedores que precisam de controle próximo ao hardware, modelagem lógica de redes, comunicação entre dispositivos, gerenciamento de serviços de rede, concorrência e simulação, sem carregar toda a complexidade de linguagens como C++ ou Rust e sem aceitar o custo de execução e consumo de recursos típico de linguagens interpretadas de alto nível.

A linguagem deve permitir que o desenvolvedor trabalhe em diferentes níveis de abstração sem precisar trocar de linguagem.

O mesmo ecossistema deve ser capaz de suportar:

- ferramentas de linha de comando;
- serviços de rede;
- servidores;
- agentes de monitoramento;
- sistemas distribuídos;
- aplicações concorrentes;
- simuladores;
- automação de infraestrutura;
- dispositivos com recursos limitados;
- componentes próximos ao sistema operacional;
- aplicações que interagem diretamente com protocolos e hardware.

Seu princípio central é:

**Controle quando necessário. Abstração quando conveniente. Segurança por padrão.**

---

# 2. Identidade

A linguagem não pretende ser apenas mais uma linguagem de propósito geral.

Sua identidade principal é:

**Uma linguagem de sistemas orientada a redes, concorrência e simulação.**

Em inglês:

**Network-Oriented Systems Programming Language.**

Ela deve ocupar um espaço entre linguagens de alto nível e linguagens tradicionais de sistemas.

A linguagem busca combinar:

- a produtividade de linguagens modernas;
- a previsibilidade de linguagens compiladas;
- o controle de linguagens de sistemas;
- abstrações específicas para redes;
- concorrência segura;
- suporte nativo a simulação;
- baixo consumo de recursos.

---

# 3. Filosofia

A linguagem segue cinco ideias fundamentais.

## 3.1 High-level by default, low-level by choice

A linguagem deve oferecer abstrações convenientes por padrão.

Um servidor deve poder ser criado de maneira simples:

```txt
service api {
    listen tcp :8080;
}
```

Mas o desenvolvedor deve poder acessar níveis inferiores quando necessário:

```txt
let socket = tcp.socket();

socket.bind(
    address: "0.0.0.0",
    port: 8080
);

socket.listen();
```

E operações realmente perigosas devem ser explicitamente marcadas:

```txt
unsafe {
    let ptr = memory.alloc<u8>(1024);
}
```

A linguagem não deve impedir operações de baixo nível.

Ela deve apenas tornar explícito quando o desenvolvedor deixa as garantias de segurança da linguagem.

---

## 3.2 Simple things should be simple

Operações comuns não devem exigir cerimônia excessiva.

```txt
let port = 8080;
let host = "localhost";
```

O desenvolvedor não deveria precisar declarar tipos quando eles podem ser inferidos com segurança.

---

## 3.3 Dangerous things should be explicit

Operações perigosas não devem parecer operações comuns.

Isso inclui:

- manipulação manual de memória;
- ponteiros crus;
- acesso direto a hardware;
- syscalls não verificadas;
- compartilhamento de memória não sincronizado;
- conversões potencialmente inseguras;
- FFI sem garantias de segurança.

A linguagem deve tornar essas operações visualmente identificáveis.

---

## 3.4 Zero-cost abstractions whenever possible

Abstrações da linguagem não devem necessariamente significar maior custo em runtime.

Quando possível:

```txt
for packet in packets {
    process(packet);
}
```

deve gerar código tão eficiente quanto uma implementação manual equivalente.

Abstrações devem ser resolvidas em compile-time sempre que possível.

---

## 3.5 Predictability over magic

A linguagem deve evitar comportamentos implícitos difíceis de prever.

Código deve ser legível por humanos e analisável pelo compilador.

Recursos que introduzam comportamento invisível ou imprevisível devem ser evitados.

---

# 4. Pilares

A linguagem possui cinco pilares fundamentais.

## 4.1 Systems

A linguagem deve fornecer acesso eficiente a recursos do sistema operacional.

Isso inclui:

- memória;
- arquivos;
- processos;
- threads;
- sockets;
- timers;
- sinais;
- dispositivos;
- syscalls;
- hardware;
- bibliotecas nativas.

A linguagem deve ser capaz de implementar componentes de infraestrutura sem depender de outra linguagem para suas funcionalidades básicas.

---

## 4.2 Networks

Redes são um conceito fundamental da linguagem.

Networking não deve ser apenas uma biblioteca de terceiros.

A biblioteca padrão ou runtime oficial deve oferecer suporte consistente a:

- IPv4;
- IPv6;
- TCP;
- UDP;
- DNS;
- sockets;
- multicast;
- interfaces de rede;
- endereços;
- portas;
- conexões;
- streams;
- protocolos;
- servidores;
- clientes.

Protocolos de nível superior poderão existir como módulos oficiais:

- HTTP;
- WebSocket;
- MQTT;
- TLS;
- QUIC;
- gRPC;
- outros.

A linguagem deve facilitar tanto a construção quanto a observação de sistemas de rede.

---

# 5. Redes como entidades de primeira classe

A linguagem deve permitir representar conceitos de rede diretamente.

Exemplo:

```txt
network factory {
    node controller {
        address: "10.0.0.10";
    }

    node sensor {
        address: "10.0.0.20";
    }

    link controller <-> sensor {
        latency: 5ms;
        bandwidth: 100Mbps;
    }
}
```

Uma definição desse tipo poderá ser utilizada para:

- simulação;
- documentação;
- testes;
- validação;
- geração de configuração;
- observabilidade;
- eventualmente deployment.

A mesma representação conceitual deve poder conectar redes simuladas e redes reais.

---

# 6. Serviços de rede

Serviços devem ser abstrações naturais da linguagem.

Exemplo:

```txt
service telemetry {
    listen tcp :8080;

    on connection(client) {
        log("Connected:", client.address);
    }
}
```

Ou:

```txt
service broker {
    mqtt.listen(:1883);

    on message(topic, message) {
        process(topic, message);
    }
}
```

Essas abstrações devem ser implementadas sobre primitivas mais fundamentais.

O desenvolvedor poderá decidir qual nível utilizar.

---

# 7. Concorrência

Concorrência deve fazer parte do design da linguagem desde sua primeira versão.

A linguagem não deve tratar concorrência como um recurso secundário.

Devem existir abstrações como:

```txt
spawn monitor();
```

```txt
await server.start();
```

```txt
parallel {
    collect();
    analyze();
    serve();
}
```

E comunicação segura:

```txt
channel<Packet> packets;

spawn capture(packets);
spawn analyze(packets);
```

A linguagem deve desencorajar compartilhamento arbitrário de memória mutável.

Comunicação por mensagens deve ser preferida quando apropriado.

---

# 8. Threads

Threads continuarão disponíveis.

Porém, o usuário não deveria precisar manipulá-las diretamente na maioria das aplicações.

O modelo de alto nível poderá utilizar:

- tasks;
- executors;
- coroutines;
- async/await;
- channels;
- structured concurrency.

Threads serão consideradas uma primitiva de nível inferior.

---

# 9. Structured concurrency

Tarefas concorrentes devem possuir relações claras de vida útil.

A linguagem deve evitar tarefas abandonadas ou impossíveis de controlar.

Por exemplo:

```txt
scope {
    spawn collect();
    spawn monitor();
    spawn serve();
}
```

Ao sair do escopo, o runtime deve saber como tratar todas as tarefas pertencentes a ele.

---

# 10. Simulação

Simulação é um elemento fundamental da linguagem.

O objetivo não é apenas executar aplicações reais, mas também permitir representar e testar sistemas antes de executá-los.

Exemplo:

```txt
simulate factory {
    duration: 60s;

    latency: 100ms;
    packet_loss: 2%;
}
```

A infraestrutura de simulação deve permitir:

- tempo virtual;
- eventos discretos;
- falhas;
- latência;
- perda de pacotes;
- jitter;
- largura de banda;
- indisponibilidade de nós;
- congestionamento;
- falhas de serviços.

---

# 11. Rede real e rede simulada

Um dos objetivos de longo prazo é permitir que partes da mesma aplicação possam ser executadas contra ambientes reais ou simulados.

Exemplo conceitual:

```txt
let network = simulation.network("factory");
```

ou:

```txt
let network = system.network();
```

O restante da aplicação deveria exigir poucas alterações.

Isso permitirá:

- testes reproduzíveis;
- chaos testing;
- validação de arquitetura;
- experimentação;
- treinamento;
- benchmarking.

---

# 12. Segurança

Segurança deve ser padrão, não uma funcionalidade opcional.

A linguagem deve impedir ou detectar uma grande classe de erros ainda durante a compilação.

Isso inclui, quando possível:

- uso após liberação;
- null pointer;
- data races;
- acesso fora dos limites;
- double free;
- conversões inválidas;
- acesso a memória não inicializada.

---

# 13. Memória

A linguagem não deve exigir que o desenvolvedor gerencie manualmente toda memória.

Ao mesmo tempo, ela não deve depender necessariamente de um garbage collector tradicional para toda aplicação.

O sistema de memória deverá buscar um equilíbrio entre:

- segurança;
- simplicidade;
- previsibilidade;
- performance.

Possíveis estratégias incluem:

- ownership simplificado;
- análise de lifetime;
- move semantics;
- RAII;
- reference counting em situações específicas;
- alocação automática baseada em escopo.

O modelo final deverá ser escolhido após experimentação.

---

# 14. Garbage collection

A linguagem não deve depender de um garbage collector global e pesado.

Isso não significa proibir garbage collection.

Alguns tipos ou runtimes poderão utilizar técnicas de gerenciamento automático quando isso for vantajoso.

Porém:

**GC não deve ser requisito fundamental para execução de qualquer programa.**

Isso permitirá aplicações com requisitos de:

- baixa latência;
- uso previsível de memória;
- execução embarcada;
- sistemas;
- servidores de alta performance.

---

# 15. Tipagem

A linguagem deverá utilizar tipagem estática.

Tipos devem ser conhecidos em compile-time sempre que possível.

Por exemplo:

```txt
let port = 8080;
```

O compilador deduz:

```txt
int
```

Declarações explícitas continuam possíveis:

```txt
let port: u16 = 8080;
```

---

# 16. Inferência de tipos

O sistema de tipos deve evitar redundância.

```txt
let name = "router";
```

é preferível a:

```txt
let name: string = "router";
```

quando o tipo é óbvio.

Entretanto, interfaces públicas devem favorecer clareza.

Por exemplo:

```txt
fn latency(packet: Packet) -> Duration {
    ...
}
```

---

# 17. Null safety

Null não deve ser implicitamente permitido em qualquer referência.

Em vez disso:

```txt
User
```

significa que um valor existe.

E:

```txt
User?
```

indica explicitamente ausência possível.

Exemplo:

```txt
let user: User? = findUser(id);
```

O compilador deve exigir tratamento adequado antes do uso.

---

# 18. Erros

Erros previsíveis não devem ser tratados como exceções invisíveis.

Operações sujeitas a falhas devem expressar isso em seus tipos.

Exemplo conceitual:

```txt
fn connect(address: Address) -> Result<Connection, NetworkError>
```

Uso:

```txt
let connection = connect(address)?;
```

ou:

```txt
match connect(address) {
    Ok(connection) => use(connection),
    Err(error) => log(error)
}
```

Exceções poderão existir apenas se houver uma justificativa clara.

---

# 19. Imutabilidade

Valores devem ser imutáveis por padrão.

```txt
let port = 8080;
```

Se mutabilidade for necessária:

```txt
let mut attempts = 0;
```

Isso reduz efeitos colaterais e facilita análise de concorrência.

---

# 20. Estruturas de dados

A linguagem deverá oferecer estruturas leves.

Exemplo:

```txt
struct Packet {
    source: Address;
    destination: Address;
    payload: Bytes;
}
```

Deve ser possível controlar layout quando necessário:

```txt
packed struct Header {
    version: u8;
    flags: u8;
    length: u16;
}
```

Isso será importante para protocolos de rede.

---

# 21. Enums e tipos algébricos

Enums devem poder carregar valores.

```txt
enum Packet {
    TCP(TcpPacket),
    UDP(UdpPacket),
    ICMP(IcmpPacket)
}
```

Isso facilita modelagem segura de protocolos.

---

# 22. Pattern matching

Pattern matching será uma funcionalidade importante.

```txt
match packet {
    TCP(packet) => processTcp(packet),
    UDP(packet) => processUdp(packet),
    ICMP(packet) => processIcmp(packet)
}
```

---

# 23. Unidades como tipos

A linguagem deve considerar unidades utilizadas em sistemas e redes como conceitos seguros.

Exemplo:

```txt
let timeout = 500ms;
let bandwidth = 100Mbps;
let size = 1500bytes;
```

Isso permite evitar erros como misturar:

- segundos e milissegundos;
- bits e bytes;
- Mbps e MB/s.

Quando possível, conversões devem ser feitas em compile-time.

---

# 24. Recursos

Recursos do sistema devem possuir ciclos de vida previsíveis.

Exemplo:

```txt
with file = File.open("data.txt") {
    ...
}
```

ou através de destruição automática por escopo.

Sockets, arquivos e locks devem ser liberados automaticamente ao final de suas vidas úteis.

---

# 25. FFI

Interoperabilidade com C é requisito fundamental.

A linguagem deve conseguir utilizar bibliotecas existentes sem wrappers excessivamente complexos.

Exemplo conceitual:

```txt
extern "C" {
    fn malloc(size: usize) -> *void;
}
```

Integração com C++ poderá surgir posteriormente.

---

# 26. Runtime

O runtime deve ser pequeno.

Um programa simples não deve carregar uma infraestrutura gigantesca sem necessidade.

Funcionalidades deverão ser incluídas conforme utilizadas.

Idealmente:

```txt
hello world
```

não deve carregar:

- scheduler de async;
- stack de networking;
- runtime de simulação;
- garbage collector;
- bibliotecas desnecessárias.

---

# 27. Compilação

A linguagem deve ser compilada.

O objetivo de longo prazo é produzir código nativo eficiente.

Possíveis backends:

- LLVM;
- Cranelift;
- backend próprio;
- WebAssembly.

A arquitetura do compilador deve evitar dependência irreversível de um único backend.

---

# 28. Performance

A linguagem não precisa sempre vencer C ou Rust em benchmarks.

Mas ela deve possuir uma regra:

**Abstrações comuns não podem transformar código previsível em código absurdamente caro.**

O desempenho deve ser suficientemente próximo de linguagens compiladas para permitir seu uso em sistemas reais.

---

# 29. Zero hidden allocation

Operações simples não devem realizar alocações invisíveis desnecessárias.

Quando uma operação pode alocar memória, isso deve ser documentável e previsível.

A linguagem deve facilitar código allocation-free quando necessário.

---

# 30. Tooling

Ferramentas oficiais fazem parte da linguagem.

Não serão tratadas como projetos secundários.

O ecossistema deverá possuir:

- compiler;
- formatter;
- linter;
- package manager;
- test runner;
- benchmark runner;
- documentation generator;
- debugger integration;
- language server.

Idealmente, uma única CLI central:

```txt
lang new
lang build
lang run
lang test
lang fmt
lang lint
lang bench
lang doc
lang add
```

O nome `lang` é apenas ilustrativo.

---

# 31. Build system

A linguagem deve evitar reproduzir a complexidade de build systems tradicionais.

Um projeto comum deve funcionar sem configuração extensa.

Exemplo:

```txt
project {
    name: "router-monitor";
    version: "0.1.0";
}
```

Dependências:

```txt
dependencies {
    mqtt: "1.2";
    telemetry: "0.4";
}
```

---

# 32. Pacotes

Pacotes devem possuir:

- versionamento;
- dependências;
- metadata;
- hashes;
- lockfile;
- reprodutibilidade.

Reprodutibilidade é particularmente importante para software de infraestrutura.

---

# 33. Observabilidade

Aplicações de rede devem ser fáceis de observar.

A standard library deverá facilitar:

- logs;
- métricas;
- traces;
- profiling;
- eventos;
- estatísticas de rede.

Isso não significa que observabilidade será obrigatória.

Ela simplesmente não deverá exigir frameworks gigantescos para tarefas simples.

---

# 34. Determinismo

Quando possível, a linguagem e seus runtimes devem favorecer comportamento determinístico.

Isso é especialmente importante para simulação e testes.

Simulações devem ser capazes de utilizar seeds controladas:

```txt
simulate network {
    seed: 420;
}
```

Assim, falhas poderão ser reproduzidas.

---

# 35. Testes

Testes são parte fundamental do ecossistema.

Exemplo conceitual:

```txt
test "packet serialization" {
    let packet = Packet(...);

    assert(packet.serialize().size == 128);
}
```

Testes de redes poderão incluir simulação:

```txt
test network "handles packet loss" {
    packet_loss: 10%;

    run service;

    expect delivered > 90%;
}
```

---

# 36. Filosofia de sintaxe

A sintaxe deve priorizar:

1. legibilidade;
2. consistência;
3. baixa ambiguidade;
4. facilidade de parsing;
5. familiaridade;
6. concisão.

Não devemos criar sintaxe diferente apenas para parecer original.

Originalidade deve surgir da semântica e das capacidades da linguagem.

---

# 37. O que a linguagem não é

A linguagem não pretende ser:

- substituta universal de todas as linguagens;
- linguagem de frontend web;
- ferramenta principal para scripts descartáveis;
- linguagem focada em data science;
- linguagem otimizada especificamente para machine learning;
- substituta direta de JavaScript;
- uma linguagem puramente funcional;
- uma linguagem exclusivamente orientada a objetos.

Esses domínios podem ser suportados eventualmente.

Mas não serão prioridade.

---

# 38. Não objetivos

A linguagem não terá como objetivo inicial:

### Aplicações web frontend

JavaScript, TypeScript e WebAssembly já possuem ecossistemas fortes para isso.

### Machine learning

Não tentaremos competir inicialmente com Python, PyTorch ou TensorFlow.

### GUIs complexas

GUIs poderão existir através de bibliotecas, mas não são parte central da linguagem.

### Metaprogramação extrema

Não queremos reproduzir sistemas de templates extremamente complexos.

### Compatibilidade com tudo

Não tentaremos suportar todo paradigma ou toda plataforma imediatamente.

---

# 39. Complexidade deve pagar aluguel

Todo novo recurso deve justificar sua existência.

Antes de adicionar uma feature, devemos perguntar:

- qual problema ela resolve?
- esse problema ocorre frequentemente?
- já existe outra maneira simples de resolver?
- qual complexidade ela adiciona ao compilador?
- qual complexidade ela adiciona ao programador?
- como ela interage com outros recursos?
- conseguimos removê-la posteriormente?

Uma feature só deve ser adicionada quando seu valor supera claramente sua complexidade.

---

# 40. Preferência por composição

A linguagem deve favorecer composição em vez de hierarquias profundas.

Interfaces, traits ou conceitos similares deverão representar capacidades.

Exemplo:

```txt
trait Serializable {
    fn serialize(self) -> Bytes;
}
```

Isso deve ser preferido a árvores extensas de herança.

---

# 41. Orientação a objetos

A linguagem poderá oferecer recursos associados a orientação a objetos.

Porém, classes e herança não serão o centro da linguagem.

O modelo preferencial será:

```txt
data + behavior + composition
```

em vez de:

```txt
hierarquias profundas de objetos
```

---

# 42. Macros

Macros não devem fazer parte do núcleo inicial.

Se forem adicionadas no futuro, devem favorecer transformação segura de código e evitar criação de uma segunda linguagem dentro da linguagem.

---

# 43. Compatibilidade

O design deve considerar interoperabilidade com:

- C;
- sistemas operacionais existentes;
- bibliotecas nativas;
- protocolos de rede existentes;
- formatos de dados existentes.

A linguagem não deve exigir que todo ecossistema seja reescrito do zero.

---

# 44. Plataformas

As primeiras plataformas-alvo deverão ser:

- Linux x86-64;
- Windows x86-64.

Posteriormente:

- Linux ARM64;
- macOS ARM64;
- Raspberry Pi;
- WebAssembly;
- microcontroladores selecionados.

---

# 45. Relação com hardware

A linguagem deve permitir abstração segura sobre hardware.

Exemplo futuro:

```txt
gpio led = pin(13);

led.high();
sleep(500ms);
led.low();
```

Mas também deve permitir controle inferior quando necessário.

---

# 46. Redes embarcadas

Uma aplicação futura importante é utilizar a mesma linguagem em dispositivos pequenos e servidores.

Exemplo:

```txt
sensor -> gateway -> server
```

Todos podendo compartilhar:

- tipos;
- protocolos;
- serialização;
- mensagens;
- contratos.

---

# 47. Simulação como ferramenta de desenvolvimento

Simulação não deverá ser apenas utilizada por simuladores independentes.

Ela deverá poder participar do ciclo normal de desenvolvimento.

Fluxo ideal:

```txt
write
↓
compile
↓
simulate
↓
test
↓
benchmark
↓
deploy
↓
observe
```

---

# 48. Contratos de rede

Uma direção futura poderá permitir declarar contratos entre serviços.

Exemplo:

```txt
protocol Telemetry {
    message SensorReading {
        temperature: float;
        humidity: float;
    }
}
```

Tanto servidor quanto cliente poderiam utilizar o mesmo contrato.

---

# 49. Segurança de protocolos

A linguagem deverá incentivar parsing seguro.

Dados vindos da rede devem ser tratados como não confiáveis.

APIs devem evitar permitir facilmente:

- buffer overflow;
- integer overflow;
- parsing fora dos limites;
- leitura arbitrária de memória.

---

# 50. Segurança não deve destruir produtividade

Segurança absoluta com experiência ruim não atende aos objetivos da linguagem.

O compilador deve:

- explicar erros;
- sugerir correções;
- mostrar contexto;
- evitar mensagens excessivamente abstratas.

Mensagens de erro são parte da experiência da linguagem.

---

# 51. Compiler UX

Erros devem ser semelhantes a:

```txt
error[E042]: value may be null

  src/server.lang:24:12

24 | client.send(message)
   | ^^^^^^ client may be null

help:
    handle the optional value first:

    if let client = client {
        client.send(message)
    }
```

O compilador deve ensinar o usuário a corrigir o problema.

---

# 52. Ferramentas não devem exigir IDE

A linguagem deve funcionar bem em:

- terminal;
- CI;
- servidores;
- containers;
- editores simples.

Uma IDE melhora a experiência, mas não deve ser requisito.

---

# 53. Desenvolvimento aberto

A evolução da linguagem deverá ocorrer através de propostas documentadas.

Um sistema semelhante a:

```txt
Language Enhancement Proposal
```

poderá ser criado.

Exemplo:

```txt
LEP-0001: Async Functions
LEP-0002: Network Types
LEP-0003: Structured Concurrency
```

Toda alteração importante deverá possuir justificativa técnica.

---

# 54. Estabilidade

A linguagem deve evoluir inicialmente com liberdade.

Antes da versão 1.0, breaking changes são aceitáveis.

Depois da versão 1.0, compatibilidade deverá receber prioridade muito maior.

---

# 55. Regra de ouro

Quando houver conflito entre objetivos, a prioridade será:

```txt
Correctness
    ↓
Safety
    ↓
Clarity
    ↓
Predictability
    ↓
Performance
    ↓
Convenience
```

Performance continua sendo fundamental.

Mas nenhuma otimização deve tornar o comportamento da linguagem incompreensível ou inseguro por padrão.

---

# 56. Os cinco pilares oficiais

A identidade da linguagem pode ser resumida em cinco palavras:

## SYSTEMS

Controle real sobre sistema operacional, memória, hardware e recursos.

## NETWORKS

Redes, protocolos e serviços como conceitos fundamentais.

## CONCURRENCY

Concorrência segura, eficiente e compreensível.

## SIMULATION

Capacidade de modelar, testar e experimentar sistemas complexos.

## SAFETY

Segurança por padrão sem impedir acesso ao baixo nível.

---

# 57. Manifesto resumido

A linguagem acredita que:

**Programação de sistemas não precisa ser desnecessariamente difícil.**

**Networking não deveria exigir dezenas de camadas de boilerplate.**

**Concorrência deveria ser segura por padrão.**

**Simulações deveriam compartilhar conceitos com sistemas reais.**

**Abstrações de alto nível não deveriam obrigatoriamente custar performance.**

**Operações perigosas deveriam ser possíveis, mas explícitas.**

**Ferramentas fazem parte da linguagem.**

**O compilador deve ajudar o desenvolvedor, não lutar contra ele.**

**Controle e produtividade não precisam ser extremos opostos.**

---

# 58. Declaração final

A linguagem deve permitir que um desenvolvedor comece com:

```txt
service api {
    listen tcp :8080;
}
```

e, se necessário, consiga chegar até:

```txt
unsafe {
    syscall(...);
}
```

sem abandonar a linguagem.

Entre esses dois extremos existirão abstrações para:

```txt
services
networks
protocols
tasks
channels
simulation
memory
hardware
```

Essa capacidade de transitar entre abstração e controle será uma das características fundamentais da linguagem.

**High-level by default.  
Low-level by choice.  
Network-oriented.  
Safe by default.  
Built for systems.**