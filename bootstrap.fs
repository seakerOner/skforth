: nip 
    swap drop 
; 

: tuck 
    swap over 
; 

: sqr 
    dup * 
; 

: <> 
    = 0= 
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

