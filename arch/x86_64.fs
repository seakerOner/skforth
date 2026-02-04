\
\ x86_64.fs — x86-64 MCL (Machine Contruction Layer) for skforth
\
\ This file defines x86-64 instructions as skforth "words", allowing you to
\ construct and execute raw machine code directly from Forth.
\
\ This is NOT a traditional assembler.
\ There is no text parsing, no labels, and no instruction selection.
\ Instead, instructions are encoded explicitly as data.
\
\ Each instruction is built using two stack cells:
\   - a base cell: packed instruction bytes + byte length
\   - a payload cell: raw u64 data (immediate or displacement)
\
\ The base cell stores:
\   [ byte_count | instruction_bytes... ]
\   - upper 8 bits  : number of instruction bytes
\   - lower 56 bits : packed opcode / prefixes / ModR/M / SIB
\
\ The payload cell stores:
\   - a raw 64-bit value (imm64, disp32, etc.)
\
\ Instruction bytes are assembled using |instr and related helpers.
\ Payload data is packed explicitly using |imm64.
\
\ Execution model:
\   asm:   begins instruction construction (compile-time)
\   ;asm   copies bytes + payload into executable memory and executes them
\
\ Instructions are copied into an executable byte buffer and invoked
\ directly via a function pointer. There is no intermediate representation
\ and no hidden transformation stage.
\
\ You can freely mix Forth control flow with instruction construction,
\ enabling dynamic code generation, JIT-style execution, and metaprogramming.
\
\ Instruction layout follows the x86-64 encoding model:
\   [prefix] [REX] [opcode] [ModRM] [SIB] [displacement] [immediate]
\
\ Extended registers (%r8–%r15) are masked to simplify REX handling.
\ Only the lower bits of registers are used when encoding ModR/M fields
\ (reg-low, reg-ext?, _add-rex-1op, _add-rex-2op).
\
\ This file is a work in progress.
\ Instructions may be incomplete, missing, or untested.
\
\ The ideas here are architecture-agnostic: the same approach can be used
\ to implement other CPUs by defining a different instruction encoder.
\ The goal is a hybrid Forth + raw machine code workflow with maximal control.
\
\ If you know exactly what you want the CPU to execute,
\ you do not need an assembler to decide for you.
\
\ Reference:
\   http://ref.x86asm.net/coder64.html
\



\ NOTE: this code is under development; instructions may be missing or untested.

: asm: ( -- ) \ this should be used to create only one instruction at a time
    COMPTIME
    HEX
    0 0 \ instruction base cell + payload (imm, disp) 
    ( VARIANT: base cell: 8 higher bits reserved for length in bytes              )
    (                   : 57 lower bits reserved for instructions                 )
    ( VARIANT:   payload: raw packed u64                                          )
    ( VARIANT: the order of base+payload must not be changed when ;asm is reached )
;

: ;asm ( -- )
   DEC 
   EXEC-CODE
   INTERPRET
;

: _base-len@ ( base -- len )
    DEC 56 rshift HEX
;

: _base-len+1 ( base -- base' )
    dup _base-len@ 1 +
    >R
    HEX 00FFFFFFFFFFFFFF and 
    R> DEC 56 lshift | HEX
;

: _base-bytes@ ( base -- bytes )
    00FFFFFFFFFFFFFF and
;

: _base-pack-byte ( base byte -- base ' )
    over _base-len@ 8 * lshift \ byte << (len * 8)
    >R dup _base-bytes@ R> |
;

: |instr ( base payload byte -- base' payload )
    >R swap R>
    _base-pack-byte
    _base-len+1
    swap 
;

: |imm64 ( base payload u64 -- base payload' )
    dup DEC  0 rshift HEX FF and |
    dup DEC  8 rshift HEX FF and |
    dup DEC 16 rshift HEX FF and |
    dup DEC 24 rshift HEX FF and |
    dup DEC 32 rshift HEX FF and |
    dup DEC 40 rshift HEX FF and |
    dup DEC 48 rshift HEX FF and |
        DEC 56 rshift HEX FF and |
;

HEX

: _REX.W 
    \ 64 bit operand size
    48 |instr
;

: _REX.WR 
    \ 64 bit operand size
    \ REX.W and REX.R combination (bits REG 8-15 first operand)
    4C |instr
;

: _REX.WB
    \ 64 bit operand size
    \ REX.W and REX.R combination (bits REG 8-15 second operand)
    49 |instr
;

: _REX.WX
    \ 64 bit operand size
    \ for memory address
    \ REX.W and REX.X combination (extra bit for SIB index, REG 8-16 on index)
    4A |instr
;

: _REX.WXB
    \ 64 bit operand size
    \ REX.W, REX.X and REX.B combination
    4B |instr
;

: _REX.WRB 
    \ 64 bit operand size
    \ REX.W, REX.R and REX.B combination
    4D |instr
;

: _REX.WRX
    \ 64 bit operand size
    \ REX.W, REX.R and REX.X combination
    4E |instr
;

: _REX.WRXB
    \ 64 bit operand size
    \ REX.W, REX.R, REX.X, REX.B combination
    4F |instr
;

: reg-low ( reg -- 3lower ) \ we extract the 3 lower bits for the actual flag
    7 and
;

: reg-ext? ( reg -- flag ) \ returns 0 if its a 
    8 and
;

: _add-rex-1op ( reg -- instruction )
    dup 
    reg-ext? 
   IF _REX.WB ELSE _REX.W THEN
;

: _add-rex-2op ( reg reg -- instruction )
    >R
    reg-ext?
    IF
        R> 
        reg-ext? IF _REX.WRB ELSE _REX.WR THEN
    ELSE
        R> 
        reg-ext? IF _REX.WB ELSE _REX.W THEN
    THEN
;

\ ModR/M (register or memory address)
: _MOD-MM
    \ 00 -> memory [reg] no displacement
    2 NUMBASE !
    00
    HEX |instr
;

: _MOD-MR
    \ 01 -> memory [reg + disp8]
    2 NUMBASE !
    01
    HEX |instr
;

: _MOD-RM
    \ 10 -> memory [reg + disp32]
    2 NUMBASE !
    10
    HEX |instr
;

: _MOD-RR
    \ 11 -> registers (reg-reg)
    2 NUMBASE !
    11 
    HEX |instr
;


\ cpu registers

: %rax
    \ accumulator. Stores return value
    0
;

: %rcx 
    \ counter for loops
    1
;

: %rdx 
    \ data (commonly extends the A register)
    2
;

: %rbx 
    \ base
    3 
;

: %rsp 
    \ stack pointer
    4
;

: %rbp 
    \ stack frame base pointer
    5
;

: %rsi 
    \ source index for string operations
    6
;

: %rdi 
    \ destination index for string operations
    7
;

\ general purpose registers
\ Depending where you call this registers you need to call the correct REX.
: %r8 
    8
;
: %r9 
    9
;
: %r10 
    A
;
: %r11 
    B
;
: %r12 
    C
;
: %r13 
    D 
;
: %r14 
    E 
;
: %r15 
    F 
;
\ op codes

: _add ( r | r/m )
    03 |instr
;

: _imul ( r | r/m | imm16/32 )
    69 |instr
;

: _mov ( r | r/m )
    8B |instr
;

: _mov-imm64 ( r | imm64 ) 
    swap B8 + swap |instr
;

: _lea ( r | m ) \ load effective address
    8D |instr
;

: _ret ( -- )
    C3 |instr
;

: _PAUSE ( -- ) \ spin loop hint
    asm:
        F3 |instr 90 |instr
    ;asm
;

: _syscall ( -- )
    0F |instr 05 |instr
;

: _sysret ( -- )
    0F |instr 07 |instr
;

\ full instructions

: %lea-rr ( reg reg  -- )
    asm:
        2over
        _add-rex-2op 
        >R >R \ save registers
        _lea _MOD-RR
        R> R> \ release registers
        reg-low swap reg-low 
        |instr |instr
    ;asm
;

: %lea-rm ( reg mem -- )
    asm:
        over
        _add-rex-1op
        >R
        _lea _MOD-RM
        R>
        swap reg-low
        |instr |instr
    ;asm
;

: %mov-imm64 ( imm64 reg -- )
    asm:
        _add-rex-1op
        reg-low 
        >R >R
        _mov-imm64
        R> R>
       swap |imm64
    ;asm
;

: %set-stack ( reg -- )
    _stack swap %mov-imm64
;

: %set-sp ( reg -- )
    _sp swap %mov-imm64
;

: %set-rstack ( reg -- )
    _rstack swap %mov-imm64
;

: %set-rsp ( reg -- )
    _rsp swap %mov-imm64
;

DEC

