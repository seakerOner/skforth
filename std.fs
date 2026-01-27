
: var: PARSE-NAME ( "name" -- ) \ stores 1 CELL
    CREATE 
    0 , 
; IMMEDIATE 

: buff: PARSE-NAME ( n "name" -- )  \ stores n CELLS
    CREATE 
    dup , 
    0 , 
    ALLOC 
; IMMEDIATE 

: buf-data ( cellbuf -- addr )
    2 CELLS 
    +
; 

: buf-len  ( cellbuf -- len ) \ n CELLS
    CELL+ @
; 

: buf-cap  ( cellbuf -- cap )
    @ 
; 

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

