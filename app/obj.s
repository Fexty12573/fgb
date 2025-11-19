.include "rom.inc"

main:
    di
    call wait_vblank

    ; Copy tile data to VRAM
    ld de, 16 ; 16 bytes of tile data
    ld hl, TILE_0_DATA
    ld bc, $8000 ; Tile block 0 address
    call memcpy

    ; Copy sprite data to OAM
    ld de, 4
    ld hl, SPRITE
    ld bc, $FE00 ; OAM address
    call memcpy

    ; Set BG Tilemap to all 0s
    ld de, 1024 ; 32x32 tiles
    ld b, $00 ; Tile ID 0
    ld hl, $9800 ; BG Tilemap address
    call memset

    ; Configure PPU
    ld a, $83 ; Enable LCD, BG and OBJ
    ldh ($40), a ; Write to LCDC register
    ld a, $1C ; 0b00011100
    ldh ($47), a ; Write to BGP register
    ldh ($48), a ; Write to OBP0 register
    ldh ($49), a ; Write to OBP1 register
    xor a ; No STAT interrupts
    ldh ($41), a ; Write to STAT register

    ld a, $01 ; Enable VBlank interrupt
    ldh ($FF), a ; Write to IE register

    ei
    nop
-   halt
    nop
    jr -

vblank:
    push af
    push hl
    ld hl, $FE01 ; OAM Sprite 0 X position
    inc (hl) ; Move sprite to the right
    ld a, 169
    cp (hl) ; Check if it has moved off screen
    jr nz, .vblank_end
    ld (hl), 0 ; Reset to left side of screen
.vblank_end:
    pop hl
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

SPRITE:
    .db 100, 0, $00, $00 ; y, x, tile, attributes


.org $40 ; VBlank interrupt vector
    jp vblank
