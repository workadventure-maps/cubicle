;---------------------------------------------------------------------------------
; Music control for our altered SNESMOD driver (snesmod/sm_spc.asm, built to snesmod/sm_spc.bin).
; PVSnesLib's spcBoot uploads its own copy of the driver, so spcBootMusic is that function with our driver
; instead; every other spc* function of PVSnesLib works unchanged with it. The two commands the driver adds go
; through PVSnesLib's message queue (QueueMessage) and leave on the next spcProcess().
; Adapted from PVSnesLib 4.6.0 source/snesmodwla.asm (SNESMod by mukunda, WLA DX port by KungFuFurby and
; Alekmaul), zlib license: see snesmod/sm_spc.asm.
;---------------------------------------------------------------------------------
.include "hdr.asm"

.equ REG_APUIO0 $2140
.equ REG_APUIO1 $2141
.equ REG_APUIO2 $2142
.DEFINE SPC_BOOT $0400 ; spc entry/load address
.DEFINE CMD_TEMPO $0C
.DEFINE CMD_NEXT $0D

.SECTION ".music_driver" SUPERFREE
music_driver:
.incbin "snesmod/sm_spc.bin" FSIZE MUSIC_DRIVER_SIZE
.ENDS

.SECTION ".music_text" SUPERFREE

.index 16
.accu 8

;---------------------------------------------------------------------------------
; void spcBootMusic(void): PVSnesLib's spcBoot with our driver
spcBootMusic:
	php
	sei
	phb
	sep	#$20
	lda	#$0		; (spcBoot also clears $4200 with the caller's data bank, $7E: a WRAM byte, so
	pha			; NMI stays on; a real write would stop WaitForVBlank. The handshakes don't need it.)
	plb			; data bank 0

-:	ldx	REG_APUIO0	; wait for 'ready' signal from the SPC
	cpx	#$BBAA
	bne	-
	stx	REG_APUIO1	; start transfer: port1 = !0
	ldx	#SPC_BOOT	; port2,3 = transfer address
	stx	REG_APUIO2
	lda	#$CC		; port0 = $CC
	sta	REG_APUIO0
-:	cmp	REG_APUIO0	; wait for the SPC
	bne	-

	lda.l	music_driver	; first byte
	xba
	lda	#0
	ldx	#1
	bra	@start
@send:
	xba			; data into A
	lda.l	music_driver, x	; next byte
	inx
	xba
-:	cmp	REG_APUIO0	; wait for the SPC
	bne	-
	ina			; counter (port0 data)
@start:
	rep	#$20		; write port0+port1 data
	sta	REG_APUIO0
	sep	#$20
	cpx	#MUSIC_DRIVER_SIZE
	bcc	@send

-:	cmp	REG_APUIO0	; wait for the SPC
	bne	-
	ina
	ina
	stz	REG_APUIO1	; port1 = 0, so no invalid $80 message
	ldx	#SPC_BOOT	; port2,3 = entry point
	stx	REG_APUIO2
	sta	REG_APUIO0
-:	cmp	REG_APUIO0	; final sync
	bne	-
	stz	REG_APUIO0

	; reset PVSnesLib's side, as spcBoot does
	stz.w	spc_v
	stz.w	spc_q
	stz.w	spc_fwrite
	stz.w	spc_fread
	stz.w	spc_sfx_next
	stz.w	spc_pr+0
	stz.w	spc_pr+1
	stz.w	spc_pr+2
	stz.w	spc_pr+3
	stz.w	spc_ptr+0
	stz.w	spc_ptr+1
	stz.w	spc_ptr+2
	stz.w	spc_bank
	stz.w	spc1+0
	stz.w	spc1+1
	stz.w	spc2+0
	stz.w	spc2+1
	stz.w	digi_src+0
	stz.w	digi_src+1
	stz.w	digi_src+2
	stz.w	digi_src2+0
	stz.w	digi_src2+1
	stz.w	digi_src2+2
	stz.w	SoundTable+0
	stz.w	SoundTable+1
	stz.w	SoundTable+2
	stz.w	digi_init
	stz.w	digi_pitch
	stz.w	digi_vp
	stz.w	digi_remain+0
	stz.w	digi_remain+1
	stz.w	digi_active
	stz.w	digi_copyrate
	ldx	#$0
	lda	#$0
-:	sta.w	spc_fifo, x
	inx
	cpx	#$ff
	bne	-

	plb
	cli
	plp
	rtl

;---------------------------------------------------------------------------------
; void spcSetTempo(u8 bpm): 81 to 255
spcSetTempo:
	php
	phb
	sep	#$20
	lda	#$0
	pha
	plb			; data bank 0, as QueueMessage expects
	lda	6,s
	sta.w	spc1+1		; goes to port 3
	lda	#CMD_TEMPO
	jml	QueueMessage	; queues, then plb/plp/rtl

;---------------------------------------------------------------------------------
; void spcQueueOrder(u8 order): the order to play when the current pattern ends ($FE: stop there)
spcQueueOrder:
	php
	phb
	sep	#$20
	lda	#$0
	pha
	plb
	lda	6,s
	sta.w	spc1+1
	lda	#CMD_NEXT
	jml	QueueMessage

.ENDS

