
memcpy:
    ; Arguments:
    ; de: number of bytes to copy
    ; hl: source address
    ; bc: destination address
    push af
.loop:
    ld a, (hl+)
    ld (bc), a
    inc bc
    dec de
    ld a, d 
    or e ; check if de == 0 since `dec de` does not set flags
    jr nz, .loop
    pop af
    ret

memset:
    ; Arguments:
    ; de: number of bytes to set
    ; b: value to set
    ; hl: destination address
    push af
-   ld (hl), b
    inc hl
    dec de
    ld a, d
    or e
    jr nz, -
    pop af
    ret

wait_vblank:
    ldh a, ($44) ; Read LY register
    cp $90       ; Compare with 144 (start of VBlank)
    jr z, wait_vblank
    ret
