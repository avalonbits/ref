;
; inline.c's copy_text on the Agon: text into markup, a word's start and
; end noted as it goes -- exactly what the C does, which the PC build runs
; and test/md2ref.sh holds the Agon's documents to, byte for byte.
;
; Nearly every byte of every page passes through here. agondev's clang
; kept both pointers in the frame, at some 55 cycles a byte; by hand,
;
;   HL  the text                 IY  the class table, its low byte the byte's
;   DE  the markup               A   the byte's class
;   BC  what is left of the text
;
; and LDI copies a byte, moves both on and counts it, in one. A space after
; a space is passed over (CPI), as put_char drops it: two loops between
; words, one after a space, keep which it is, so no register does.
;
;   uint8_t *copy_text_asm(const uint8_t *s, const uint8_t *e, uint8_t *o,
;                          const uint8_t *cls)
;
; From s to e, or to the first byte cls says stops it, into o: where in o it
; got to; cw_to where in s; cw_space whether the last written was a space.
; A word -- bytes of class C_WORD or C_CAP -- starts at cw_start when cw_on
; (cw_odd 1 if a capital is in it), and is given to cw_end as it ends; one
; still being written where the copy stops stays open, as in C.

	.assume adl=1
	.section .text,"ax",@progbits
	.global _copy_text_asm

C_STOP	.equ	0
C_SPACE	.equ	1
C_GAP	.equ	2
C_WORD	.equ	3
C_CAP	.equ	4

_copy_text_asm:
	push ix
	ld ix, 0
	add ix, sp
	push iy
	ld hl, (ix+9)			; e
	ld de, (ix+6)			; s
	or a, a
	sbc hl, de
	jp z, empty
	push hl
	pop bc				; BC: what is left
	ex de, hl			; HL: s
	ld de, (ix+12)			; DE: o
	ld iy, (ix+15)			; IY: the table, aligned to 256
	ld a, (_cw_start+2)		; a word open: RAM's top byte is never 0
	or a, a
	jr nz, word
	ld a, (_cw_space)
	or a, a
	jr nz, gap_sp

gap:					; between words, the last written no space
	ld a, (hl)
	ld iyl, a
	ld a, (iy+0)
	cp a, C_GAP
	jr nz, gap_other
	ldi
	jp pe, gap
	jp done_ns
gap_other:
	cp a, C_SPACE
	jr nz, gap_word
	ldi				; the space
	jp pe, gap_sp
	jp done_sp

gap_sp:					; between words, the last written a space
	ld a, (hl)
	ld iyl, a
	ld a, (iy+0)
	cp a, C_GAP
	jr nz, sp_other
	ldi
	jp pe, gap
	jp done_ns
sp_other:
	cp a, C_SPACE
	jr nz, sp_word
	cpi				; a space after a space: passed over
	jp pe, gap_sp
	jp done_sp
sp_word:
	or a, a
	jp z, done_sp			; stopped, after a space
	jr start_word

gap_word:
	or a, a
	jp z, done_ns			; stopped
start_word:				; a word starts at DE
	ld a, (_cw_on)
	or a, a
	jr z, word
	ld (_cw_start), de
	xor a, a
	ld (_cw_odd), a

word:					; in a word
	ld a, (hl)
	ld iyl, a
	ld a, (iy+0)
	cp a, C_WORD
	jr nz, word_other
	ldi
	jp pe, word
	jr done_ns			; the word stays open
word_other:
	cp a, C_CAP
	jr nz, word_ends
	ld a, 1
	ld (_cw_odd), a
	ldi
	jp pe, word
	jr done_ns
word_ends:				; A: stop, space or between words
	or a, a
	jr z, done_ns			; stopped: the word stays open
	ld a, (_cw_start+2)
	or a, a
	jp z, gap			; none being taken
	push hl
	push bc
	push iy
	push de				; where it ends
	call _cw_end
	pop de
	pop iy
	pop bc
	pop hl
	jp gap				; this byte again, between words

empty:
	ld hl, (ix+6)
	ld (_cw_to), hl
	ld hl, (ix+12)
	jr out
done_sp:
	ld a, 1
	jr done
done_ns:
	xor a, a
done:
	ld (_cw_space), a
	ld (_cw_to), hl
	ex de, hl			; HL: where o got to
out:
	pop iy
	pop ix
	ret
