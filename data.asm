.include "hdr.asm"

.section ".rodata1" superfree

blocks_a_til:
.incbin "gfx/blocks_a.pic"
blocks_a_pal:
.incbin "gfx/blocks_a.pal"
blocks_b_til:
.incbin "gfx/blocks_b.pic"
blocks_b_pal:
.incbin "gfx/blocks_b.pal"
blocks_p_pal:
.incbin "gfx/blocks_p.pal"
walls_til:
.incbin "gfx/walls.pic"
walls_pal:
.incbin "gfx/walls.pal"

font_til:
.incbin "gfx/font.pic"
font_tilend:
font_pal:
.incbin "gfx/font.pal"

p1_pal:
.incbin "gfx/p1.pal"
p2_pal:
.incbin "gfx/p2.pal"
p3_pal:
.incbin "gfx/p3.pal"
p4_pal:
.incbin "gfx/p4.pal"

.ends

.section ".rodata2" superfree

p1_til:
.incbin "gfx/p1.pic"
p2_til:
.incbin "gfx/p2.pic"
p3_til:
.incbin "gfx/p3.pic"
p4_til:
.incbin "gfx/p4.pic"

.ends

.section ".rodata3" superfree

sfx_lock:
.incbin "sfx/lock.brr"
sfx_lock_end:
sfx_clear:
.incbin "sfx/clear.brr"
sfx_clear_end:
sfx_junk:
.incbin "sfx/junk.brr"
sfx_junk_end:
sfx_join:
.incbin "sfx/join.brr"
sfx_join_end:
sfx_topout:
.incbin "sfx/topout.brr"
sfx_topout_end:

.ends
