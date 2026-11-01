;---------------------------------------------------------------------------------
; Board and well drawing, in assembly: these loops run per row and per cell, and compiled C took ~5 scanlines
; per collision test, ~160 per piece move and ~250 to collapse a well (tests/smoke.py measures the frame cost).
; Called from C (816-tcc): args on the stack after the 3-byte return address (u8 args take 1 byte, the first at
; 5,s after php), A/X/Y 16-bit on entry and exit, data bank = $7E, return value in tcc__r0.
; Scratch: the dw_* u16 globals declared in src/main.c.
;
; Board: 4 bytes per row (a 32-bit mask), player p's board row y at byte (rbase[p] + y) * 4 of rows[]. Columns
; 0..w-1 are bits 3..w+2 (w = wcells[p], 7 in versus, up to 28 in a shared well); the other bits are walls
; (wall_lo[p], wall_hi[p]). A falling piece's 4 row masks are 16 bits wide, seen from byte pk[p] = pcol / 8 of
; its rows and shifted by pcol % 8: its 4-column box always fits in that window, and the 65816 loads 16 bits
; from any byte address. ov[] has the same layout and holds the falling pieces of a shared well, so teammates'
; pieces are solid: collisions test rows | (ov & solid).
;---------------------------------------------------------------------------------
.include "hdr.asm"
.accu 16
.index 16
.16bit
.BASE $80

; keep in sync with src/main.c
.DEFINE HID 2
.DEFINE BOTTOM 21
.DEFINE NEXT_ROW 3
.DEFINE T_EMPTY $0809
.DEFINE T_FLASH $080A
.DEFINE T_JUNK $0C14
.DEFINE WELL_TOP 6
.DEFINE W_FALL 2
.DEFINE W_LOCK 4
.DEFINE TM_OFS 32               ; byte offset of pmask[TM], the candidate masks
.DEFINE MAX_COL 28              ; highest pcol: the box's last column is the row's bit 31
.DEFINE BREAK_FRAMES 600
.DEFINE KEYS_C $3300            ; Start, Select, Left, Right

.SECTION ".well_text" SUPERFREE

; junk row masks (low word; versus wells only): every cell but the gap (columns are bits 3..9)
junk_masks:
    .dw $FFF7, $FFEF, $FFDF, $FFBF, $FF7F, $FEFF, $FDFF

;---------------------------------------------------------------------------------
; u8 fits(u8 p, u8 at): 1 if piece p's masks fit with its box top on row index at (rbase[p] + y)
fits:
    php
    rep #$30
    lda 6,s
    and #$00FF
    asl a
    asl a
    sta.w dw_y0
    lda 5,s
    and #$00FF
    jsr window
    jsr test4
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
; u8 fits_board(u8 p, u8 at): fits, against the board only (not teammates' falling pieces)
fits_board:
    php
    rep #$30
    lda 6,s
    and #$00FF
    asl a
    asl a
    sta.w dw_y0
    lda 5,s
    and #$00FF
    jsr window
    jsr test4board
    bne +
    lda #1
    sta.b tcc__r0
    plp
    rtl
+   stz.b tcc__r0
    plp
    rtl

;---------------------------------------------------------------------------------
; u8 find_drop(u8 p, u8 at): the lowest row index piece p fits at, falling from row index at.
; u8 find_land(u8 p, u8 at): the same against the board only (where its ghost goes).
; Both end at the latest on the floor rows, which have every bit set.
find_drop:
    php
    rep #$30
    lda 6,s
    and #$00FF
    asl a
    asl a
    sta.w dw_y0
    lda 5,s
    and #$00FF
    jsr window
    lda.w solid
    bne drop_solid
    bra drop_board
find_land:
    php
    rep #$30
    lda 6,s
    and #$00FF
    asl a
    asl a
    sta.w dw_y0
    lda 5,s
    and #$00FF
    jsr window
drop_board:                 ; only the board (the usual case: inline)
    iny
    iny
    iny
    iny
    lda.w pmask,x
    and.w rows,y
    bne drop_stop
    lda.w pmask+2,x
    and.w rows+4,y
    bne drop_stop
    lda.w pmask+4,x
    and.w rows+8,y
    bne drop_stop
    lda.w pmask+6,x
    and.w rows+12,y
    beq drop_board
    bra drop_stop
drop_solid:                 ; teammates' pieces too
    iny
    iny
    iny
    iny
    lda.w ov,y
    ora.w rows,y
    and.w pmask,x
    bne drop_stop
    lda.w ov+4,y
    ora.w rows+4,y
    and.w pmask+2,x
    bne drop_stop
    lda.w ov+8,y
    ora.w rows+8,y
    and.w pmask+4,x
    bne drop_stop
    lda.w ov+12,y
    ora.w rows+12,y
    and.w pmask+6,x
    beq drop_solid
drop_stop:
    tya
    sec
    sbc.w dw_k
    lsr a
    lsr a
    dec a                   ; the last row that fit
    sta.b tcc__r0
    plp
    rtl

;---------------------------------------------------------------------------------
; u8 place(u8 p, u8 s, u8 col): the masks of shape s shifted to col (box column + 3). If they fit at piece p's
; box row, they become its masks (pk[p] its window) and it returns 1. A col past MAX_COL (or wrapped below 0)
; never fits.
place:
    php
    rep #$30
    lda 7,s
    and #$00FF
    cmp #MAX_COL+1
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
    lda.w rbase,x
    and #$00FF
    sta.w dw_base
    lda.w pyy,x
    and #$00FF
    clc
    adc.w dw_base
    asl a
    asl a
    sta.w dw_y0
    lda.w dw_col
    lsr a
    lsr a
    lsr a
    sta.w dw_k              ; window byte
    clc
    adc.w dw_y0
    sta.w dw_y0             ; byte offset of the window on the box's top row
    lda.w dw_col
    and #7
    sta.w dw_n              ; shift inside the window
    ldy #0
    ldx.w dw_s
@mask:
    lda.w shape_rows,x
    and #$00FF
    phx
    ldx.w dw_n
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
    ldx #TM_OFS
    jsr test4
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
    ldx.w dw_p
    sep #$20
    lda.w dw_k
    sta.w pk,x
    rep #$20
    lda #1
    sta.b tcc__r0
    plp
    rtl
@no:
    stz.b tcc__r0
    plp
    rtl

;---------------------------------------------------------------------------------
; void ov_xor(u8 p): piece p's masks into the falling-piece overlay, or out of it (at its box row and window)
ov_xor:
    php
    rep #$30
    lda 5,s
    and #$00FF
    tax
    lda.w rbase,x
    and #$00FF
    sta.w dw_y0
    lda.w pyy,x
    and #$00FF
    clc
    adc.w dw_y0
    asl a
    asl a
    sta.w dw_y0
    txa
    jsr window
    lda.w ov,y
    eor.w pmask,x
    sta.w ov,y
    lda.w ov+4,y
    eor.w pmask+2,x
    sta.w ov+4,y
    lda.w ov+8,y
    eor.w pmask+4,x
    sta.w ov+8,y
    lda.w ov+12,y
    eor.w pmask+6,x
    sta.w ov+12,y
    plp
    rtl

;---------------------------------------------------------------------------------
; void draw_piece_cells(u8 p): erase well p's piece and ghost, then draw the ghost at gy[p] and the piece at
; pyy[p] (the piece last, so it covers the ghost). The ghost only goes on empty cells: in a shared well it never
; covers a teammate's piece. The caller marks the rows dirty.
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
    lda #1
    sta.w dw_ghost
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
    stz.w dw_ghost
    jsr blocks
    plp
    rtl

;---------------------------------------------------------------------------------
; void erase_piece_cells(u8 p): well p's piece and ghost leave the screen
erase_piece_cells:
    php
    rep #$30
    lda 5,s
    and #$00FF
    sta.w dw_p
    jsr erase
    plp
    rtl

; empty the cells listed in drawn[dw_p * 8 ..] that still show the tile drawn there (in a shared well another
; piece may have covered them since), and forget them all; dw_slot = their byte offset
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
    phy
    asl a
    tay
    lda.w bgmap,y
    cmp.w drawn_t,x
    bne +
    lda #T_EMPTY
    sta.w bgmap,y
+   ply
    lda #$FFFF
    sta.w drawn,x
@next:
    inx
    inx
    dey
    bne @cell
    rts

; the 4 blocks of shape dw_s, box top on board row dw_y0, as tile dw_tile; their bgmap indices go to drawn and
; the tile to drawn_t, from byte offset dw_dst. Rows above the well (y < HID) are not drawn, nor, with dw_ghost
; set, cells that are not empty.
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
    sta.w dw_n              ; bgmap index of the cell
    asl a
    tax
    lda.w dw_ghost
    beq +
    lda.w bgmap,x
    cmp #T_EMPTY
    bne @skip
+   lda.w dw_tile
    sta.w bgmap,x
    ldx.w dw_dst
    sta.w drawn_t,x
    lda.w dw_n
    sta.w drawn,x
@skip:
    inc.w dw_dst
    inc.w dw_dst
    inc.w dw_k
    dey
    bne @block
    rts

;---------------------------------------------------------------------------------
; u8 lock_rows(u8 p): well p's piece joins the board, and its cells are no longer tracked as the moving piece
; (it stays on screen as blocks). In a shared well (solid set) it also leaves the falling-piece overlay, and
; teammates falling in its columns redraw (their ghosts may land on it now). Returns 255 if a block is above the
; well, else a bit mask of the piece's box rows (bit 0 = top) that are now full; those flash.
lock_rows:
    php
    rep #$30
    lda 5,s
    and #$00FF
    jsr wellsetup
    ldx.w dw_p
    lda.w pyy,x
    and #$00FF
    sta.w dw_s              ; box row
    clc
    adc.w dw_base
    asl a
    asl a
    sta.w dw_y0
    txa
    jsr window
    lda.w pmask,x
    ora.w rows,y
    sta.w rows,y
    lda.w pmask+2,x
    ora.w rows+4,y
    sta.w rows+4,y
    lda.w pmask+4,x
    ora.w rows+8,y
    sta.w rows+8,y
    lda.w pmask+6,x
    ora.w rows+12,y
    sta.w rows+12,y
    lda.w solid
    beq +
    lda.w ov,y
    eor.w pmask,x
    sta.w ov,y
    lda.w ov+4,y
    eor.w pmask+2,x
    sta.w ov+4,y
    lda.w ov+8,y
    eor.w pmask+4,x
    sta.w ov+8,y
    lda.w ov+12,y
    eor.w pmask+6,x
    sta.w ov+12,y
    phx
    jsr teammates
    plx
+   txa                     ; drawn[p * 8 ..] = none
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
    asl a
    tax
    lda.w rows,x
    ora.w rows+4,x
    eor.w dw_wlo
    bne @out
    lda.w rows+2,x
    ora.w rows+6,x
    eor.w dw_whi
    beq +
@out:
    lda #255
    sta.b tcc__r0
    plp
    rtl
+   lda.w dw_s
    clc
    adc.w dw_base
    asl a
    asl a
    tax
    lda #1
    sta.w dw_tile           ; bit of the current box row
    stz.w dw_k              ; full rows found
    ldy.w dw_s
@row:
    cpy #BOTTOM+1           ; the floor rows below are full too: stop there
    bcs @done
    lda.w rows,x
    and.w rows+2,x
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
    jsr fillw
    ply
    plx
@next:
    inx
    inx
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
; void collapse(u8 p): full rows of well p go away, the rows above fall into their place (masks, falling-piece
; overlay and cells). In a shared well, teammates' falling pieces move down with the rows they are over: the
; cell copy carries them and their ghosts along, and remap updates where they are (no redraw needed).
collapse:
    php
    rep #$30
    lda 5,s
    and #$00FF
    jsr wellsetup
    jsr markdrawn
    lda #BOTTOM
    sta.w dw_k              ; source row
    sta.w dw_dst            ; destination row
@row:
    lda.w dw_k
    clc
    adc.w dw_base
    asl a
    asl a
    tax
    lda.w rows,x
    and.w rows+2,x
    cmp #$FFFF
    beq @gone
    lda.w dw_k              ; newrow[k] = destination
    asl a
    tax
    lda.w dw_dst
    sta.w newrow,x
    cmp.w dw_k
    beq +
    jsr copyrow
+   dec.w dw_dst
    bra @up
@gone:
    lda.w dw_k
    asl a
    tax
    lda #$FFFF
    sta.w newrow,x
@up:
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
    asl a
    tax
    lda.w dw_wlo
    sta.w rows,x
    lda.w dw_whi
    sta.w rows+2,x
    stz.w ov,x
    stz.w ov+2,x
    lda.w dw_dst
    cmp #HID
    bcc +
    jsr rowcells
    lda #T_EMPTY
    jsr fillw
+   dec.w dw_dst
    bra @fill
@end:
    jsr remap
    lda.w dw_base           ; dmark back to 0
    asl a
    asl a
    tax
    ldy #BOTTOM+1
-   stz.w dmark,x
    inx
    inx
    inx
    inx
    dey
    bne -
    plp
    rtl

; dmark (laid out like rows) = 1 on the board rows holding a cell drawn for a piece or ghost on this board:
; copyrow must move those rows even when they look empty
markdrawn:
    stz.w dw_n              ; player
@q:
    ldx.w dw_n
    lda.w rbase,x
    and #$00FF
    cmp.w dw_base
    bne @nextq
    txa
    asl a
    asl a
    asl a
    asl a
    tax                     ; byte offset of drawn[q * 8]
    ldy #8
@slot:
    lda.w drawn,x
    cmp #$FFFF
    beq +
    lsr a                   ; screen row
    lsr a
    lsr a
    lsr a
    lsr a
    sec
    sbc #WELL_TOP-HID       ; board row
    clc
    adc.w dw_base
    asl a
    asl a
    phx
    tax
    lda #1
    sta.w dmark,x
    plx
+   inx
    inx
    dey
    bne @slot
@nextq:
    inc.w dw_n
    lda.w dw_n
    cmp #4
    bne @q
    rts

; after a collapse of the board at dw_base: each falling piece on it went down with its rows (newrow[old row] =
; new row, $FFFF for a row that went away). Its box row, its ghost's and its drawn cells follow. A piece whose
; box reached into the hidden rows gets redrawn (a cell that was hidden may show now); a ghost on a row that
; went away (it was out of date) is looked for again.
remap:
    stz.w dw_n              ; player
@q:
    ldx.w dw_n
    lda.w rbase,x
    and #$00FF
    cmp.w dw_base
    beq +
    brl @nextq
+   lda.w phase,x
    and #$00FF
    cmp #W_FALL
    beq +
    cmp #W_LOCK
    beq +
    brl @nextq
+   txa                     ; dw_s = first box row holding blocks
    asl a
    asl a
    asl a
    tax
    ldy #0
-   lda.w pmask,x
    bne +
    inx
    inx
    iny
    cpy #3
    bne -
+   sty.w dw_s
    ldx.w dw_n
    lda.w pyy,x
    and #$00FF
    sta.w dw_y0
    jsr newbox
    sep #$20
    sta.w pyy,x
    rep #$20
    cmp.w dw_y0
    beq +
    lda.w dw_y0
    cmp #HID
    bcs +
    lda #1
    jsr redraw
+   lda.w gy,x
    and #$00FF
    jsr newbox
    bpl +
    lda #3                  ; the ghost was on a cleared row
    jsr redraw
    bra @cells
+   sep #$20
    sta.w gy,x
    rep #$20
@cells:
    txa
    asl a
    asl a
    asl a
    asl a
    tax                     ; byte offset of drawn[q * 8]
    lda #8
    sta.w dw_k
@slot:
    lda.w drawn,x
    cmp #$FFFF
    beq @nexts
    sta.w dw_tile
    lsr a
    lsr a
    lsr a
    lsr a
    lsr a
    sec
    sbc #WELL_TOP-HID
    sta.w dw_y0             ; board row of the cell
    asl a
    tay
    lda.w newrow,y
    bmi @gone
    sec
    sbc.w dw_y0             ; rows it moved down
    asl a
    asl a
    asl a
    asl a
    asl a
    clc
    adc.w dw_tile
    sta.w drawn,x
    bra @nexts
@gone:
    lda #$FFFF
    sta.w drawn,x
@nexts:
    inx
    inx
    dec.w dw_k
    bne @slot
@nextq:
    inc.w dw_n
    lda.w dw_n
    cmp #4
    beq +
    brl @q
+   rts

; A = box row of a piece or ghost whose blocks start at its box row dw_s: A = its box row once that row moved
; (negative if it went away). X = player, kept.
newbox:
    clc
    adc.w dw_s
    asl a
    tay
    lda.w newrow,y
    bmi +
    sec
    sbc.w dw_s
+   rts

; dirty[X] |= A (X = player, kept)
redraw:
    sep #$20
.ACCU 8
    ora.w dirty,x
    sta.w dirty,x
    rep #$20
.ACCU 16
    rts

;---------------------------------------------------------------------------------
; void add_junk_rows(u8 p, u8 n, u8 gap): the stack of well p (7 columns) rises n rows (1..20), n junk rows
; come in at the bottom, all with the gap in column gap
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
    asl a
    tax
    lda.w dw_tile
    sta.w rows,x
    lda #$FFFF
    sta.w rows+2,x
    lda.w dw_dst
    jsr rowcells
    phy
    lda #T_JUNK
    jsr fillw
    pla
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
    ldy.w dw_w
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
; u8 read_pads(u8 n): pads 0..n-1 as the game sees them. keys_down holds the presses since the previous call
; (comparing with the last frame the game saw, not PVSnesLib's per-VBlank padsDown, so a late frame can't lose
; one), plus those kept from the previous frame (keys_held). A touched pad gets touched[p] = 1, idle[p] = 0, brk[p] = 0; an untouched one counts idle[p] up to
; BREAK_FRAMES. Returns a bit per pad with a new Start, Select, Left or Right press, for the C code.
read_pads:
    php
    rep #$30
    lda 5,s
    and #$00FF
    sta.w dw_n
    stz.w dw_k              ; result
    lda #1
    sta.w dw_tile           ; bit of pad p
    ldy #0                  ; p
    ldx #0                  ; p * 2
@pad:
    cpy.w dw_n
    beq @end
    lda.l pad_keys,x
    sta.w keys_now,x
    sta.w dw_s
    lda.w keys_before,x
    eor #$FFFF
    and.w dw_s
    ora.w keys_held,x
    sta.w keys_down,x
    stz.w keys_held,x
    bit #KEYS_C
    beq +
    lda.w dw_k
    ora.w dw_tile
    sta.w dw_k
+   lda.w dw_s
    sta.w keys_before,x
    beq @untouched
    stz.w idle,x
    sep #$20
.ACCU 8
    lda #1
    sta.w touched,y
    dec a
    sta.w brk,y
    rep #$20
.ACCU 16
    bra @next
@untouched:
    lda.w idle,x
    cmp #BREAK_FRAMES
    bcs @next
    inc a
    sta.w idle,x
@next:
    asl.w dw_tile
    iny
    inx
    inx
    bra @pad
@end:
    lda.w dw_k
    sta.b tcc__r0
    plp
    rtl

;---------------------------------------------------------------------------------
; u8 find_top(u8 p): the highest visible row of p's board that holds a block (BOTTOM + 1 if none)
find_top:
    php
    rep #$30
    lda 5,s
    and #$00FF
    jsr wellsetup
    lda.w dw_base
    clc
    adc #HID
    asl a
    asl a
    tax
    ldy #HID
-   lda.w rows,x
    cmp.w dw_wlo
    bne +
    lda.w rows+2,x
    cmp.w dw_whi
    bne +
    inx
    inx
    inx
    inx
    iny
    cpy #BOTTOM+1
    bne -
+   sty.b tcc__r0
    plp
    rtl

;---------------------------------------------------------------------------------
; void draw_next_cells(u8 p, u8 t): piece t in player p's next-piece box (4x2 cells right of the Woka)
draw_next_cells:
    php
    rep #$30
    lda 6,s
    and #$00FF
    sta.w dw_s
    lda 5,s
    and #$00FF
    tax
    lda.w zcol,x
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

; dirty[q] = 3 for every teammate q falling on dw_p's board whose box columns overlap dw_p's piece's
teammates:
    ldy.w dw_p
    lda.w pcol,y
    and #$00FF
    sta.w dw_n              ; pcol[p]
    ldx #0
@q:
    cpx.w dw_p
    beq @next
    lda.w phase,x
    and #$00FF
    cmp #W_FALL
    bne @next
    lda.w rbase,x
    and #$00FF
    cmp.w dw_base
    bne @next
    lda.w pcol,x
    and #$00FF
    sta.w dw_tile           ; pcol[q]
    lda.w dw_n
    clc
    adc #4
    cmp.w dw_tile
    beq @next
    bcc @next               ; p's box ends left of q's
    lda.w dw_tile
    clc
    adc #4
    cmp.w dw_n
    beq @next
    bcc @next               ; q's box ends left of p's
    lda #3
    jsr redraw
@next:
    inx
    cpx #4
    bne @q
    rts

; A = p, dw_y0 = byte offset of the box's top row in rows[]: X = p * 8 (byte offset of pmask[p * 4]),
; dw_k = pk[p], Y = dw_y0 + pk[p] (the piece's window on that row)
window:
    tax
    lda.w pk,x
    and #$00FF
    sta.w dw_k
    clc
    adc.w dw_y0
    tay
    txa
    asl a
    asl a
    asl a
    tax
    rts

; Z set if the 4 masks at pmask+X fit on the 4 rows from byte Y: rows | (ov & solid)
test4:
    lda.w solid
    bne test4ov
test4board:
    lda.w pmask,x
    and.w rows,y
    bne +
    lda.w pmask+2,x
    and.w rows+4,y
    bne +
    lda.w pmask+4,x
    and.w rows+8,y
    bne +
    lda.w pmask+6,x
    and.w rows+12,y
+   rts
test4ov:
    lda.w ov,y
    and.w solid
    ora.w rows,y
    and.w pmask,x
    bne +
    lda.w ov+4,y
    and.w solid
    ora.w rows+4,y
    and.w pmask+2,x
    bne +
    lda.w ov+8,y
    and.w solid
    ora.w rows+8,y
    and.w pmask+4,x
    bne +
    lda.w ov+12,y
    and.w solid
    ora.w rows+12,y
    and.w pmask+6,x
+   rts

; A = p: dw_p, dw_base = rbase[p], dw_col = col0[p], dw_w = wcells[p], dw_wlo / dw_whi = its empty row
wellsetup:
    sta.w dw_p
    tax
    lda.w rbase,x
    and #$00FF
    sta.w dw_base
    lda.w col0,x
    and #$00FF
    sta.w dw_col
    lda.w wcells,x
    and #$00FF
    sta.w dw_w
    txa
    asl a
    tax
    lda.w wall_lo,x
    sta.w dw_wlo
    lda.w wall_hi,x
    sta.w dw_whi
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

; A = tile into the dw_w cells from bgmap byte offset Y (X and Y change)
fillw:
    ldx.w dw_w
-   sta.w bgmap,y
    iny
    iny
    dex
    bne -
    rts

; board row dw_k to row dw_dst of the well at dw_base / dw_col: the mask, the falling-piece overlay, and the
; cells when dw_dst is visible. An empty row over an empty one changes nothing (the empty top of a wide well costs
; no copying): empty means no blocks, no falling piece and no cell drawn for one (dmark).
copyrow:
    lda.w dw_k
    clc
    adc.w dw_base
    asl a
    asl a
    tax
    lda.w dw_dst
    clc
    adc.w dw_base
    asl a
    asl a
    tay
    lda.w rows,x
    cmp.w dw_wlo
    bne @copy
    cmp.w rows,y
    bne @copy
    lda.w rows+2,x
    cmp.w dw_whi
    bne @copy
    cmp.w rows+2,y
    bne @copy
    lda.w ov,x
    ora.w ov+2,x
    ora.w ov,y
    ora.w ov+2,y
    ora.w dmark,x
    ora.w dmark,y
    bne @copy
    rts
@copy:
    lda.w rows,x
    sta.w rows,y
    lda.w rows+2,x
    sta.w rows+2,y
    lda.w ov,x
    sta.w ov,y
    lda.w ov+2,x
    sta.w ov+2,y
    lda.w dw_dst
    cmp #HID
    bcc @done
    jsr rowcells
    lda.w dw_k
    cmp #HID
    bcc @empty
    tya                     ; destination address
    clc
    adc #loword(bgmap)
    pha
    lda.w dw_k
    jsr rowcells
    tya                     ; source address
    clc
    adc #loword(bgmap)
    tax
    ply
    lda.w dw_w              ; MVN moves A + 1 bytes from X to Y (both in bank $7E)
    asl a
    dec a
    phb
    mvn $7E, $7E
    plb
@done:
    rts
@empty:
    lda #T_EMPTY
    jmp fillw

.ENDS
