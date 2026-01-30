\ WARN: WIP, setting up words and minimize workload for future mini-assembler

\ These are some x86-64 assembly instructions/opcodes into words, with this as an
\ example you should be able to make the same for your architeture! 
\
\ Reference: <http://ref.x86asm.net/coder64.html>

\ instruction structure
(  [prefix] [REX] [opcode] [ModRM *mod | reg | r/m*] [SIB *optional*] [displacement] [immediate] )


: %REX.W 
    \ 64 bit operand size
    48
;

: %REX.WR 
    \ 64 bit operand size
    \ REX.W and REX.R combination (bits REG 8-15 first operand) 4C
;

: %REX.WB
    \ 64 bit operand size
    \ REX.W and REX.R combination (bits REG 8-15 second operand)
    49
;

: %REX.WX
    \ 64 bit operand size
    \ for memory adress
    \ REX.W and REX.X combination (extra bit for SIB index, REG 8-16 on index)
    4A
;

: %REX.WXB
    \ 64 bit operand size
    \ REX.W, REX.X and REX.B combination
    4B
;

: %REX.WRB 
    \ 64 bit operand size
    \ REX.W, REX.R and REX.B combination
    4D
;

: %REX.WRX
    \ 64 bit operand size
    \ REX.W, REX.R and REX.X combination
    4E
;

: %REX.WRXB
    \ 64 bit operand size
    \ REX.W, REX.R, REX.X, REX.B combination
    4F
;

\ ModR/M (register or memory address)
: %MOD-MM
    \ 00 -> memory [reg] no displacement
    2 NUMBASE !
    00
    HEX |
;

: %MOD-MR
    \ 01 -> memory [reg + disp8]
    2 NUMBASE !
    01
    HEX |
;

: %MOD-RM
    \ 10 -> memory [reg + disp32]
    2 NUMBASE !
    10
    HEX |
;

: %MOD-RR
    \ 11 -> registers (reg-reg)
    2 NUMBASE !
    11 
    HEX |
;


\ cpu registers

: %rax,
    \ accumulator. Stores return value
    0
;

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
    0
;
: %r9 
    1
;
: %r10 
    2
;
: %r11 
    3
;
: %r12 
    4
;
: %r13 
    5
;
: %r14 
    6
;
: %r15 
    7 
;


: %add ( r | r/m )
    03 |
;

: %ret ( -- )
    C3 |
;

: %syscall ( -- )
    0F 05 |
;

: %sysret ( -- )
    0F 07 |
;


