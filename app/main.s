.include "rom.inc"

.define DIR $C000 ; Address to store sprite direction

main:
    di
    call wait_vblank

    ; Initialize direction to right
    ld hl, DIR
    ld (hl), $01

    ; Copy tile data to VRAM
    ld de, 16 ; 16 bytes of tile data
    ld hl, TILE_0_DATA
    ld bc, $8000 ; Tile block 0 address
    call memcpy

    ld de, 16 ; 16 bytes of tile data
    ld hl, TILE_128_DATA
    ld bc, $9000 ; Tile block 2 address
    call memcpy

    ; Copy sprite data to OAM
    ld de, 4
    ld hl, SPRITE
    ld bc, $FE00 ; OAM address
    call memcpy

    ; Configure PPU
    ld a, $E3 ; Enable LCD, BG, OBJ and WND and set Window tilemap to $9C00
    ldh ($40), a ; Write to LCDC register
    ld a, $1C ; 0b00011100
    ldh ($47), a ; Write to BGP register
    ldh ($48), a ; Write to OBP0 register
    ldh ($49), a ; Write to OBP1 register
    ld a, 110
    ldh ($4A), a ; Write to WY register
    ld a, 50
    ldh ($4B), a ; Write to WX register
    ld a, $08 ; Enable HBlank interrupt
    ldh ($41), a ; Write to STAT register

    ; Set BG Tilemap to all 0s
    ld de, 1024 ; 32x32 tiles
    ld b, $00 ; Tile ID 0
    ld hl, $9800 ; BG Tilemap address
    call memset

    ; Set Window Tilemap to all $80s
    ld de, 1024 ; 32x32 tiles
    ld b, $80 ; Tile ID 128
    ld hl, $9C00 ; Window Tilemap address
    call memset

    ; Enable interrupts
    ld a, $03 ; Enable VBlank and LCDC interrupts
    ldh ($FF), a ; Write to IE register
    ei
    nop
loop:
    halt
    jr loop

vblank:
    push af
    push hl
    push bc
    ld hl, DIR ; Load address of direction variable
    ld a, ($FE01) ; Read sprite X position
    add a, (hl) ; Update position
    cp 160 ; Check if the sprite has reached the right edge
    jr z, put_negative
    cp 8 ; Check if the sprite has reached the left edge
    jr z, put_positive
    jr done
put_negative:
    ld b, $FF ; Set direction to left
    ld (hl), b
    jr done
put_positive:
    ld b, $01 ; Set direction to right
    ld (hl), b
done:
    ld ($FE01), a ; Write updated X position back
    pop bc
    pop hl
    pop af
    reti

hblank:
    push af
    push bc
    ld b, $20 ; Mask for bit 5 (Window Enable)
    ldh a, ($40) ; Read LCDC register
    xor b ; Toggle bit 5
    ldh ($40), a ; Write back to LCDC register
    pop bc
    pop af
    reti

.include "util.inc"

.align 4
TILE_0_DATA:
    .db $FF, $00
    .db $FF, $00
    .db $FF, $00
    .db $FF, $00
    .db $FF, $00
    .db $FF, $00
    .db $FF, $00
    .db $FF, $00

.align 4
TILE_128_DATA:
    .db $00, $FF
    .db $00, $FF
    .db $00, $FF
    .db $00, $FF
    .db $00, $FF
    .db $00, $FF
    .db $00, $FF
    .db $00, $FF

SPRITE:
    .db 100, 67, $00, $00 ; y, x, tile, attributes

.org $40 ; VBlank interrupt vector
    jp vblank

.org $48 ; LCDC interrupt vector
    jp hblank
