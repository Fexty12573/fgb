.include "map.inc"

.define DIR $C000 ; Address to store sprite direction

main:
    di
    call wait_vblank

    ; Initialize direction to right
    ld hl, DIR
    ld (hl), $01

    ; Copy tile data to VRAM
    ld d, 16
    ld hl, TILE_DATA
    ld bc, $8000 ; Tile block 0 address
    call memcpy

    ; Copy sprite data to OAM
    ld d, 4
    ld hl, SPRITE
    ld bc, $FE00 ; OAM address
    call memcpy

    ; Configure PPU
    ld a, $83 ; Enable LCD, BG and OBJ display
    ldh ($40), a ; Write to LCDC register
    ld a, $0C ; OBP0/1
    ldh ($48), a ; Write to OBP0 register
    ldh ($49), a ; Write to OBP1 register

    ; Tile map should be initialized to 0, so no need to set it up

    ; Enable interrupts
    ld a, $01 ; Enable VBlank interrupt
    ldh ($FF), a ; Write to IE register
    ei
    nop
loop:
    halt
    jr loop

memcpy:
    ; Arguments:
    ; d: number of bytes to copy
    ; hl: source address
    ; bc: destination address
    ld a, (hl+)
    ld (bc), a
    inc bc
    dec d
    jr nz, memcpy
    ret

wait_vblank:
    ldh a, ($44) ; Read LY register
    cp $90       ; Compare with 144 (start of VBlank)
    jr z, wait_vblank
    ret

vblank:
    push af
    push hl
    push bc
    ld hl, DIR ; Load address of direction variable
    ld a, ($FE01) ; Read sprite X position
    add a, (hl) ; Update position
    cp 152 ; Check if the sprite has reached the right edge
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

.align 4
TILE_DATA:
    .db $FF, $00
    .db $FF, $00
    .db $FF, $00
    .db $FF, $00
    .db $FF, $00
    .db $FF, $00
    .db $FF, $00
    .db $FF, $00

SPRITE:
    .db $64, $64, $00, $00 ; y, x, tile, attributes

.org $40 ; VBlank interrupt vector
    jp vblank
