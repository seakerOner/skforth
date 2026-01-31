\
\ This file defines x86-64 instructions as skforth "words", allowing you to
\ write raw binary code directly inside Forth.  
\ It is NOT a full assembler: it does not handle text, labels, or parsing.  
\ Instead, each word pushes the corresponding instruction bytes (opcode, REX,
\ ModR/M, SIB, displacement, or immediate) directly onto the main stack and then executes it.
\
\ You can mix Forth logic and "assembly" freely, giving full control over memory,
\ registers, and the stack.
\
\ Instruction structure:
\   [prefix] [REX] [opcode] [ModRM (mod | reg | r/m)] [optional SIB] [displacement] [immediate]
\
\ Extended registers (%r8-%r15) are masked for easier REX flag handling and reduce code branching.
\ Only the lower bits of the registers matter (_add-rex-1op, _add-rex-2op, reg-low, reg-ext?).
\
\ This file is still a work in progress. The ideas here can be adapted for other
\ processors: you can create a similar set of words for your own architecture,
\ encoding instructions as data within Forth for a hybrid Forth + raw assembly
\ workflow.
\
\ With this approach, you can:
\   - move values or addresses into registers (%mov-imm64)
\   - load effective addresses (%lea-rr, %lea-rm)
\   - set up stack and return pointers (%set-stack, %set-rstack, etc.)
\   - generate CPU instructions without the overhead of a full assembler.
\     if you know what you want, you don't need an assembler to decide for you
\
\ This hybrid approach gives maximum flexibility to combine Forth logic with
\ direct low-level control.

\ NOTE: this code is under development; instructions may be missing or untested.


: |addr ( u64 -- )
    dup DEC  0 rshift HEX FF and |
    dup DEC  8 rshift HEX FF and |
    dup DEC 16 rshift HEX FF and |
    dup DEC 24 rshift HEX FF and |
    dup DEC 32 rshift HEX FF and |
    dup DEC 40 rshift HEX FF and |
    dup DEC 48 rshift HEX FF and |
        DEC 56 rshift HEX FF and |
        DEC
;

HEX

: _REX.W 
    \ 64 bit operand size
    48
;

: _REX.WR 
    \ 64 bit operand size
    \ REX.W and REX.R combination (bits REG 8-15 first operand)
    4C 
;

: _REX.WB
    \ 64 bit operand size
    \ REX.W and REX.R combination (bits REG 8-15 second operand)
    49
;

: _REX.WX
    \ 64 bit operand size
    \ for memory address
    \ REX.W and REX.X combination (extra bit for SIB index, REG 8-16 on index)
    4A 
;

: _REX.WXB
    \ 64 bit operand size
    \ REX.W, REX.X and REX.B combination
    4B
;

: _REX.WRB 
    \ 64 bit operand size
    \ REX.W, REX.R and REX.B combination
    4D
;

: _REX.WRX
    \ 64 bit operand size
    \ REX.W, REX.R and REX.X combination
    4E
;

: _REX.WRXB
    \ 64 bit operand size
    \ REX.W, REX.R, REX.X, REX.B combination
    4F
;

: _add-rex-1op ( reg -- instruction )
    dup 
    reg-ext? 
   IF _REX.WB ELSE _REX.W THEN |
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
    THEN |
;

\ ModR/M (register or memory address)
: _MOD-MM
    \ 00 -> memory [reg] no displacement
    2 NUMBASE !
    00
    HEX |
;

: _MOD-MR
    \ 01 -> memory [reg + disp8]
    2 NUMBASE !
    01
    HEX |
;

: _MOD-RM
    \ 10 -> memory [reg + disp32]
    2 NUMBASE !
    10
    HEX |
;

: _MOD-RR
    \ 11 -> registers (reg-reg)
    2 NUMBASE !
    11 
    HEX |
;


\ cpu registers

: %rax;
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

: reg-low ( reg -- 3lower ) \ we extract the 3 lower bits for the actual flag
    7 and 
;

: reg-ext? ( reg -- flag ) \ returns 0 if its a 
    8 and
;

\ op codes

: _add ( r | r/m )
    03 |
;

: _imul ( r | r/m | imm16/32 )
    69 |
;

: _mov ( r | r/m )
    8B |
;

: _mov-imm64 ( r | imm64 ) 
    B8 + |
;

: _lea ( r | m ) \ load effective address
    8D |
;

: _ret ( -- )
    C3 |
;

: _PAUSE ( -- ) \ spin loop hint
    asm:
        F3 90
    ;asm
;

: _syscall ( -- )
    0F 05 |
;

: _sysret ( -- )
    0F 07 |
;

\ full instructions

: %lea-rr ( reg reg  -- )
    asm:
        2over
        _add-rex-2op 
        _lea _MOD-RR
        reg-low swap reg-low 
        | |
    ;asm
;

: %lea-rm ( reg mem -- )
    asm:
        over
        _add-rex-1op
        _lea _MOD-RM
        swap reg-low
        | |
    ;asm
;

: %mov-imm64 ( imm64 reg -- )
    asm:
        _add-rex-1op
        reg-low _mov-imm64
       swap |addr
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

