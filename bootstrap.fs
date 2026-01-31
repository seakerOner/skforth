: 2dup ( n -- )
    dup dup
;

: 2over ( n -- )
    over over
;

: nip ( x y -- y )
    swap drop 
; 

: tuck ( x y -- y x y )
    swap over 
; 

: sqr ( n -- n2 )
    dup * 
; 

: <> ( a b -- flag ) 
    \ true if a != b
    = 0= 
; 

: / ( a b -- a/b )
    /mod nip 
;

8 constvar: CELLSIZEBYTES
1 constvar: BYTESIZE

: CELL+ ( addr  -- addr' )
    CELLSIZEBYTES + 
; 

: CELLS ( n -- bytes )
    CELLSIZEBYTES * 
; 

: BYTE+ ( add -- addr' )
    BYTESIZE + 
;

: BYTES ( bytes -- cells )
    CELLSIZEBYTES / 
;

: ceil-cells ( bytes -- cells )
    \ convert bytes to cells, rounding up
    CELLSIZEBYTES + 1-
    CELLSIZEBYTES /
;

\ word to print a string

: ." PARSE-STRING ( string" -- )
    mode 1 =
    IF
        TYPE
    ELSE 
        \ addr lenbytes
        dup >R              \ addr lenbytes             R: lenbytes
        dup ceil-cells >R   \ addr lenbytes             R: lenbytes lencells
        HERE                \ addr lenbytes addr_code
        R> ALLOC            \ addr lenbytes addr_code  
        dup >R              \ addr lenbytes addr_code   R: lenbytes addr_code
        swap                \ addr addr_code lenbytes
        COPY-BYTES          \ ( )
        R> LITERAL          \ compile addr_code
        R> LITERAL          \ compile len
        TYPE
    THEN
; IMMEDIATE

\ word to do shell commands

: SYS" PARSE-STRING ( string" -- )
    mode 1 = 
    IF
        SHELL-CMD
    ELSE
        dup >R              
        dup ceil-cells >R   
        HERE                
        R> ALLOC            
        dup >R             
        swap                
        COPY-BYTES         
        R> LITERAL          
        R> LITERAL          
        SHELL-CMD
    THEN
; IMMEDIATE

\ words to work with BLOCKS

: BLOCK ( n -- addr ) 
    BLOCK-SIZE *
    BLOCKS-BASE +
;

: LOAD ( n -- )
    dup BLK!
    BLOCK
    BLOCK-SIZE
    INTERPRET-BLOCK
    0 BLK!
;

: LIST ( n -- )
   BLOCK
   BLOCK-SIZE
   TYPE
;

: LISTALL ( -- )
    0 
    BEGIN
    dup #BLOCKS 1- <> 
    WHILE
       dup ." BLOCK " . ." -------" cr
       dup >R LIST cr
       R> 1 +
    REPEAT
    drop
;

: NVIM ( n -- )
    dup BLK!
    dup EDITOR-BLOCK !
    BLOCK BLOCK-SIZE LOAD-EXTRN-EDITBUFF
    SYS" nvim -c 'e!' ~/.config/skforth/block_editor.fs"
;

: EDIT ( n -- ) \ ALIAS to NVIM until an editor is made
    NVIM 
;

: UPDATE ( -- )
    EDITOR-BLOCK @ -1 = 
    IF
        ." no BLOCK in editor" cr
    ELSE
        1 EDITOR-DIRTY !
        ." Editor BLOCK marked as dirty." cr
    THEN
;

: FLUSH ( -- )
    EDITOR-DIRTY @ 
    IF
        EDITOR-BLOCK @ SAVE-EXTRN-EDITBUFF
        0  EDITOR-DIRTY !
        ." EDITBUFF saved to marked BLOCK." cr 
    ELSE
        ." EDITBUFF is not set to dirty."   cr
    THEN
;

: ERASE ( n -- )
    BLOCK BLOCK-SIZE 0 FILL
;

\ to easily access skforth settings. You will need to restart skforth for new settings to take place though

: SETTINGS
    SYS" nvim ~/.config/skforth/config.fs"
;

\ Change base number

: DEC ( -- )
    10 NUMBASE !
;

: HEX ( -- )
    16 NUMBASE !
;

: OCTAL ( -- )
    8 NUMBASE !
;

\ to write assembly, you will need to use opcodes 
\ x86-64 implementation is in ./arch/x86_64.fs

: asm: ( -- )
    COMPTIME
    HEX
;

: ;asm ( -- )
   DEC 
   EXEC-CODE
   INTERPRET
;
