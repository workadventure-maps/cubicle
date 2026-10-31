;---------------------------------------------------------------------------------
; Board and well drawing, in assembly: these loops run per row and per cell, and compiled C took ~5 scanlines
; per collision test, ~160 per piece move and ~250 to collapse a well (tests/smoke.py measures the frame cost).
; Called from C (816-tcc): args on the stack after the 3-byte return address (u8 args take 1 byte, the first at
; 5,s after php), A/X/Y 16-bit on entry and exit, data bank = $7E, return value in tcc__r0.
; Scratch: the dw_* u16 globals declared in src/main.c.
;---------------------------------------------------------------------------------
.include "hdr.asm"
.accu 16
.index 16
.16bit
.BASE $80

; keep in sync with src/main.c
.DEFINE HID 2
.DEFINE BOTTOM 21
.DEFINE CELLS $03F8
.DEFINE WALLS $FC07
.DEFINE NEXT_ROW 3
.DEFINE T_EMPTY $0809
.DEFINE T_FLASH $080A
.DEFINE T_JUNK $0C14
.DEFINE TM_OFS 32               ; byte offset of pmask[TM], the candidate masks

.SECTION ".well_text" SUPERFREE

; junk row masks: every cell but the gap (columns are bits 3..9)
junk_masks:
    .dw $FFF7, $FFEF, $FFDF, $FFBF, $FF7F, $FEFF, $FDFF

;---------------------------------------------------------------------------------
; u8 fits(u8 mi, u8 at): 1 if the masks pmask[mi..mi+3] fit on board rows at..at+3 (index into rows)
fits:
    php
    rep #$30
    lda 5,s
    and #$00FF
    asl a
    tax
    lda 6,s
    and #$00FF
    asl a
    tay
    lda.w pmask,x
    and.w rows,y
    bne @no
    lda.w pmask+2,x
    and.w rows+2,y
    bne @no
    lda.w pmask+4,x
    and.w rows+4,y
    bne @no
    lda.w pmask+6,x
    and.w rows+6,y
    bne @no
    lda #1
    sta.b tcc__r0
    plp
    rtl
@no:
    stz.b tcc__r0
    plp
    rtl

;---------------------------------------------------------------------------------
; u8 find_drop(u8 mi, u8 at): the lowest row the masks pmask[mi..] fit at, falling from row at.
; Ends at the latest on the floor rows, which have every bit set.
find_drop:
    php
    rep #$30
    lda 5,s
    and #$00FF
    asl a
    tax
    lda 6,s
    and #$00FF
    asl a
    tay
@down:
    iny
    iny
    lda.w pmask,x
    and.w rows,y
    bne @stop
    lda.w pmask+2,x
    and.w rows+2,y
    bne @stop
    lda.w pmask+4,x
    and.w rows+4,y
    bne @stop
    lda.w pmask+6,x
    and.w rows+6,y
    beq @down
@stop:
    dey
    dey
    tya
    lsr a
    sta.b tcc__r0
    plp
    rtl

;---------------------------------------------------------------------------------
; u8 place(u8 p, u8 s, u8 col): the masks of shape s shifted to col (box column + 3). If they fit at well p's
; box row, they become the piece's masks and it returns 1. A col past 12 (wrapped below 0) never fits.
place:
    php
    rep #$30
    lda 7,s
    and #$00FF
    cmp #13
    bcc +
    brl @no
+   sta.w dw_col
    lda 6,s
    and #$00FF
    sta.w dw_s
    lda 5,s
    and #$00FF
    sta.w dw_p
    tax
    lda.l row_base,x
    and #$00FF
    sta.w dw_base
    lda.w pyy,x
    and #$00FF
    clc
    adc.w dw_base
    asl a
    sta.w dw_y0             ; byte offset of rows[box row]
    ldy #0
    ldx.w dw_s
@mask:
    lda.w shape_rows,x
    and #$00FF
    phx
    ldx.w dw_col
    beq +
-   asl a
    dex
    bne -
+   plx
    sta.w pmask+TM_OFS,y
    inx
    iny
    iny
    cpy #8
    bne @mask
    ldy.w dw_y0
    lda.w pmask+TM_OFS
    and.w rows,y
    bne @no
    lda.w pmask+TM_OFS+2
    and.w rows+2,y
    bne @no
    lda.w pmask+TM_OFS+4
    and.w rows+4,y
    bne @no
    lda.w pmask+TM_OFS+6
    and.w rows+6,y
    bne @no
    lda.w dw_p
    asl a
    asl a
    asl a
    tax
    lda.w pmask+TM_OFS
    sta.w pmask,x
    lda.w pmask+TM_OFS+2
    sta.w pmask+2,x
    lda.w pmask+TM_OFS+4
    sta.w pmask+4,x
    lda.w pmask+TM_OFS+6
    sta.w pmask+6,x
    lda #1
    sta.b tcc__r0
    plp
    rtl
@no:
    stz.b tcc__r0
    plp
    rtl

;---------------------------------------------------------------------------------
; void draw_piece_cells(u8 p): erase well p's piece and ghost cells, then draw the ghost at gy[p] and the
; piece at pyy[p] (the piece last, so it covers the ghost). The caller marks the rows dirty.
draw_piece_cells:
    php
    rep #$30
    lda 5,s
    and #$00FF
    sta.w dw_p
    jsr erase
    ldx.w dw_p
    lda.w ptype,x           ; shape index = ptype * 16 + prot * 4
    and #$00FF
    asl a
    sta.w dw_tilex          ; ptype * 2, into the tile tables
    asl a
    asl a
    asl a
    sta.w dw_s
    lda.w prot,x
    and #$00FF
    asl a
    asl a
    clc
    adc.w dw_s
    sta.w dw_s
    lda.w col0,x            ; screen column of the box's left edge: col0 + pcol - 3
    and #$00FF
    sta.w dw_col
    lda.w pcol,x
    and #$00FF
    clc
    adc.w dw_col
    sec
    sbc #3
    sta.w dw_col

    lda.w gy,x              ; ghost: drawn slots 4..7
    and #$00FF
    sta.w dw_y0
    ldx.w dw_tilex
    lda.l ghost_tile,x
    sta.w dw_tile
    lda.w dw_slot
    clc
    adc #8
    sta.w dw_dst
    jsr blocks

    ldx.w dw_p              ; piece: drawn slots 0..3
    lda.w pyy,x
    and #$00FF
    sta.w dw_y0
    ldx.w dw_tilex
    lda.l piece_tile,x
    sta.w dw_tile
    lda.w dw_slot
    sta.w dw_dst
    jsr blocks
    plp
    rtl

; empty the cells listed in drawn[dw_p * 8 ..], forget them; dw_slot = their byte offset
erase:
    lda.w dw_p
    asl a
    asl a
    asl a
    asl a
    sta.w dw_slot
    tax
    ldy #8
@cell:
    lda.w drawn,x
    cmp #$FFFF
    beq @next
    phx
    asl a
    tax
    lda #T_EMPTY
    sta.w bgmap,x
    plx
    lda #$FFFF
    sta.w drawn,x
@next:
    inx
    inx
    dey
    bne @cell
    rts

; the 4 blocks of shape dw_s, box top on board row dw_y0, as tile dw_tile; their bgmap indices go to drawn
; from byte offset dw_dst. Rows above the well (y < HID) are not drawn.
blocks:
    lda.w dw_y0             ; base = row_map[y0] + column
    asl a
    tax
    lda.w row_map,x
    clc
    adc.w dw_col
    sta.w dw_base
    lda.w dw_s
    sta.w dw_k
    ldy #4
@block:
    ldx.w dw_k
    lda.w blk_y,x
    and #$00FF
    clc
    adc.w dw_y0
    cmp #HID
    bcc @skip
    txa
    asl a
    tax
    lda.w blk_off,x
    clc
    adc.w dw_base
    ldx.w dw_dst
    sta.w drawn,x
    asl a
    tax
    lda.w dw_tile
    sta.w bgmap,x
@skip:
    inc.w dw_dst
    inc.w dw_dst
    inc.w dw_k
    dey
    bne @block
    rts

;---------------------------------------------------------------------------------
; u8 lock_rows(u8 p): well p's piece joins the board, and its cells are no longer tracked as the moving piece
; (it stays on screen as blocks). Returns 255 if a block is above the well, else a bit mask of the piece's
; box rows (bit 0 = top) that are now full; those flash.
lock_rows:
    php
    rep #$30
    lda 5,s
    and #$00FF
    jsr wellsetup
    ldx.w dw_p
    lda.w pyy,x
    and #$00FF
    sta.w dw_y0
    clc
    adc.w dw_base
    asl a
    tay
    lda.w dw_p
    asl a
    asl a
    asl a
    tax
    lda.w pmask,x
    ora.w rows,y
    sta.w rows,y
    lda.w pmask+2,x
    ora.w rows+2,y
    sta.w rows+2,y
    lda.w pmask+4,x
    ora.w rows+4,y
    sta.w rows+4,y
    lda.w pmask+6,x
    ora.w rows+6,y
    sta.w rows+6,y
    txa                     ; drawn[p * 8 ..] = none
    asl a
    tax
    lda #$FFFF
    ldy #8
-   sta.w drawn,x
    inx
    inx
    dey
    bne -
    lda.w dw_base           ; a block in the hidden rows: locked out
    asl a
    tax
    lda.w rows,x
    ora.w rows+2,x
    and #CELLS
    beq +
    lda #255
    sta.b tcc__r0
    plp
    rtl
+   lda.w dw_y0
    clc
    adc.w dw_base
    asl a
    tax
    lda #1
    sta.w dw_tile           ; bit of the current box row
    stz.w dw_k              ; full rows found
    ldy.w dw_y0
@row:
    cpy #BOTTOM+1           ; the floor rows below are full too: stop there
    bcs @done
    lda.w rows,x
    cmp #$FFFF
    bne @next
    lda.w dw_k
    ora.w dw_tile
    sta.w dw_k
    phx
    phy
    tya
    jsr rowcells
    lda #T_FLASH
    jsr fill7
    ply
    plx
@next:
    inx
    inx
    iny
    asl.w dw_tile
    lda.w dw_tile
    cmp #16
    bne @row
@done:
    lda.w dw_k
    sta.b tcc__r0
    plp
    rtl

;---------------------------------------------------------------------------------
; void collapse(u8 p): full rows of well p go away, the rows above fall into their place (masks and cells)
collapse:
    php
    rep #$30
    lda 5,s
    and #$00FF
    jsr wellsetup
    lda #BOTTOM
    sta.w dw_k              ; source row
    sta.w dw_dst            ; destination row
@row:
    lda.w dw_k
    clc
    adc.w dw_base
    asl a
    tax
    lda.w rows,x
    cmp #$FFFF
    beq @gone
    lda.w dw_dst
    cmp.w dw_k
    beq +
    jsr copyrow
+   dec.w dw_dst
@gone:
    lda.w dw_k
    beq @fill
    dec.w dw_k
    bra @row
@fill:                      ; rows 0..dw_dst are empty now
    lda.w dw_dst
    bmi @end
    clc
    adc.w dw_base
    asl a
    tax
    lda #WALLS
    sta.w rows,x
    lda.w dw_dst
    cmp #HID
    bcc +
    jsr rowcells
    lda #T_EMPTY
    jsr fill7
+   dec.w dw_dst
    bra @fill
@end:
    plp
    rtl

;---------------------------------------------------------------------------------
; void add_junk_rows(u8 p, u8 n, u8 gap): the stack of well p rises n rows (1..20), n junk rows come in at
; the bottom, all with the gap in column gap
add_junk_rows:
    php
    rep #$30
    lda 6,s
    and #$00FF
    sta.w dw_n
    lda 7,s
    and #$00FF
    sta.w dw_s
    lda 5,s
    and #$00FF
    jsr wellsetup
    stz.w dw_dst            ; row y + n moves to y, from the top
@up:
    lda.w dw_dst
    clc
    adc.w dw_n
    cmp #BOTTOM+1
    bcs @junk
    sta.w dw_k
    jsr copyrow
    inc.w dw_dst
    bra @up
@junk:
    lda.w dw_s
    asl a
    sta.w dw_s              ; gap * 2: byte offset in a row of cells
    tax
    lda.l junk_masks,x
    sta.w dw_tile
@jrow:                      ; rows dw_dst..BOTTOM; with n <= 20 they are all visible
    lda.w dw_dst
    cmp #BOTTOM+1
    bcs @end
    clc
    adc.w dw_base
    asl a
    tax
    lda.w dw_tile
    sta.w rows,x
    lda.w dw_dst
    jsr rowcells
    lda #T_JUNK
    jsr fill7
    tya
    clc
    adc.w dw_s
    tay
    lda #T_EMPTY
    sta.w bgmap,y
    inc.w dw_dst
    bra @jrow
@end:
    plp
    rtl

;---------------------------------------------------------------------------------
; void gray_well_cells(u8 p): the falling piece disappears, every block of well p turns to junk
gray_well_cells:
    php
    rep #$30
    lda 5,s
    and #$00FF
    sta.w dw_p
    jsr erase
    lda.w dw_p
    jsr wellsetup
    lda #HID
    sta.w dw_k
@row:
    jsr rowcells
    tya
    tax
    ldy #7
@cell:
    lda.w bgmap,x
    cmp #T_EMPTY
    beq +
    lda #T_JUNK
    sta.w bgmap,x
+   inx
    inx
    dey
    bne @cell
    inc.w dw_k
    lda.w dw_k
    cmp #BOTTOM+1
    bne @row
    plp
    rtl

;---------------------------------------------------------------------------------
; void draw_next_cells(u8 p, u8 t): piece t in well p's next-piece box (4x2 cells right of the Woka)
draw_next_cells:
    php
    rep #$30
    lda 6,s
    and #$00FF
    sta.w dw_s
    lda 5,s
    and #$00FF
    tax
    lda.w col0,x
    and #$00FF
    clc
    adc #NEXT_ROW * 32 + 3
    asl a
    tax
    stz.w bgmap,x           ; clear the box
    stz.w bgmap+2,x
    stz.w bgmap+4,x
    stz.w bgmap+6,x
    stz.w bgmap+64,x
    stz.w bgmap+66,x
    stz.w bgmap+68,x
    stz.w bgmap+70,x
    lda.w dw_s              ; the I piece sits in its box's row 1: one row up; the O is 2 wide: one right
    bne +
    txa
    sec
    sbc #64
    tax
    bra @drawn
+   cmp #1
    bne @drawn
    inx
    inx
@drawn:
    stx.w dw_base
    lda.w dw_s
    asl a
    tax
    lda.l piece_tile,x
    sta.w dw_tile
    lda.w dw_s              ; blocks of shape t * 16 (spawn orientation)
    asl a
    asl a
    asl a
    asl a
    asl a                   ; * 2: byte offset into blk_off
    tax
    ldy #4
-   lda.w blk_off,x
    asl a
    clc
    adc.w dw_base
    phx
    tax
    lda.w dw_tile
    sta.w bgmap,x
    plx
    inx
    inx
    dey
    bne -
    plp
    rtl

;---------------------------------------------------------------------------------
; helpers

; A = p: dw_p, dw_base = row_base[p], dw_col = col0[p]
wellsetup:
    sta.w dw_p
    tax
    lda.l row_base,x
    and #$00FF
    sta.w dw_base
    lda.w col0,x
    and #$00FF
    sta.w dw_col
    rts

; A = board row (visible): Y = byte offset in bgmap of its first cell in the well at dw_col
rowcells:
    asl a
    tax
    lda.w row_map,x
    clc
    adc.w dw_col
    asl a
    tay
    rts

; 7 cells from byte offset Y = A
fill7:
    sta.w bgmap,y
    sta.w bgmap+2,y
    sta.w bgmap+4,y
    sta.w bgmap+6,y
    sta.w bgmap+8,y
    sta.w bgmap+10,y
    sta.w bgmap+12,y
    rts

; board row dw_k to row dw_dst of the well at dw_base / dw_col: the mask, and the cells when dw_dst is visible
copyrow:
    lda.w dw_k
    clc
    adc.w dw_base
    asl a
    tax
    lda.w rows,x
    pha
    lda.w dw_dst
    clc
    adc.w dw_base
    asl a
    tax
    pla
    sta.w rows,x
    lda.w dw_dst
    cmp #HID
    bcc @done
    jsr rowcells
    lda.w dw_k
    cmp #HID
    bcc @empty
    phy
    jsr rowcells
    tyx
    ply
    lda.w bgmap,x
    sta.w bgmap,y
    lda.w bgmap+2,x
    sta.w bgmap+2,y
    lda.w bgmap+4,x
    sta.w bgmap+4,y
    lda.w bgmap+6,x
    sta.w bgmap+6,y
    lda.w bgmap+8,x
    sta.w bgmap+8,y
    lda.w bgmap+10,x
    sta.w bgmap+10,y
    lda.w bgmap+12,x
    sta.w bgmap+12,y
@done:
    rts
@empty:
    lda #T_EMPTY
    jmp fill7

.ENDS
