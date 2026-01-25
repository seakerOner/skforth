: nip swap drop ; 

: tuck swap over ; 

: sqr dup * ; 

: <> = 0= ; 

: SOURCE-c@ 
    2dup swap >= IF 
        2drop 0 
    ELSE 
        swap drop + c@ 
    THEN ; 

: var: PARSE-NAME ( "name" -- ) 
    CREATE 
    0 , 
; IMMEDIATE 

: buf: PARSE-NAME ( n "name" -- )  
    CREATE 
    dup , 
    0 , 
    ALLOC 
; IMMEDIATE 

8 constvar: CELLSIZEBYTES 

: CELL+ CELLSIZEBYTES + ; 

: CELLS CELLSIZEBYTES * ; 

: buf-data 2 CELLS + ; 

: buf-len  CELL+ @ ; 
: buf-cap  @ ; 
