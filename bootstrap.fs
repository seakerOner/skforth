: nip swap drop ; 

: tuck swap over ; 

: sqr dup * ; 

: <> = 0= ; 

8 constvar: CELLSIZEBYTES 
1 constvar: BYTESIZE

: CELL+ CELLSIZEBYTES + ; 
: CELLS CELLSIZEBYTES * ; 

: BYTE+ BYTESIZE + ;
: BYTES CELLSIZEBYTES /mod swap drop ;

: ceil-cells ( bytes -- cells )
    CELLSIZEBYTES + 1-
    CELLSIZEBYTES /mod
    swap drop
;
