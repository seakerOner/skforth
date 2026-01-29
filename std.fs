
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
