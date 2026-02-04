# skforth

skforth is a lightweight Forth interpreter written in C.
The name comes from “seak Forth”; a small, experimental Forth runtime designed for **simplicity**, **flexibility**, and **extensibility**.

skforth intentionally prioritizes explicit control flow and simplicity over standard compliance.

## It implements a classic stack-based Forth model, including:

- BLOCKS (persistent storage)
- loading `.fs` files inside the REPL
- CPU Instruction Construction layer (ICL)
- main stack
- return stack
- dictionary of words
- compile mode and interpret mode
- basic control flow
- user-defined words
- basic I/O and debugging helpers

## Features

### Core model

- `INTERPRET` \ `COMPTIME` modes
- `:` `;` word definition
- `IF ... ELSE ... THEN`
- `BEGIN ... WHILE ... REPEAT`
- `EXIT`
- `IMMEDIATE`
- `LITERAL`

`LITERAL` pushes a value at runtime.
When used during compilation, it emits code that will push the value on the stack when the word is executed.

Some words (such as `TYPE`) are aware of the current mode and may emit code when used during compilation.

> **POSTPONE is not implemented**.
> The existing compile-time model makes it unnecessary for most use cases.

> Memory in skforth is cell-addressed by default.
> Byte-level access is explicit and opt-in via BYTE+, BYTES, and COPY-BYTES.

### Memory Model & Configuration

skforth uses a **fully virtual-memory-based memory model**.

All major memory regions are allocated explicitly using `mmap(2)` and are independent, contiguous virtual memory areas:

- main data stack
- return stack
- control-flow stack
- dictionary
- code space
- data space

No traditional heap allocator (`malloc`) is used for the Forth memory model itself.

BLOCKS are planned as mmap-backed persistent storage, aligned with the traditional Forth block model.

### Configuration via `config.fs`

On startup, skforth loads a configuration file written in **Forth itself**:

```text
$HOME/.config/skforth/config.fs
```

If the file does not exist, skforth automatically creates it with default values.

The configuration file is **executed by a minimal Forth interpreter** and is expected to **push configuration values onto the stack in a predefined order.**

#### Default `config.fs`

```Forth
1024        ( BLOCK_SIZE ) 
64          ( NUM_BLOCKS ) 
32          ( STACK_SIZE ) 
5000        ( MAX_WORDS ) 
1024 64 *   ( MAX_CODE_SPACE ) 
256         ( CF_STACK -- This is for the control flow stack ) 
1024        ( DATA_SIZE ) 
1024 64 *   ( MAX_BLOB_SPACE -- Used for string allocation and ICL's instructions )
```

After execution, skforth pops the values from the stack and uses them to initialize the runtime.

---

#### Configuration parameters

| Name | Description |
|------|-------------|
|BLOCK_SIZE	| Size (in bytes) of a single block |
|INITIAL_BLOCKS	| Initial number of blocks|
|STACK_SIZE	| Main data stack size (cells)|
|MAX_WORDS	| Maximum number of dictionary entries|
|MAX_CODE_SPACE	| Size of code space (cells)|
|CF_STACK   | Control-flow stack depth|
|DATA_SIZE	Initial data space size (cells)|

All sizes related to stacks and data/code spaces are expressed in **cells**, where one cell is the native machine word (`u64`).

The data space can be dynamically grown at runtime using:

```Forth
n GROW
```

Memory usage can be inspected interactively using:

```Forth
.memstats
```

Example output:

```Forth
skforth stats:
 NAME | MAX_BYTES | CURRENT_BYTES | MAX_CELLS | CURRENT_CELLS
[STACK] 8192 0 1024 0
[DATA] 40960 16 5120 2
[CODE] 524288 <no info> 65536 <no info>
[RSTACK] 8192 0 1024 0
[CFSTACK] 2048 0 256 0
```
---

## BLOCKS (persistent storage)

skforth includes a **BLOCKS** subsystem inspired by the traditional Forth block model.

Unlike file-based source loading, BLOCKS provide persistent, addressable storage
that can be executed, edited, and managed explicitly from within the system.

BLOCKS are backed by a memory-mapped file and are designed to integrate naturally
with skforth’s virtual-memory-based architecture.

### Storage model

Blocks are stored in: 

```text
$HOME/.config/skforth/BLOCKS.blk
```
At startup, this file is:

- resized to **BLOCK_SIZE * INITIAL_BLOCKS**
- mapped into memory using `mmap(2)` with `MAP_SHARED`

All block addresses returned by the system point **directly into this mapped region**.

There is currently **no block cache** — blocks are accessed directly in memory.

### Configuration

BLOCKS are configured via `config.fs`:

```Forth
1024    ( BLOCK_SIZE )
64      ( INITIAL_BLOCKS )
```

### Mental model

The BLOCKS system is intentionally explicit and separates **editing** from
**persistence**.

Think in terms of three layers:

1. **Block storage**
    The memory-mapped BLOCKS.blk file

2. **Editor buffer**
    A temporary external file used by an editor (currently `nvim`)

3. **Commit control**
Explicit words that decide when changes are written back to block storage

Editing a block does not automatically imply saving it.

### WORDS for BLOCKS

```Forth
BLOCK ( u -- addr )
```
Returns the address of block `u`.

- The address points directly into the memory-mapped block store
- Each block is `BLOCK_SIZE` bytes long

---
```Forth
LOAD ( u -- )
```
Loads and interprets block `u` as Forth source.

This allows blocks to be used as executable, persistent code units.

TIP: You can use this inside a block to call other blocks ;D

---
```Forth
EDIT ( u -- )
```
Edits block `u` using an external editor.

Current behavior:

- the contents of block `u` are copied into a temporary file:
```text
$HOME/.config/skforth/block_editor.fs
```
- an external editor is launched
- changes remain in the editor buffer until explicitly committed

Edits are **not saved automatically**.

---
```Forth
UPDATE ( -- )
```
Marks the currently edited block as modified.

This records intent, but does not write anything to disk.

---
```Forth
FLUSH ( -- )
```
Writes the editor buffer back into the memory-mapped block store.

After `FLUSH`, changes become persistent in `BLOCKS.blk`.

---
```Forth
LIST ( n -- )
```
Prints the contents of BLOCK n

---
```Forth
LISTALL ( -- )
```

Prints the contents of all blocks

---
```Forth
ERASE ( n -- )
```
Erase the contents of Block n (Fills with 0's)

---

Editing workflow

To preserve changes made during editing:

```Forth
1 EDIT  \ you enter in editor mode and change the block
UPDATE  \ after you exit the editor, UPDATE to mark block as dirty
FLUSH   \ save contents of EDITOR-BLOCK to dirty BLOCK
```
If you edit another block without flushing, previous edits may be lost.

This behavior is intentional and reflects skforth’s preference for
explicit control over implicit persistence.

---
**Notes on implementation**

- `BLOCKS.blk` is accessed exclusively through `mmap`
- external editors operate on a separate temporary file
- after editor exit, file descriptors are closed and reopened to force
inode and page-cache revalidation (mixing `mmap` and external writes)

---
**Current limitations**

- only one external editor buffer exists
- no `BUFFER` word yet
- no in-memory block buffering

---
## Native CPU instructions (experimental)

skforth allows you to define and execute **raw CPU instructions directly from Forth**.

This is done by encoding instructions as data: each “assembly word” pushes the
corresponding instruction bytes (prefixes, REX, opcode, ModR/M, SIB, displacement,
immediates) into code space and executes them.

This is not a traditional assembler.

- there is no text-based syntax
- no labels
- no instruction parsing
- no automatic instruction selection

Instead, skforth gives you **explicit, byte-level control** over instruction
encoding.

If you know exactly what instruction you want, **you do not need an assembler
to decide for you**.

Think of this as a Forth-level instruction builder:

- Forth words represent instruction components (REX, ModR/M, registers, opcodes)
- instruction bytes are emitted explicitly
- Forth logic can be freely mixed with instruction generation

This makes it possible to build small pieces of machine code while still using
Forth for control flow, data manipulation, and abstraction.

### Instruction Construction Layer (ICL)

The native CPU instruction support in skforth is implemented as an
**Instruction Construction Layer (ICL)**.

This is **not** an assembler, an IR, or a traditional JIT.

Instead, it is a small domain-specific language embedded in Forth that
allows **explicit construction and direct execution of native machine
instructions**.

Instructions are built as data:
- each instruction component (REX, opcode, ModR/M, immediate, etc.) is
  represented by a Forth word
- instruction bytes are packed explicitly, byte by byte
- no parsing, labels, relocation, or instruction selection is performed

If you know exactly what instruction you want to emit, skforth does not
“decide” anything for you — it simply executes what you construct.

### Assembly mode

Assembly words are typically used inside an asm: / ;asm block:

```Forth
asm:
    ... instruction words ...
;asm
```

`asm:` switches to compile mode and sets numeric base to HEX

`;asm` restores DEC base, emits the generated code, and returns to interpret mode

#### x86-64 support

An experimental x86-64 implementation lives in:

```text
arch/x86_64.fs
```

It provides:

- register definitions (`%rax … %r15`)
- REX prefix helpers
- ModR/M helpers
- opcode words
- higher-level instruction words


#### Instruction structure

Instructions are built according to the x86-64 encoding model:

```text
[prefix] [REX] [opcode] [ModRM] [optional SIB] [displacement] [immediate]
```

Extended registers (`%r8`–%r15) are masked so that:
- only the lower 3 bits are used for ModR/M encoding
- the high bit is used to select the correct REX prefix

This reduces code branching and keeps instruction builders simple.

#### Address and immediate handling

64-bit immediates and addresses are emitted byte-by-byte explicitly.

For example, helper words such as `|addr` simply split a 64-bit value into
individual bytes and emit them in the correct order.

There is no relocation or symbol resolution; addresses must be known at
generation time.

#### Status and limitations

This subsystem is **still under development**.

- not all instructions are implemented
- some instructions may be incomplete or untested
- there is no support for labels or jumps yet
- instruction correctness is the responsibility of the user

The design is intentionally minimal and transparent.

#### Portability

The ideas used here are **not x86-64 specific**.

The same approach can be applied to other processors by defining:

- register words
- instruction encoding helpers
- opcode emitters

In other words, you can do exactly the same thing for **your own architecture**,
using Forth as a thin, explicit layer over raw machine code.

---

## WORDS

### Stack and arithmetic primitives

| Word  | Stack effect      | Description   |
|-------|-------------------|---------------|
| dup	|x -- x x	        | duplicate top |
| 2dup	|x y -- x y x y	    |duplicate two  | 
| drop	|x --	            |drop top       |
| 2drop	|x y --	            |drop two       |
| swap	|x y -- y x         |	swap top two|
| 2swap	|a b c d -- c d a b	|swap two pairs |
| over	|x y -- x y x	    |copy second    |
| 2over	|a b c -- a b c a	|copy third     |
| rot	|a b c -- c a b	    |rotate         |
| -rot	|a b c -- b c a	    |reverse rotate |
| +	    |a b -- a+b	        |addition       |
| -	    |a b -- a-b	        |subtraction    |
| *	    |a b -- a*b	        |multiplication |
| /mod	|a b -- (a%b) (a/b)	|divide and mod |
| 1-	|n -- n-1	        |decrement      |

### Comparison and boolean primitives

| Word	| Stack effect	| Description |
|-------|---------------|-------------|
|=	    |a b -- flag	| equal       |
|<	    |a b -- flag	| less than   |
|>	    |a b -- flag	| greater than|
|>=	    |a b -- flag	|greater or equal|
|0=	    | a -- flag	    | is zero? |
|0>	    | a -- flag	    | greater than zero |
|0<>	| a -- flag	    | not zero |
|or / | |a b -- a       | or |
|and / &&|	a b -- a&&b	| logical and |

### Stack inspection

| Word	| Description |
|-------|--------------|
| .s	| prints stack content |
| depth	| returns stack depth |
| ddepth | returns data depth |

### Output

| Word | Description |
|-----|---------------|
| .    | print top stack value |
| cr   | newline |
| TYPE | print string at (addr len) |
| ."  | print string literal (works in both interpret and compile modes) |

### Control flow

| Word	                    | Mode	        | Description |
|---------------------------|---------------|---------------|
| IF ... ELSE ... THEN	    | compile-time	| conditional execution |
| BEGIN ... WHILE ... REPEAT| compile-time	| loop |
| EXIT	                    | run-time	    | exit current word |

### Definitions & Compilation

| Word	    | Description |
|-----------|--------------|
| : ;	    | define a new word |
| IMMEDIATE	| mark a word as immediate |
| COMPTIME	| switch to compile mode |
| INTERPRET	| switch to interpret mode |
| INTERPRET-TOKEN | interpret a token manually |
| PARSE-NAME	| parse next token from input |
| PARSE-STRING | parse string until `"`, excluding the `"` |
| SOURCE	| returns current input line |
| >IN	| returns current input cursor index |

### Memory primitives

Raw data space control

The interpreter provides a raw data space you can control manually.

| Word	 | Stack effect	| Description |
|--------|--------------|--------------|
| HERE	 | -- addr	    | returns current data pointer (address) |
| ALLOC	 | n --	        | allocate n cells in data space |
| GROW	 | n --	        | increase data space capacity by n cells |
| clear.d|	--	        | free and reset data space |
| constvar: | n "name" -- | reserve a cell and assign it as a word. example : `420 constvar: myvar`| 
| s"     | string" -- addr len  | allocate a ascii string on BLOB-HERE |

`HERE` returns the address of the next free cell, and `ALLOC` increments the data pointer by a number of cells.

### Cell arithmetic helpers

| Word | Stack effect | Description |
|------|--------------|-------------|
| CELLS | bytes -- cells | convert bytes to cells |
| CELL+ | addr -- addr+cell | advance pointer by one cell |
| BYTE+ | addr -- addr+byte | advance pointer by one byte |
| BYTES | n -- n*byte | convert cells to bytes |
| ceil-cells | bytes -- cells | ceil division by cell size |

### Memory copy

| Word | Stack effect | Description |
|------|--------------|-------------|
| `COPY-CELLS` | dest src len | copy len cells |
| `COPY-BYTES` | dest src len | copy len bytes |

### Byte operations

| Word | Stack effect | Description |
|------|--------------|-------------|
| `b@` | addr -- byte | read a byte |
| `b!` | addr byte -- | write a byte |

---

## `see` (word inspection)

`see` prints the definition of a word:

```forth
see word-name
```
--- 

- The bootstrap file `bootstrap.fs` **adds additional utilities**:
( You can also INCLUDE the `std.fs` for further utilities )

var:

```text
Creates a variable in data space.

var: x
```

."

```text
skforth> : test ." hello " cr ;
skforth> test
hello
```

see

```text
skforth> see var:
: var:
  PARSE-NAME
  CREATE
  LIT 0
  ,
;
```

buf:

```text
Creates a buffer with length and capacity fields.

buf: mybuf
```

It allocates:

```text
buf-cap (capacity)

buf-len (current length)

buf-data (data pointer)
```
--- 

- Debugging & Diagnostics

| Word	| Description |
|-------|-------------|
|.memstats	| print memory usage stats |
| words	| list defined words |
| clear.s | clear the main stack |

---

- Constants & helpers

`CELLSIZEBYTES` — size of a cell in bytes

`sqr`, `nip`, `tuck`, `<>`, etc.

---

- File inclusion

You can load `.fs` files directly from the REPL using:

```text
INCLUDE std.fs
```
This lets you extend the interpreter without modifying the C code.

--- 

# Getting Started

For a proper introduction to Forth (Gforth manual): <https://gforth.org/manual/>

## Building

```shell
make

Running
./build/skforth
```

It automatically loads `bootstrap.fs` and drops into a REPL:

```shell
Loading bootstrap.fs...

Welcome to skforth :D
skforth>
```

## Example Usage

```forth
skforth> 1 2 + .

3
```

- Create and use a variable:

```forth
var: x
420 x !
x @ .
```


- Buffer usage:

```forth
2 buf: mybuf
mybuf buf-cap .
mybuf buf-len .
mybuf buf-data .
```

You can also do raw memory on the data space with `HERE` and `ALLOC`
- `HERE` gives the address of available memory on data space
- `ALLOC` takes N as argument to increments the data space pointer (takes number of cells)

# Goals

skforth is intended as:

- a minimal Forth implementation
- a base for experimentation and expansion
- simply to have a Forth as I see fit

# Future Improvements

Possible next steps:

- implement native string literals (e.g. S")
- add file I/O
- improve error handling
- implement DO LOOP
- add standard library words expansion
