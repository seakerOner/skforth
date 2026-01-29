: nip 
    swap drop 
; 

: tuck 
    swap over 
; 

: sqr 
    dup * 
; 

: <> = 0= 
; 

: / 
    /mod nip 
;

8 constvar: CELLSIZEBYTES 
1 constvar: BYTESIZE

: CELL+ 
    CELLSIZEBYTES + 
; 

: CELLS 
    CELLSIZEBYTES * 
; 

: BYTE+ 
    BYTESIZE + 
;

: BYTES 
    CELLSIZEBYTES / 
;

: ceil-cells ( bytes -- cells )
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

: NVIM ( n -- )
    dup BLK!
    dup EDITOR-BLOCK!
    BLOCK BLOCK-SIZE LOAD-EXTRN-EDITBUFF
    SYS" nvim -c 'e!' ~/.config/skforth/block_editor.fs"
;

: EDIT ( n -- ) \ ALIAS to NVIM until an editor is made
    NVIM 
;

: UPDATE ( -- )
    EDITOR-BLOCK@ -1 = 
    IF
        ." no BLOCK in editor" cr
    ELSE
        1 EDITOR-DIRTY!
        ." Editor BLOCK marked as dirty." cr
    THEN
;

: FLUSH ( -- )
    EDITOR-DIRTY@ 
    IF
        EDITOR-BLOCK@ SAVE-EXTRN-EDITBUFF
        0  EDITOR-DIRTY!
        ." EDITBUFF saved to marked BLOCK." cr 
    ELSE
        ." EDITBUFF is not set to dirty."   cr
    THEN
;
