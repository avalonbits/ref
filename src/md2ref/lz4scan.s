;
; LZ4's search for a match, on the Agon: lz4w.c's scan, which the PC build
; runs, and which this does exactly -- test/md2ref.sh holds the document
; the Agon writes to the PC's, byte for byte.
;
; Nearly every byte of every topic passes through here: its four bytes'
; hash, the table of positions read and written, and the position the
; table gave compared. agondev's clang kept it all in the frame, at some
; 300 cycles a byte. By hand every value lives in a register:
;
;   IY  p, the position             B   the hash's low half
;   IX  v, the position + base + 1  C   the hash's high half
;
; and the hash rolls: the next position's is this one's rotated, with the
; byte leaving taken out and the byte coming in put in (see lz4w.c).
;
;   unsigned lz4_scan(const uint8_t *src, unsigned n, unsigned *pi,
;                     unsigned *pcand)
;
; From *pi on -- and *pi + 4 <= n, which the caller sees to -- until a
; position whose table entry starts a match of 4 bytes or more: its length,
; the position in *pi and the match's start + 1 in *pcand. 0 when none
; does before the last four bytes, with *pi the first position past them.

	.assume adl=1
	.section .text,"ax",@progbits
	.global _lz4_scan

T_PAGE	.equ	_lz4_tab
R_PAGE	.equ	_lz4_tab + 256
U_PAGE	.equ	_lz4_tab + 512
V_PAGE	.equ	_lz4_tab + 768

_lz4_scan:
	push ix
	ld ix, 0
	add ix, sp
	push iy
	ld (frame), ix

	ld de, (ix+6)
	ld (src), de
	ld hl, (ix+12)
	ld hl, (hl)			; i
	push hl
	add hl, de
	push hl
	pop iy				; p = src + i
	ld hl, (_lz4_head_base)
	inc hl
	ld (base1), hl
	pop de				; i
	add hl, de
	push hl				; v = base1 + i
	ld hl, (base1)
	ld de, (ix+9)
	add hl, de
	ld de, 3
	or a, a
	sbc hl, de
	ld (vstop), hl			; base1 + n - 3: the first v past the end
	; The table's entry is at lz4_head + the hash's offset, which is
	; built in HL with the tables' top byte above it: headadj takes that
	; away again as it adds lz4_head.
	ld hl, _lz4_tab
	ld l, 0
	ld h, 0
	ex de, hl
	ld hl, _lz4_head
	or a, a
	sbc hl, de
	ld (headadj), hl
	pop ix				; v

fresh:					; the hash of p's four bytes, afresh
	ld hl, T_PAGE
	ld l, (iy+0)
	ld b, (hl)
	ld h, U_PAGE >> 8
	ld c, (hl)
	ld l, (iy+1)
	ld a, c
	rlca
	xor a, (hl)
	ld c, a
	ld h, T_PAGE >> 8
	ld a, b
	rlca
	xor a, (hl)
	ld b, a
	ld l, (iy+2)
	ld a, b
	rlca
	xor a, (hl)
	ld b, a
	ld h, U_PAGE >> 8
	ld a, c
	rlca
	xor a, (hl)
	ld c, a
	ld l, (iy+3)
	ld a, c
	rlca
	xor a, (hl)
	ld c, a
	ld h, T_PAGE >> 8
	ld a, b
	rlca
	xor a, (hl)
	ld b, a

position:
	ld hl, T_PAGE			; for its top byte, which headadj undoes
	ld a, c
	and a, 0x1F
	ld h, a
	ld a, b
	and a, 0xFE
	ld l, a
	ld de, (headadj)
	add hl, de			; the entry
	ld de, 0
	ld e, (hl)
	ld a, ixl
	ld (hl), a
	inc hl
	ld d, (hl)
	ld a, ixh
	ld (hl), a			; v in its place; what was there in DE
	ex de, hl
	ld de, (base1)
	or a, a
	sbc hl, de			; at or past base1: this call's, and where
	jr c, next
	ld de, (src)
	add hl, de
	ld a, (iy+0)
	cp a, (hl)
	jr nz, next
	inc hl
	ld a, (iy+1)
	cp a, (hl)
	jr nz, next
	inc hl
	ld a, (iy+2)
	cp a, (hl)
	jr nz, next
	inc hl
	ld a, (iy+3)
	cp a, (hl)
	jr z, found

next:
	inc ix
	push ix
	pop de
	ld hl, (vstop)
	or a, a
	sbc hl, de
	jr z, none
	ld hl, R_PAGE			; roll: out goes p's first byte
	ld l, (iy+0)
	ld a, b
	rlca
	xor a, (hl)
	ld b, a
	ld h, V_PAGE >> 8
	ld a, c
	rlca
	xor a, (hl)
	ld c, a
	ld l, (iy+4)			; and in comes the one after its four
	ld h, U_PAGE >> 8
	ld a, c
	xor a, (hl)
	ld c, a
	ld h, T_PAGE >> 8
	ld a, b
	xor a, (hl)
	ld b, a
	inc iy
	jr position

none:
	ld hl, 0
	jr out

found:					; HL is the match's start + 3
	push hl
	ld de, (src)
	or a, a
	sbc hl, de
	dec hl
	dec hl
	ld (cand), hl			; its start + 1
	ld hl, (vstop)			; the bytes after these four: n - i - 4
	push ix
	pop de
	or a, a
	sbc hl, de
	dec hl
	push hl
	pop bc
	pop hl
	inc hl				; the match's start + 4
	lea de, iy+4			; p + 4
	ld a, b
	or a, c				; BC is under 65536: n is
	jr nz, more
	ld hl, 4			; no more bytes: just the four
	jr report

more:
	ld a, (de)
	inc de
	cpi
	jr nz, differ
	jp pe, more
equal_all:				; every byte to the end matched
	ex de, hl
	lea de, iy+4
	or a, a
	sbc hl, de
	ld de, 4
	add hl, de
	jr report
differ:					; DE is one past the byte that differs
	ex de, hl
	lea de, iy+5
	or a, a
	sbc hl, de
	ld de, 4
	add hl, de

report:					; HL the length
	push hl
	push ix
	pop hl
	ld de, (base1)
	or a, a
	sbc hl, de			; i
	ex de, hl
	ld ix, (frame)
	ld hl, (ix+12)
	ld (hl), de
	ld hl, (ix+15)
	ld de, (cand)
	ld (hl), de
	pop hl
	jr done

out:					; HL 0, v at vstop: i = n - 3
	push hl
	push ix
	pop hl
	ld de, (base1)
	or a, a
	sbc hl, de
	ex de, hl
	ld ix, (frame)
	ld hl, (ix+12)
	ld (hl), de
	pop hl

done:
	ld sp, ix
	ld iy, (ix-3)
	pop ix
	ret

	.section .bss,"aw",@nobits
frame:	.skip 3
src:	.skip 3
base1:	.skip 3
vstop:	.skip 3
headadj: .skip 3
cand:	.skip 3
