# Core Language Grammar v0.1

**Status:** Draft  
**Versão:** 0.1  
**Escopo:** sintaxe do núcleo da linguagem

---

# 1. Objetivo

Este documento define:

- tokens;
- palavras-chave;
- literais;
- operadores;
- precedência;
- declarações;
- expressões;
- controle de fluxo;
- tipos;
- funções;
- structs;
- enums;
- traits;
- generics;
- módulos;
- blocos `unsafe`;
- sintaxe inicial de concorrência.

A gramática será descrita em EBNF simplificada.

---

# 2. Convenções

Nesta especificação:

```text
"token"
```

representa um token literal.

```text
rule
```

representa outra regra gramatical.

```text
[ rule ]
```

significa opcional.

```text
{ rule }
```

significa zero ou mais repetições.

```text
rule | other
```

significa alternativa.

---

# 3. Estrutura de um arquivo

```ebnf
source_file =
    { item } ,
    EOF ;
```

Um arquivo consiste em uma sequência de itens de nível superior.

---

# 4. Itens de nível superior

```ebnf
item =
      use_declaration
    | const_declaration
    | function_declaration
    | struct_declaration
    | enum_declaration
    | trait_declaration
    | impl_declaration
    | extern_declaration ;
```

---

# 5. Identificadores

```ebnf
identifier =
    letter ,
    { letter | digit | "_" } ;
```

Exemplos válidos:

```text
router
packet_count
TcpStream
IPv4Address
_handle
```

Exemplos inválidos:

```text
1router
packet-count
```

---

# 6. Case sensitivity

A linguagem é case-sensitive.

Portanto:

```text
packet
Packet
PACKET
```

são identificadores diferentes.

---

# 7. Convenções de nomes

Não serão regras obrigatórias do parser, mas o formatter e linter recomendarão:

```text
snake_case
```

para:

- variáveis;
- funções;
- módulos.

```text
PascalCase
```

para:

- structs;
- enums;
- traits;
- tipos.

```text
SCREAMING_SNAKE_CASE
```

para constantes.

Exemplo:

```text
let packet_count = 10;

fn process_packet() {}

struct TcpPacket {}

const MAX_CONNECTIONS = 1024;
```

---

# 8. Comentários

Comentário de linha:

```text
// comment
```

Comentário de bloco:

```text
/*
    comment
*/
```

Comentários de bloco deverão suportar nesting:

```text
/*
    outer

    /*
        inner
    */
*/
```

---

# 9. Documentation comments

Comentários de documentação:

```text
/// Documentation
fn connect() {}
```

Para módulos ou itens superiores:

```text
//! Module documentation
```

---

# 10. Whitespace

Os seguintes caracteres separam tokens:

```text
space
tab
newline
carriage return
```

Whitespace não é semanticamente significativo fora de strings.

A linguagem não utiliza indentação significativa.

---

# 11. Keywords

Palavras-chave reservadas da v0.1:

```text
as
async
await
break
const
continue
defer
else
enum
extern
false
fn
for
if
impl
in
let
loop
match
mut
pub
return
scope
spawn
struct
trait
true
unsafe
use
while
```

Reservadas para possível uso futuro:

```text
network
service
protocol
simulate
node
link
where
type
move
ref
static
```

Essas palavras não devem ser permitidas como identificadores comuns.

---

# 12. Literais inteiros

Decimal:

```text
42
1000
8080
```

Separadores:

```text
1_000
1_000_000
```

Binário:

```text
0b1010
```

Octal:

```text
0o755
```

Hexadecimal:

```text
0xFF
0xDEADBEEF
```

---

# 13. Sufixos inteiros

Tipos explícitos poderão ser especificados diretamente:

```text
10u8
500u16
42i32
1024usize
```

Tipos reconhecidos:

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

---

# 14. Literais floating point

```text
3.14
0.5
10.0
1_000.25
```

Notação científica:

```text
1e6
1.5e-3
```

Sufixos:

```text
3.14f32
3.14f64
```

---

# 15. Booleanos

```text
true
false
```

Tipo:

```text
bool
```

---

# 16. Caracteres

```text
'a'
'Z'
'\n'
'\u{1F600}'
```

Tipo:

```text
char
```

Representação conceitual baseada em Unicode scalar values.

---

# 17. Strings

```text
"hello"
"router-01"
```

Escapes:

```text
"\n"
"\r"
"\t"
"\""
"\\"
"\0"
```

Unicode:

```text
"\u{1F600}"
```

---

# 18. Raw strings

Permitiremos:

```text
r"raw text"
```

e:

```text
r#"text with "quotes""#
```

Para facilitar:

- regex;
- caminhos;
- protocolos;
- payloads;
- configuração.

---

# 19. Byte literals

```text
b'A'
```

Strings de bytes:

```text
b"HTTP/1.1"
```

---

# 20. Literais de duração

Inicialmente, o lexer reconhecerá números e identificadores normalmente.

A semântica poderá converter:

```text
500ms
5s
2min
```

em literais especiais posteriormente.

Para a primeira implementação, não precisamos tornar unidades tokens especiais.

Isso simplifica o lexer.

---

# 21. Operadores

Operadores aritméticos:

```text
+
-
*
/
%
```

Comparação:

```text
==
!=
<
<=
>
>=
```

Booleanos:

```text
&&
||
!
```

Bitwise:

```text
&
|
^
~
<<
>>
```

Atribuição:

```text
=
```

Atribuições compostas:

```text
+=
-=
*=
/=
%=
&=
|=
^=
<<=
>>=
```

Outros:

```text
.
::
->
=>
?
..
..=
&
*
@
```

---

# 22. Operadores deliberadamente ausentes

Não teremos:

```text
++
--
```

Motivo:

```text
x += 1;
```

é suficientemente explícito.

---

# 23. Precedência

Da maior para menor:

| Nível | Operadores | Associatividade |
|---|---|---|
| 1 | `()` `[]` `.` `::` | esquerda |
| 2 | `!` `~` `-` `&` `*` `await` | direita |
| 3 | `*` `/` `%` | esquerda |
| 4 | `+` `-` | esquerda |
| 5 | `<<` `>>` | esquerda |
| 6 | `<` `<=` `>` `>=` | esquerda |
| 7 | `==` `!=` | esquerda |
| 8 | `&` | esquerda |
| 9 | `^` | esquerda |
| 10 | `|` | esquerda |
| 11 | `&&` | esquerda |
| 12 | `||` | esquerda |
| 13 | `..` `..=` | não associativo |
| 14 | `=` `+=` etc. | direita |

---

# 24. Expressão

```ebnf
expression =
    assignment_expression ;
```

---

# 25. Assignment

```ebnf
assignment_expression =
    logical_or_expression ,
    [ assignment_operator , assignment_expression ] ;
```

```ebnf
assignment_operator =
      "="
    | "+="
    | "-="
    | "*="
    | "/="
    | "%="
    | "&="
    | "|="
    | "^="
    | "<<="
    | ">>=" ;
```

Somente valores mutáveis podem estar no lado esquerdo.

---

# 26. Logical OR

```ebnf
logical_or_expression =
    logical_and_expression ,
    { "||" , logical_and_expression } ;
```

---

# 27. Logical AND

```ebnf
logical_and_expression =
    bitwise_or_expression ,
    { "&&" , bitwise_or_expression } ;
```

---

# 28. Bitwise expressions

```ebnf
bitwise_or_expression =
    bitwise_xor_expression ,
    { "|" , bitwise_xor_expression } ;
```

```ebnf
bitwise_xor_expression =
    bitwise_and_expression ,
    { "^" , bitwise_and_expression } ;
```

```ebnf
bitwise_and_expression =
    equality_expression ,
    { "&" , equality_expression } ;
```

---

# 29. Equality

```ebnf
equality_expression =
    comparison_expression ,
    { ( "==" | "!=" ) , comparison_expression } ;
```

---

# 30. Comparison

```ebnf
comparison_expression =
    shift_expression ,
    {
        ( "<" | "<=" | ">" | ">=" ) ,
        shift_expression
    } ;
```

---

# 31. Shift

```ebnf
shift_expression =
    additive_expression ,
    {
        ( "<<" | ">>" ) ,
        additive_expression
    } ;
```

---

# 32. Additive

```ebnf
additive_expression =
    multiplicative_expression ,
    {
        ( "+" | "-" ) ,
        multiplicative_expression
    } ;
```

---

# 33. Multiplicative

```ebnf
multiplicative_expression =
    unary_expression ,
    {
        ( "*" | "/" | "%" ) ,
        unary_expression
    } ;
```

---

# 34. Unary

```ebnf
unary_expression =
      postfix_expression
    | unary_operator , unary_expression
    | "await" , unary_expression ;
```

```ebnf
unary_operator =
      "!"
    | "~"
    | "-"
    | "&"
    | "*" ;
```

---

# 35. Postfix expressions

```ebnf
postfix_expression =
    primary_expression ,
    {
          call_suffix
        | index_suffix
        | field_suffix
        | method_suffix
        | try_suffix
    } ;
```

---

# 36. Function calls

```ebnf
call_suffix =
    "(" ,
    [ argument_list ] ,
    ")" ;
```

```ebnf
argument_list =
    expression ,
    { "," , expression } ,
    [ "," ] ;
```

Exemplo:

```text
connect(address, port)
```

---

# 37. Named arguments

A v0.1 não terá named arguments universais.

Não:

```text
connect(port: 8080)
```

Funções comuns serão posicionais.

Struct literals continuam nomeados.

---

# 38. Indexing

```ebnf
index_suffix =
    "[" ,
    expression ,
    "]" ;
```

Exemplo:

```text
buffer[10]
```

---

# 39. Field access

```ebnf
field_suffix =
    "." ,
    identifier ;
```

Exemplo:

```text
packet.source
```

---

# 40. Method calls

Métodos são representados sintaticamente através de field access seguido de call.

```text
packet.size()
```

Não precisamos de regra gramatical separada no AST inicial.

Pode ser parseado como:

```text
Call(
    Member(packet, size)
)
```

---

# 41. Try operator

```ebnf
try_suffix =
    "?" ;
```

Exemplo:

```text
socket.bind(address)?
```

O parser apenas gera a expressão.

A análise semântica verifica se o tipo é `Result` ou `Option`, dependendo da política final.

---

# 42. Primary expressions

```ebnf
primary_expression =
      literal
    | identifier
    | tuple_expression
    | array_expression
    | block_expression
    | if_expression
    | match_expression
    | loop_expression
    | unsafe_expression ;
```

---

# 43. Parentheses

```ebnf
tuple_expression =
    "(" ,
    [ tuple_elements ] ,
    ")" ;
```

Uma única expressão:

```text
(x)
```

continua sendo agrupamento.

Tuple de um elemento:

```text
(x,)
```

---

# 44. Tuple elements

```ebnf
tuple_elements =
    expression ,
    "," ,
    [ expression , { "," , expression } ] ,
    [ "," ] ;
```

---

# 45. Arrays

```ebnf
array_expression =
    "[" ,
    [
        expression ,
        { "," , expression } ,
        [ "," ]
    ] ,
    "]" ;
```

Exemplo:

```text
[1, 2, 3]
```

---

# 46. Array repetition

Também teremos:

```text
[0; 1024]
```

Gramática:

```ebnf
array_expression =
      "[" , [ argument_list ] , "]"
    | "[" , expression , ";" , expression , "]" ;
```

---

# 47. Blocks

```ebnf
block =
    "{" ,
    { statement } ,
    [ expression ] ,
    "}" ;
```

A última expressão sem `;` é o valor do bloco.

Exemplo:

```text
{
    let x = 10;
    x * 2
}
```

retorna:

```text
20
```

---

# 48. Statements

```ebnf
statement =
      let_statement
    | return_statement
    | break_statement
    | continue_statement
    | defer_statement
    | expression_statement
    | item ;
```

Permitir itens dentro de blocos poderá ser restrito inicialmente.

---

# 49. Expression statement

```ebnf
expression_statement =
    expression ,
    ";" ;
```

---

# 50. Let

```ebnf
let_statement =
    "let" ,
    [ "mut" ] ,
    pattern ,
    [ ":" , type ] ,
    "=" ,
    expression ,
    ";" ;
```

Exemplos:

```text
let x = 10;
```

```text
let mut count = 0;
```

```text
let port: u16 = 8080;
```

---

# 51. Declarações sem inicialização

Não permitiremos inicialmente:

```text
let x: i32;
```

Variáveis locais devem ser inicializadas.

Isso evita estado não inicializado.

---

# 52. Patterns

Inicialmente:

```ebnf
pattern =
      identifier_pattern
    | tuple_pattern
    | wildcard_pattern ;
```

---

# 53. Identifier pattern

```ebnf
identifier_pattern =
    identifier ;
```

---

# 54. Wildcard

```text
_
```

representa um valor ignorado.

---

# 55. Tuple destructuring

```text
let (host, port) = address;
```

Gramática:

```ebnf
tuple_pattern =
    "(" ,
    pattern ,
    { "," , pattern } ,
    [ "," ] ,
    ")" ;
```

---

# 56. Return

```ebnf
return_statement =
    "return" ,
    [ expression ] ,
    ";" ;
```

Exemplos:

```text
return;
```

```text
return packet;
```

---

# 57. Break

```ebnf
break_statement =
    "break" ,
    [ expression ] ,
    ";" ;
```

---

# 58. Continue

```ebnf
continue_statement =
    "continue" ,
    ";" ;
```

---

# 59. Defer

```ebnf
defer_statement =
    "defer" ,
    expression ,
    ";" ;
```

Exemplo:

```text
defer file.close();
```

---

# 60. If

```ebnf
if_expression =
    "if" ,
    expression ,
    block ,
    [
        "else" ,
        ( block | if_expression )
    ] ;
```

Exemplo:

```text
if connected {
    send();
} else {
    retry();
}
```

---

# 61. Parênteses no if

Não são obrigatórios.

Preferido:

```text
if x > 10 {
}
```

em vez de:

```text
if (x > 10) {
}
```

Os parênteses continuam válidos como agrupamento da expressão.

---

# 62. If como expressão

```text
let state = if connected {
    "online"
} else {
    "offline"
};
```

Os dois branches devem produzir tipos compatíveis.

---

# 63. While

```ebnf
while_expression =
    "while" ,
    expression ,
    block ;
```

`while` inicialmente será statement-like e retornará `unit`.

---

# 64. Loop

```ebnf
loop_expression =
    "loop" ,
    block ;
```

---

# 65. For

```ebnf
for_expression =
    "for" ,
    pattern ,
    "in" ,
    expression ,
    block ;
```

Exemplo:

```text
for packet in packets {
    process(packet);
}
```

---

# 66. Ranges

```ebnf
range_expression =
      expression , ".." , expression
    | expression , "..=" , expression ;
```

Posteriormente:

```text
..
..end
start..
```

podem ser adicionados.

No MVP, usaremos ranges completos.

---

# 67. Match

```ebnf
match_expression =
    "match" ,
    expression ,
    "{" ,
    { match_arm } ,
    "}" ;
```

---

# 68. Match arm

```ebnf
match_arm =
    pattern ,
    [ match_guard ] ,
    "=>" ,
    expression ,
    [ "," ] ;
```

---

# 69. Match guard

```ebnf
match_guard =
    "if" ,
    expression ;
```

Exemplo:

```text
match packet {
    TCP(packet) if packet.port == 80 => handle_http(packet),
    TCP(packet) => handle_tcp(packet),
    _ => ignore(),
}
```

---

# 70. Enum patterns

Patterns serão expandidos para:

```text
Some(value)
None
TCP(packet)
```

Forma:

```ebnf
variant_pattern =
    path ,
    [
        "(" ,
        [ pattern , { "," , pattern } ] ,
        ")"
    ] ;
```

---

# 71. Function declaration

```ebnf
function_declaration =
    visibility ,
    [ "async" ] ,
    "fn" ,
    identifier ,
    [ generic_parameters ] ,
    "(" ,
    [ parameter_list ] ,
    ")" ,
    [ "->" , type ] ,
    block ;
```

---

# 72. Visibility

```ebnf
visibility =
    [ "pub" ] ;
```

---

# 73. Parameters

```ebnf
parameter_list =
    parameter ,
    { "," , parameter } ,
    [ "," ] ;
```

```ebnf
parameter =
    [ "mut" ] ,
    identifier ,
    ":" ,
    type ;
```

Exemplo:

```text
fn send(packet: Packet, attempts: u32) {
}
```

---

# 74. Self

Em `impl`, métodos poderão usar:

```text
self
&self
&mut self
```

`self` será tratado como keyword contextual, não necessariamente global.

Exemplo:

```text
impl Packet {
    fn size(&self) -> usize {
        self.payload.len()
    }
}
```

---

# 75. Expression body

A v0.1 não terá:

```text
fn add(a: i32, b: i32) => a + b;
```

Usaremos blocos sempre:

```text
fn add(a: i32, b: i32) -> i32 {
    a + b
}
```

---

# 76. Struct declaration

```ebnf
struct_declaration =
    visibility ,
    "struct" ,
    identifier ,
    [ generic_parameters ] ,
    "{" ,
    { struct_field } ,
    "}" ;
```

---

# 77. Struct field

```ebnf
struct_field =
    visibility ,
    identifier ,
    ":" ,
    type ,
    [ "," | ";" ] ;
```

O formatter deverá padronizar vírgulas:

```text
struct Packet {
    source: Address,
    destination: Address,
}
```

---

# 78. Unit structs

Poderemos suportar:

```text
struct Marker;
```

Mas não é necessário no primeiro parser.

---

# 79. Tuple structs

Também ficam fora do MVP inicial:

```text
struct Port(u16);
```

Primeiro usaremos structs nomeadas.

---

# 80. Struct literals

```ebnf
struct_expression =
    path ,
    "{" ,
    [ struct_initializer_list ] ,
    "}" ;
```

---

# 81. Struct initializer

```ebnf
struct_initializer_list =
    struct_initializer ,
    { "," , struct_initializer } ,
    [ "," ] ;
```

```ebnf
struct_initializer =
    identifier ,
    ":" ,
    expression ;
```

Exemplo:

```text
let packet = Packet {
    source: src,
    destination: dst,
};
```

---

# 82. Field shorthand

Posteriormente poderemos permitir:

```text
Packet {
    source,
    destination,
}
```

No MVP, podemos exigir forma explícita.

---

# 83. Enum declaration

```ebnf
enum_declaration =
    visibility ,
    "enum" ,
    identifier ,
    [ generic_parameters ] ,
    "{" ,
    { enum_variant } ,
    "}" ;
```

---

# 84. Enum variant

```ebnf
enum_variant =
    identifier ,
    [
          "(" , [ type_list ] , ")"
        | "{" , { struct_field } , "}"
    ] ,
    [ "," ] ;
```

Exemplo:

```text
enum Packet {
    TCP(TcpPacket),
    UDP(UdpPacket),
    None,
}
```

---

# 85. Trait declaration

```ebnf
trait_declaration =
    visibility ,
    "trait" ,
    identifier ,
    [ generic_parameters ] ,
    "{" ,
    { trait_item } ,
    "}" ;
```

---

# 86. Trait method

```ebnf
trait_item =
    function_signature , ";" ;
```

---

# 87. Function signature

```ebnf
function_signature =
    [ "async" ] ,
    "fn" ,
    identifier ,
    [ generic_parameters ] ,
    "(" ,
    [ parameter_list ] ,
    ")" ,
    [ "->" , type ] ;
```

---

# 88. Impl

```ebnf
impl_declaration =
    "impl" ,
    [ generic_parameters ] ,
    type ,
    [
        "for" ,
        type
    ] ,
    "{" ,
    { impl_item } ,
    "}" ;
```

Isso permite:

```text
impl Packet {
}
```

e:

```text
impl Serializable for Packet {
}
```

---

# 89. Impl item

Inicialmente:

```ebnf
impl_item =
    function_declaration ;
```

---

# 90. Generics

```ebnf
generic_parameters =
    "<" ,
    generic_parameter ,
    { "," , generic_parameter } ,
    [ "," ] ,
    ">" ;
```

---

# 91. Generic parameter

Inicialmente:

```ebnf
generic_parameter =
    identifier ,
    [ ":" , trait_bounds ] ;
```

---

# 92. Trait bounds

```ebnf
trait_bounds =
    path ,
    { "+" , path } ;
```

Exemplo:

```text
fn encode<T: Serializable + Send>(value: T) {
}
```

---

# 93. Generic arguments

```ebnf
generic_arguments =
    "<" ,
    type ,
    { "," , type } ,
    [ "," ] ,
    ">" ;
```

Exemplo:

```text
Vec<Packet>
Result<Packet, NetworkError>
```

---

# 94. Tipos

```ebnf
type =
      path_type
    | reference_type
    | pointer_type
    | array_type
    | slice_type
    | tuple_type
    | optional_type
    | function_type ;
```

---

# 95. Path type

```ebnf
path_type =
    path ,
    [ generic_arguments ] ;
```

---

# 96. Paths

```ebnf
path =
    identifier ,
    { "::" , identifier } ;
```

Exemplo:

```text
std::net::TcpStream
```

Decisão importante:

Para namespace/module paths usaremos:

```text
::
```

e para membros de valores:

```text
.
```

Assim:

```text
std::net::TcpStream
```

versus:

```text
socket.close()
```

---

# 97. Referências

```ebnf
reference_type =
    "&" ,
    [ "mut" ] ,
    type ;
```

Exemplos:

```text
&Packet
&mut Packet
```

---

# 98. Raw pointers

```ebnf
pointer_type =
    "*" ,
    [ "mut" ] ,
    type ;
```

Exemplos:

```text
*u8
*mut u8
```

---

# 99. Arrays

```ebnf
array_type =
    "[" ,
    type ,
    ";" ,
    expression ,
    "]" ;
```

Exemplo:

```text
[u8; 32]
```

O tamanho deverá ser compile-time evaluable.

---

# 100. Slices

```ebnf
slice_type =
    "[" ,
    type ,
    "]" ;
```

Exemplo:

```text
[u8]
```

---

# 101. Tuple types

```ebnf
tuple_type =
    "(" ,
    type ,
    "," ,
    [ type , { "," , type } ] ,
    [ "," ] ,
    ")" ;
```

---

# 102. Unit

```text
()
```

será o tipo `unit`.

---

# 103. Never

```text
never
```

poderá ser palavra reservada ou type identifier especial.

Posteriormente podemos adotar:

```text
!
```

como tipo never, mas não no primeiro parser.

---

# 104. Optional

```ebnf
optional_type =
    type ,
    "?" ;
```

Exemplo:

```text
Packet?
```

Internamente:

```text
Option<Packet>
```

---

# 105. Ambiguidade do `?`

Em tipos:

```text
Packet?
```

significa optional.

Em expressões:

```text
connect()?
```

significa propagação.

O contexto gramatical resolve a diferença.

---

# 106. Function types

```ebnf
function_type =
    "fn" ,
    "(" ,
    [ type_list ] ,
    ")" ,
    [ "->" , type ] ;
```

Exemplo:

```text
fn(Packet) -> Result<(), Error>
```

---

# 107. Type list

```ebnf
type_list =
    type ,
    { "," , type } ,
    [ "," ] ;
```

---

# 108. Constantes

```ebnf
const_declaration =
    visibility ,
    "const" ,
    identifier ,
    ":" ,
    type ,
    "=" ,
    expression ,
    ";" ;
```

Exemplo:

```text
const DEFAULT_PORT: u16 = 8080;
```

---

# 109. Use declarations

```ebnf
use_declaration =
    "use" ,
    path ,
    [ use_suffix ] ,
    ";" ;
```

---

# 110. Import simples

```text
use std::net::TcpStream;
```

---

# 111. Alias

```ebnf
use_suffix =
      "as" , identifier
    | "::" , use_group ;
```

Exemplo:

```text
use std::net::TcpStream as Stream;
```

---

# 112. Import groups

Posteriormente:

```text
use std::net::{TcpStream, TcpListener};
```

Pode ser implementado depois do import simples.

---

# 113. Unsafe block

```ebnf
unsafe_expression =
    "unsafe" ,
    block ;
```

Exemplo:

```text
unsafe {
    ptr.write(value);
}
```

---

# 114. Extern

```ebnf
extern_declaration =
    "extern" ,
    string_literal ,
    "{" ,
    { extern_item } ,
    "}" ;
```

Exemplo:

```text
extern "C" {
    fn malloc(size: usize) -> *mut u8;
}
```

---

# 115. Extern item

```ebnf
extern_item =
    visibility ,
    function_signature ,
    ";" ;
```

---

# 116. Async

```text
async fn fetch() -> Packet {
}
```

Parser:

```ebnf
async_function =
    "async" ,
    function_declaration ;
```

Na implementação, é melhor tratar `async` como modificador da declaração de função.

---

# 117. Await

```text
let packet = await receive();
```

`await` será operador prefixo.

---

# 118. Spawn

Forma inicial:

```text
spawn process(packet);
```

Gramática:

```ebnf
spawn_expression =
    "spawn" ,
    expression ;
```

---

# 119. Scope

Forma inicial:

```text
scope {
    spawn a();
    spawn b();
}
```

Gramática:

```ebnf
scope_expression =
    "scope" ,
    block ;
```

---

# 120. Concorrência no parser

O parser apenas representa:

```text
SpawnExpression
ScopeExpression
AwaitExpression
```

A semântica de structured concurrency será responsabilidade de fases posteriores.

---

# 121. Attributes

Não serão parte central do MVP, mas reservaremos:

```text
@
```

Exemplos futuros:

```text
@repr(C)
struct Header {
}
```

```text
@packed
struct Header {
}
```

---

# 122. Attribute grammar futura

```ebnf
attribute =
    "@" ,
    identifier ,
    [
        "(" ,
        [ argument_list ] ,
        ")"
    ] ;
```

Itens poderão possuir:

```ebnf
item =
    { attribute } ,
    item_body ;
```

O lexer deverá reconhecer `@` desde o início.

---

# 123. Semicolon rules

Declarações como:

```text
struct
enum
trait
impl
fn
```

não exigem `;` depois do bloco.

Statements comuns exigem.

Exemplo:

```text
let x = 10;
send(x);
return;
```

---

# 124. Tail expressions

Dentro de blocos:

```text
{
    let x = 10;
    x + 1
}
```

a última expressão sem `;` é retornada.

Com:

```text
{
    let x = 10;
    x + 1;
}
```

o resultado é `unit`.

---

# 125. Exemplo completo

```text
use std::net::TcpListener;

const DEFAULT_PORT: u16 = 8080;

struct Server {
    listener: TcpListener,
}

impl Server {
    fn start(&mut self) -> Result<(), NetworkError> {
        scope {
            loop {
                let client = self.listener.accept()?;

                spawn handle(client);
            }
        }

        Ok(())
    }
}

fn handle(mut client: TcpStream)
    -> Result<(), NetworkError>
{
    let mut buffer: [u8; 4096] = [0; 4096];

    loop {
        let count = client.read(&mut buffer)?;

        if count == 0 {
            break;
        }

        client.write(&buffer[0..count])?;
    }

    Ok(())
}

fn main() -> Result<(), Error> {
    let listener = TcpListener.bind(DEFAULT_PORT)?;

    let mut server = Server {
        listener: listener,
    };

    server.start()?;

    Ok(())
}
```

---

# 126. AST mínima necessária

O parser v0.1 deverá conseguir produzir aproximadamente:

```text
SourceFile

Item
├── FunctionDeclaration
├── StructDeclaration
├── EnumDeclaration
├── TraitDeclaration
├── ImplDeclaration
├── ConstDeclaration
└── UseDeclaration
```

Expressions:

```text
Expression
├── LiteralExpression
├── IdentifierExpression
├── BinaryExpression
├── UnaryExpression
├── AssignmentExpression
├── CallExpression
├── MemberExpression
├── IndexExpression
├── BlockExpression
├── IfExpression
├── MatchExpression
├── LoopExpression
├── ArrayExpression
├── TupleExpression
├── StructExpression
├── AwaitExpression
├── SpawnExpression
├── ScopeExpression
└── UnsafeExpression
```

Statements:

```text
Statement
├── LetStatement
├── ReturnStatement
├── BreakStatement
├── ContinueStatement
├── DeferStatement
└── ExpressionStatement
```

---

# 127. Tokens necessários

O lexer deverá reconhecer ao menos:

```text
IDENTIFIER
INTEGER_LITERAL
FLOAT_LITERAL
STRING_LITERAL
CHAR_LITERAL
BYTE_LITERAL
```

Keywords:

```text
AS
ASYNC
AWAIT
BREAK
CONST
CONTINUE
DEFER
ELSE
ENUM
EXTERN
FALSE
FN
FOR
IF
IMPL
IN
LET
LOOP
MATCH
MUT
PUB
RETURN
SCOPE
SPAWN
STRUCT
TRAIT
TRUE
UNSAFE
USE
WHILE
```

Pontuação:

```text
(
)
{
}
[
]

,
;
:
::

.
..
..=

+
-
*
/
%

=
==
!=

<
<=
>
>=

&&
||
!

&
|
^
~
<<
>>

+=
-=
*=
/=
%=

&=
|=
^=
<<=
>>=

-> 
=>
?

@
```

---

# 128. Source locations

Todo token deverá armazenar:

```text
file
start_offset
end_offset
line
column
```

Idealmente:

```text
SourceSpan {
    file_id,
    start,
    end
}
```

Linha e coluna podem ser calculadas ou armazenadas separadamente.

O objetivo é suportar diagnostics de alta qualidade.

---

# 129. Trivia

O lexer deverá distinguir semanticamente:

```text
tokens
```

de:

```text
comments
whitespace
```

Para o parser, trivia pode ser ignorada.

Para formatter e ferramentas, deverá ser possível preservá-la.

Isso sugere futuramente separar:

```text
Lexer
```

de uma estrutura de:

```text
SyntaxToken
```

com trivia associada.

---

# 130. Error recovery

O parser não deverá abortar no primeiro erro.

Deve tentar sincronizar em tokens como:

```text
;
}
fn
struct
enum
trait
impl
let
```

Assim:

```text
error 1
error 2
error 3
```

podem ser reportados em uma única compilação.

---

# 131. Invalid nodes

AST ou árvore sintática poderá conter:

```text
ErrorExpression
ErrorStatement
ErrorItem
```

para permitir continuar parsing mesmo após erros.

---

# 132. Parser strategy

A estratégia recomendada é híbrida.

Para declarações e statements:

**Recursive Descent**

Para expressões:

**Pratt Parser**

Isso torna precedência e associatividade muito mais simples de manter.

---

# 133. Arquitetura recomendada

```text
Source code
    ↓
Lexer
    ↓
Token stream
    ↓
Parser
    ├── recursive descent
    └── Pratt expression parser
    ↓
Syntax Tree / AST
```

---

# 134. CST versus AST

Recomendação:

Não construir apenas uma AST destrutiva.

Idealmente teremos:

```text
Concrete Syntax Tree
        ↓
       AST
```

A CST preserva:

- tokens;
- comments;
- delimitadores;
- source spans.

Isso será útil para:

- formatter;
- language server;
- refactoring;
- diagnostics;
- IDE tooling.

Entretanto, o primeiro protótipo pode começar diretamente com AST.

---

# 135. MVP real do parser

Apesar desta especificação ser maior, a primeira milestone deve aceitar apenas:

```text
fn main() {
    let x = 10;
    let y = 20;

    if x < y {
        return;
    }
}
```

Depois expandiremos em ordem.

---

# 136. Ordem de implementação gramatical

## Milestone 1

```text
tokens
literals
identifiers

let

binary operators
unary operators

function declaration
function call

block
return
```

---

## Milestone 2

```text
if
else
while
loop
for

arrays
indexing
member access
```

---

## Milestone 3

```text
types
structs
struct literals
impl
methods
```

---

## Milestone 4

```text
enum
match
patterns
Option
Result
?
```

---

## Milestone 5

```text
references
raw pointers
unsafe
```

---

## Milestone 6

```text
traits
generics
```

---

## Milestone 7

```text
async
await
spawn
scope
```

---

# 137. Primeira gramática implementável

A gramática mínima do primeiro parser pode ser reduzida a:

```ebnf
source_file =
    { function } ,
    EOF ;

function =
    "fn" ,
    identifier ,
    "(" ,
    ")" ,
    block ;

block =
    "{" ,
    { statement } ,
    "}" ;

statement =
      let_statement
    | return_statement
    | expression_statement
    | if_statement ;

let_statement =
    "let" ,
    [ "mut" ] ,
    identifier ,
    "=" ,
    expression ,
    ";" ;

return_statement =
    "return" ,
    [ expression ] ,
    ";" ;

if_statement =
    "if" ,
    expression ,
    block ,
    [ "else" , block ] ;

expression_statement =
    expression ,
    ";" ;
```

Expressions:

```ebnf
expression =
    assignment ;

assignment =
    logical_or ,
    [ "=" , assignment ] ;

logical_or =
    logical_and ,
    { "||" , logical_and } ;

logical_and =
    equality ,
    { "&&" , equality } ;

equality =
    comparison ,
    { ( "==" | "!=" ) , comparison } ;

comparison =
    additive ,
    { ( "<" | "<=" | ">" | ">=" ) , additive } ;

additive =
    multiplicative ,
    { ( "+" | "-" ) , multiplicative } ;

multiplicative =
    unary ,
    { ( "*" | "/" | "%" ) , unary } ;

unary =
      ( "!" | "-" ) , unary
    | call ;

call =
    primary ,
    { "(" , [ arguments ] , ")" } ;

primary =
      integer
    | float
    | string
    | "true"
    | "false"
    | identifier
    | "(" , expression , ")" ;
```

Essa é a parte que devemos implementar primeiro.

---

# 138. Exemplo-alvo da primeira milestone

O compilador deverá conseguir parsear:

```text
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn main() {
    let x = 10;
    let y = 20;

    let result = add(x, y);

    if result > 20 {
        print(result);
    }
}
```

E produzir uma árvore equivalente a:

```text
SourceFile
├── Function add
│   ├── Parameter a: i32
│   ├── Parameter b: i32
│   └── Block
│       └── Return
│           └── Binary(+)
│               ├── Identifier(a)
│               └── Identifier(b)
│
└── Function main
    └── Block
        ├── Let x
        │   └── Integer(10)
        ├── Let y
        │   └── Integer(20)
        ├── Let result
        │   └── Call add
        │       ├── Identifier(x)
        │       └── Identifier(y)
        └── If
            ├── Binary(>)
            │   ├── Identifier(result)
            │   └── Integer(20)
            └── Block
                └── Call print
                    └── Identifier(result)
```

Quando conseguirmos produzir corretamente essa estrutura, teremos o primeiro frontend funcional da linguagem.

---

# 139. Decisões congeladas pela Grammar v0.1

A partir desta especificação, consideramos provisoriamente estabelecidos:

- `{}` para blocos;
- `;` para statements;
- `fn` para funções;
- `let` e `let mut`;
- `struct`;
- `enum`;
- `trait`;
- `impl`;
- `match`;
- `Result<T, E>`;
- `T?`;
- `&T`;
- `&mut T`;
- `*T`;
- `*mut T`;
- `async`;
- `await`;
- `spawn`;
- `scope`;
- `unsafe`;
- `use`;
- `::` para paths;
- `.` para members;
- `?` para propagação;
- `->` para retorno;
- `=>` para match;
- imutabilidade por padrão.

Qualquer alteração relevante nesses pontos deverá atualizar esta especificação.

---

# 140. Próxima etapa

Com esta gramática, a implementação pode oficialmente começar.

Primeiro componente:

```text
SourceManager
     ↓
Lexer
     ↓
Token
```

O lexer não deve conhecer AST, tipos, ownership ou LLVM.

Sua responsabilidade é somente:

```text
characters → tokens
```

A primeira implementação deve ser testada contra:

```text
fn main() {
    let x = 10 + 20 * 3;
}
```

e produzir corretamente a sequência:

```text
FN
IDENTIFIER(main)
LEFT_PAREN
RIGHT_PAREN
LEFT_BRACE

LET
IDENTIFIER(x)
EQUAL
INTEGER(10)
PLUS
INTEGER(20)
STAR
INTEGER(3)
SEMICOLON

RIGHT_BRACE
EOF
```