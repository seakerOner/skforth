# skforth

skforth is a lightweight Forth interpreter written in C.
The name comes from “seak Forth”; a small, experimental Forth runtime designed for **simplicity**, **flexibility**, and **extensibility**.

skforth intentionally prioritizes explicit control flow and simplicity over standard compliance.

## It implements a classic stack-based Forth model, including:

- main stack
- return stack
- dictionary of words
- compile mode and interpret mode
- basic control flow
- memory allocation and data space
- user-defined words
- basic I/O and debugging helpers
- loading `.fs` files inside the REPL

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

**POSTPONE is not implemented** - the current system makes it unnecessary.

> Memory in skforth is cell-addressed by default.
> Byte-level access is explicit and opt-in via BYTE+, BYTES, and COPY-BYTES.

---

- Stack and arithmetic primitives

| Word  | Stack effect      | Description   |
|-------|-------------------|---------------|
| dup	|x -- x x	        | duplicate top |
| 2dup	|x y -- x y x y	    |duplicate two  | 
| drop	|x --	            |drop top       |
| 2drop	|x y --	            |drop two       |
| swap	|x y -- y x         |	swap top two|
| 2swap	|a b c d -- c d a b	|swap two pairs |
| over|	|x y -- x y x	    |copy second    |
| 2over	|a b c -- a b c a	|copy third     |
| rot|	|a b c -- c a b	    |rotate         |
| -rot	|a b c -- b c a	    |reverse rotate |
| +	    |a b -- a+b	        |addition       |
| -	    |a b -- a-b	        |subtraction    |
| *	    |a b -- a*b	        |multiplication |
| /mod	|a b -- (a%b) (a/b)	|divide and mod |
| 1-	|n -- n-1	        |decrement      |

- Comparison and boolean primitives

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

- Stack inspection

| Word	| Description |
|-------|--------------|
| .s	| prints stack content |
| depth	| returns stack depth |
| ddepth | returns data depth |

- Output

| Word | Description |
|-----|---------------|
| .    | print top stack value |
| cr   | newline |
| TYPE | print string at (addr len) |
| ."  | print string literal (works in both interpret and compile modes) |

- Control flow

| Word	                    | Mode	        | Description |
|---------------------------|---------------|---------------|
| IF ... ELSE ... THEN	    | compile-time	| conditional execution |
| BEGIN ... WHILE ... REPEAT| compile-time	| loop |
| EXIT	                    | run-time	    | exit current word |

- Definitions & Compilation

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

- Memory primitives

Raw data space control

The interpreter provides a raw data space you can control manually.

| Word	 | Stack effect	| Description |
|--------|--------------|--------------|
| HERE	 | -- addr	    | returns current data pointer (address) |
| ALLOC	 | n --	        | allocate n cells in data space |
| GROW	 | n --	        | increase data space capacity by n cells |
| clear.d|	--	        | free and reset data space |
| constvar: | n "name" -- | reserve a cell and assign it as a word. example : `420 constvar: myvar`| 

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

---

## Byte operations

| Word | Stack effect | Description |
|------|--------------|-------------|
| `b@` | addr -- byte | read a byte |
| `b!` | addr byte -- | write a byte |

---

## `see` (word inspection)

`see` prints the definition of a word:

```forth
see word-name
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

You can also do raw memory allocation with `HERE` and `ALLOC`
- `HERE` gives the address of available memory on data space
- `ALLOC` takes N as argument to incremente the data space pointer (takes number of cells)

## Memory & Debugging

The interpreter provides some debugging tools:

`.s` — print stack contents

`.memstats` — print memory usage statistics

`words` — list defined words

# Project Structure

- C runtime (skforth.c)

- The core interpreter is implemented in C and includes:
    - Word dictionary
    - Execution loop
    - Token parsing
    - Built-in primitives
    - Memory management
    - Forth bootstrap (bootstrap.fs)
    - Defines additional high-level Forth words using the primitive vocabulary.

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
