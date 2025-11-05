
.define ROM_BANK_SIZE $4000

.rombanksize ROM_BANK_SIZE

.memorymap
    defaultslot 1
    slot 0 start $0000 size ROM_BANK_SIZE ; Fixed ROM
    slot 1 start $4000 size ROM_BANK_SIZE ; Switchable ROM
    slot 2 start $C000 size $2000 ; Work RAM
    slot 3 start $A000 size $2000 ; External RAM
    slot 4 start $FF80 size $007F ; HRAM
.endme

.rombanks 2
.emptyfill $FF

.romdmg

.licenseecodenew "ZZ"
.cartridgetype 0
.romsize
.ramsize 0
.countrycode $01
.nintendologo
.version $00

.computegbcomplementcheck

.bank 0 slot 0
.org $100
.section "Header" force
    nop
    jp $150
.ends

.bank 0 slot 0
.org $150