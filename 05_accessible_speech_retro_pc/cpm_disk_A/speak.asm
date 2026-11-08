;	SPEAK.COM - CP/M 2.2 utility for the RP2350 Core 1 Virtual DoubleTalk
;
;	A>SPEAK [/Sn] [/Pnn] [/Vn] [/On] text...
;
;	Options (any order, before the text) become DoubleTalk commands:
;	  /Sn   speed 0..9        /Pnn  pitch 0..99
;	  /Vn   volume 0..9       /On   voice 0..7 (0 Paul, 1 Betty, 2 Harry ...)
;	  /X    silence (flush) first
;	Options alone just set the voice; text alone is spoken.  No arguments
;	prints and speaks the usage line.
;
;	The host BDOS call 224 (0E0h) streams the NUL-terminated string at DE
;	to the synthesizer port byte for byte (doubletalk_write_string); CR
;	is the clause trigger.  Control codes inside the string are passed
;	through, so this program is just a convenience for SUBMIT files and
;	MBASIC users.  On a plain CP/M 2.2 (no host) call 224 does nothing.
;
;	The program switches to its own stack, so it must leave through the warm-boot
;	vector (JP 0000h), never RET: a RET would pop uninitialised memory.  (Found by
;	running it under RunCPM, where the first version looped forever.)
;
;	Assemble with z80pack's z80asm:  z80asm -fb -ospeak.com speak.asm
;	Test build (TEST defined): uses BDOS 9 instead of 224 and ends the
;	buffer with '$', so the exact byte stream shows on the console.
;
BDOS	EQU	0005H
TAIL	EQU	0080H		; command tail: length byte, then text
CONOUT	EQU	2
PRTSTR	EQU	9
SPKSTR	EQU	224		; host: speak NUL-terminated string at DE
CR	EQU	0DH
LF	EQU	0AH
;
	ORG	0100H
;
START:	LD	SP,STACK
	LD	HL,TAIL
	LD	A,(HL)		; tail length
	OR	A
	JP	Z,USAGE
	LD	C,A		; C = remaining tail bytes
	INC	HL		; HL -> first tail byte
	LD	DE,BUF		; DE -> output buffer
;
; skip leading blanks
SKIPB:	LD	A,(HL)
	CP	' '
	JR	NZ,OPTS
	INC	HL
	DEC	C
	JR	NZ,SKIPB
	JP	FINISH		; only blanks: nothing to say
;
; options: a '/' introduces one; anything else starts the text
OPTS:	LD	A,(HL)
	CP	'/'
	JR	NZ,TEXT
	INC	HL
	DEC	C
	JP	Z,FINISH
	LD	A,(HL)		; option letter
	INC	HL
	DEC	C
	AND	5FH		; upper case
	CP	'X'
	JR	NZ,OPTNUM
	LD	A,18H		; /X -> ^X flush byte
	LD	(DE),A
	INC	DE
	JR	OPTEND
OPTNUM:	LD	B,A		; B = command letter
	LD	A,01H		; ^A command prefix
	LD	(DE),A
	INC	DE
; copy the decimal digits that follow
DIGITS:	LD	A,C
	OR	A
	JR	Z,OPTLET
	LD	A,(HL)
	CP	'0'
	JR	C,OPTLET
	CP	'9'+1
	JR	NC,OPTLET
	LD	(DE),A
	INC	DE
	INC	HL
	DEC	C
	JR	DIGITS
OPTLET:	LD	A,B		; the DoubleTalk command letter (S, P, V, O ...)
	LD	(DE),A
	INC	DE
OPTEND:	LD	A,C
	OR	A
	JP	Z,FINISH
	JR	SKIPB
;
; text: copy the rest of the tail, then CR
TEXT:	LD	A,(HL)
	LD	(DE),A
	INC	DE
	INC	HL
	DEC	C
	JR	NZ,TEXT
	LD	A,CR
	LD	(DE),A
	INC	DE
;
FINISH:	LD	A,TERM		; NUL (or '$' in the test build)
	LD	(DE),A
	LD	DE,BUF
	LD	C,SPKFN
	CALL	BDOS
	JP	0000H		; warm boot back to the CCP (own stack: no RET)
;
USAGE:	LD	A,TERM		; speak it: terminate with NUL (BDOS 224)
	LD	(UEND),A
	LD	DE,UMSG
	LD	C,SPKFN
	CALL	BDOS
	LD	A,'$'		; print it: terminate with $ (BDOS 9)
	LD	(UEND),A
	LD	DE,UMSG
	LD	C,PRTSTR
	CALL	BDOS
	JP	0000H
;
	IFDEF	TEST
SPKFN	EQU	PRTSTR
TERM	EQU	'$'
	ELSE
SPKFN	EQU	SPKSTR
TERM	EQU	0
	ENDIF
;
UMSG:	DEFM	'SPEAK: RP2350 Core 1 DECtalk. Usage: SPEAK [/Sn] [/Pnn] [/Vn] [/On] [/X] text'
	DEFB	CR,LF
UEND:	DEFB	'$'
;
BUF:	DEFS	140		; 128-byte tail + command bytes + CR + terminator
	DEFS	32
STACK:
	END	START
