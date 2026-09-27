# UVScript

UVScript (`.uvs`) is UniVex's scripting language: one text file per entity node that says what the
node does. It replaces the visual script graph, which was hard to keep in step with the engine.

## What it has to beat

The comparison point is GDScript (Godot 4.x). The weaknesses that shaped UVScript:

| GDScript today | UVScript |
|---|---|
| Typing is optional. Untyped code is slow, because every operation resolves the type at run time. | Every value has a static type. It is inferred when left out (`let n = 3`), and checked before the game runs. |
| Interpreted. Hot loops are about half the speed of C#. | Compiled: a register bytecode VM in the editor (instant reload), and C++23 generated from the same checked tree for release builds. |
| No tuples, and no generics on user types. | Tuples (`let (a, b) = pair()`) and typed collections (`list[int]`, `map[str, Node3D]`). |
| Signals are connected by name as strings; typos show up at run time. | Events are blocks (`on body_entered(other):`). The compiler checks the name and the parameters against the node kind. |
| `await` needs a signal or a timer object. | `wait 0.5 s`, `wait until is_on_floor`, `wait next_frame`. The node pauses; nothing is allocated. |
| Numbers carry no units, so seconds vs frames and degrees vs radians get mixed up. | Unit literals (`2 s`, `150 ms`, `90 deg`, `3 m`). Angles are converted to radians at compile time. |
| The built-in editor has no rename refactoring. | Every symbol resolves to one declaration, so the editor can rename safely. The node's own name is a symbol too. |

## A script

```
entity Player : CharacterBody3D

export speed: float = 6.0
export jump_height = 1.2 m
var jumps = 0

on ready:
    print("{name} is ready")

on tick(dt):
    let move = input.axis("left", "right")
    velocity.x = move * speed
    if is_on_floor and input.pressed("jump"):
        velocity.y = sqrt(2.0 * gravity * jump_height)
        jumps += 1

on body_entered(other: Node3D):
    wait 0.5 s
    other.hide()

fn heal(amount: int) -> bool:
    health += amount
    return health >= max_health
```

- **`entity Name : Kind`** is the first line. It says which node kind the script drives. Its
  properties (`velocity`, `is_on_floor`, ...) are in scope without `self.`.
- **`export`** shows a field in the Inspector. **`var`** keeps a value between frames. **`let`**
  is a local. **`const`** is fixed at compile time.
- **`on <event>`** runs when the event happens. **`fn`** declares a function.
- Blocks are set by indentation. `#` starts a comment.

## Grammar (v1)

```
file        := header? (member | NEWLINE)*
header      := 'entity' IDENT ':' IDENT NEWLINE
member      := field | handler | function
field       := ('export' | 'var' | 'const') IDENT (':' type)? ('=' expr)? NEWLINE
handler     := 'on' IDENT ('(' params? ')')? ':' block
function    := 'fn' IDENT '(' params? ')' ('->' type)? ':' block
params      := param (',' param)*
param       := IDENT (':' type)?
type        := IDENT ('[' type (',' type)* ']')?
block       := NEWLINE INDENT statement+ DEDENT
statement   := 'let' IDENT (':' type)? '=' expr NEWLINE
             | 'if' expr ':' block ('elif' expr ':' block)* ('else' ':' block)?
             | 'while' expr ':' block
             | 'for' IDENT 'in' expr ':' block
             | 'return' expr? NEWLINE | 'break' NEWLINE | 'continue' NEWLINE | 'pass' NEWLINE
             | 'wait' expr NEWLINE
             | expr (assign_op expr)? NEWLINE
assign_op   := '=' | '+=' | '-=' | '*=' | '/='
expr        := or ;  or := and ('or' and)* ;  and := not ('and' not)*
not         := 'not' not | compare
compare     := range (('=='|'!='|'<'|'<='|'>'|'>=') range)*
range       := sum ('..' sum)?
sum         := product (('+'|'-') product)*
product     := unary (('*'|'/'|'%') unary)*
unary       := '-' unary | postfix
postfix     := primary ('.' IDENT | '(' args? ')' | '[' expr ']')*
primary     := NUMBER UNIT? | STRING | 'true' | 'false' | 'none' | IDENT | '(' expr ')'
```

## Module

- **What it does:** turns `.uvs` source into a checked tree, then into something that runs.
- **Why it is separate:** the old `Scripting` module interprets node graphs. Its IR is
  "execute node N" and cannot hold a text program. It is removed once UVScript runs every script.
- **Depends on:** nothing but the standard library, for now.
- **Exposes:**
  - `ParseUVScriptUVE` (source text → `UVScriptFileUVE` plus diagnostics);
  - the AST types in `uvscript_ast_uve.h`.

## Status

1. **Lexer and parser (this change).** Indentation, unit literals, string interpolation, the full
   v1 grammar, and diagnostics with line and column. Parsing recovers at the next line, so one
   mistake does not hide the rest.
2. **Next:** type checker and register VM.
3. **Then:** engine binding (script slot, events, exported fields in the Inspector), and removing
   the graph scripting.
4. **Last:** C++23 output for release builds.
