; SYSINFO81 
; 	GPL
; 	Oliver Lange
; 	Version 1.0.1

; Compile with "tasm -80 -b wspdrv.asm wspdrv.p" 


; driver for wespi
;


 
#define db .byte ;  cross-assembler definitions 
#define dw .word 
#define ds .block 
#define org .org 
#define end .end 



#define ADDR_DISABLE 0
#define UPDATE	01FCh	; LOAD/SAVE adress update subroutine in ROM

;;#define VERBOSE 1
 
org     $4009 ; BASIC PROGRAMM
;= System variables ============================================ 
 
   db 0     	;VERSN 
   dw 0     	;E_PPC 
   dw dfile      ;D_FILE 
   dw dfile+1    ;DF_CC 
   dw var   	;VARS 
   dw 0     	;DEST 
   dw var+1      ;E_LINE 
   dw last-1     ;c_ADD 
   dw 0     	;X_PTR 
   dw last  	;STKBOT 
   dw last  	;STKEND 
   db 0     	;BERG 
   dw membot     ;MEM 
   db 0     ;not used 
   db 2     ;DF_SZ 
   dw 1     ;S_TOP 
   db $FF,$FF,$FF     ;LAST_K 
   db 55    ;MARGIN 
   dw line10     ;NXTLIN   line10   dfile
   dw 0     ;OLDPPC 
   db 0     ;FLAGX 
   dw 0     ;STRLEN 
   dw $0C8D      ;T_ADDR 
   dw 0     ;SEED 
   dw $FFFF      ;FRAMES 
   db 0,0   ;COORDS 
   db $BC   ;PR_CC 
   db 33,24      ;S_POSN 
   db 01000000B  ;CDFLAG 
   ds 33    ;Print buffer --- now used for loader code, all loaded programs need to have the same !

membot: 
   ds 30    ;Calculator�s memory area 
   ds 2     ;not used 
 
;= First BASIC line, asm code ================================== 
 
line0: 
   db 0,0   ;line number 
   dw line10-$-2 ;line length 
   db $ea   ; REM 


#define ELINE	4014h  ; Systemvariable, die das Ende des abzuspeichernen BASIC-Programs anzeigt
#define ELINEHI	4015h  ; Systemvariable, die das Ende des abzuspeichernen BASIC-Programs anzeigt

#define SHOW	0207h  ; ROM-Routinen
#define FAST	02E7h
#define RCLS	0A2Ah
#define GETKEY	02BBh


#DEFINE RST_PRTCHAR RST 10H
#DEFINE c_SPACE 0
#DEFINE c_NEWLINE 76H
#DEFINE c_0 1CH

#DEFINE c_A 26H
#DEFINE c_B c_A+1
#DEFINE c_C c_A+2
#DEFINE c_D c_A+3
#DEFINE c_E c_A+4
#DEFINE c_F c_A+5
#DEFINE c_G c_A+6
#DEFINE c_H c_A+7
#DEFINE c_I c_A+8
#DEFINE c_J c_A+9
#DEFINE c_K c_J+1
#DEFINE c_L c_J+2
#DEFINE c_M c_J+3
#DEFINE c_N c_J+4
#DEFINE c_O c_J+5
#DEFINE c_P c_J+6
#DEFINE c_Q c_J+7
#DEFINE c_R c_J+8
#DEFINE c_S c_J+9
#DEFINE c_T c_S+1
#DEFINE c_U c_S+2
#DEFINE c_V c_S+3
#DEFINE c_W c_S+4
#DEFINE c_X c_S+5
#DEFINE c_Y c_S+6
#DEFINE c_Z c_S+7

#define UFM_ERRNO 16434 ; Definiert in UFM, Fehlercode 1 Byte (eigentlich SEED)
#define UFM_CMDBUF 16444 ; Definiert in UFM, 32 Byte, alternative Kommando�bergabe (eigentlich PRBUF)
#define UFM_COORDS 16438 ; L�ngen�bergabe 2 Byte (eigentlich COORDS)


;
;   === Main entry point ====
;

BASIC_START:
	CALL NAME	; get command line arg
	JR BASIC_CONT
	NOP
	NOP
	NOP

CALL_START:	; (IN UFM als START_ADDR+6 festgelegt)
	LD HL,UFM_CMDBUF
    PUSH HL
    ;  get length in BC by parsing for '"' or so..
    LD   BC,0
ENDPRSLP:
    LD   A,(HL)
    CP   11     ; is " ?
    JR   Z, EXITPRSLP
    INC  BC
    INC  HL
    JR   ENDPRSLP
EXITPRSLP:
    POP  HL ; original pointer
	CALL GENER_START
	LD (UFM_ERRNO),A
	RET

BASIC_CONT:
	CALL GENER_START ; A is 0 for ok, !=0 for error
	AND A
	JR Z,BAS_OK
	LD A,c_E		; Ausgabe ERR
	RST 10h 
	LD A,c_R
	RST 10h
    LD A,c_R
	RST 10h
    LD A,c_NEWLINE
	RST 10h
    ; exit
    XOR A
	RST 08h
    db  09h             ; Error Report: Invalid argument
	;db 0FFh

BAS_OK:
	RET  ; TODO check if RET or better RST8 with -1
;    XOR A		; sonst zurueck nach BASIC
;	RST 08h
;	db 0FFh
	

GENER_START: ; Interpreting the string independent of its origin, on ret A=0 for okay, BC retval to basic
	LD A,(HL)   ; HL=start, BC=length
    INC  HL
    DEC  BC
    LD   DE,GENER_END
    PUSH DE     ; ret adddess
;	CP c_I		; Info
;	JP Z,INFO1
	CP c_S		; Save
	JP Z,SAVE1
;	CP c_L		; Load
;	JP Z,LOAD1
;	CP 29h		; ist es ein D;
;	JP Z,DIR1
;	CP 3Bh		; ist es ein V (UFM)
;	JP Z,DIRV1
;	CP 30h		; ist es ein K (UFM)
;	JP Z,DIRK1
;	CP 37h		; ist es ein R
;	JP Z,RENAM1
;	CP 2Ah		; ist es ein E
;	JP Z,ERAS1
	CP c_H		; Help
	JP Z,HLP
	CP 0Fh		; ist es ein ?
	JP Z,HLP
    ret 

GENER_END:
    PUSH AF ; holds our error status
    PUSH BC ; ret value...
	CALL SHOW
    POP  BC
    POP  AF
	RET
	



CHECKCOMMA:  ; Z is set when comma seperator found in string, NZ if not, uses A
    ; BC =0?
    XOR  A
    CP   C
    JR   NZ,CHKK_CONT
    CP   B
    JR   NZ,CHKK_CONT
    INC  A ; clear the Z flag
    RET     ; no match till end
CHKK_CONT:
    LD   A, (HL)
    CP   26     ; comma
    RET  z
    CP   26     ; also check for semicolon
    RET  z
    INC  HL
    DEC  BC
    JR CHECKCOMMA


SKIPEMPTY:  ; Z is set when comma seperator found in string, NZ if not, uses A
    ; BC =0?
    XOR  A
    CP   C
    JR   NZ,CHKE_CONT
    CP   B
    JR   NZ,CHKE_CONT
    INC  A ; clear the Z flag
    RET     ; no match till end
CHKE_CONT:
    CP   (HL)
    RET  NZ ; not empty,leave
    INC  HL
    DEC  BC
    JR SKIPEMPTY

PRINTA:
    PUSH HL
    PUSH DE
    PUSH BC
    RST 10H
    POP  BC
    POP  DE
    POP  HL
    RET
    
ERREXIT:
    LD   A,1
    LD   BC,0
    RET

SAVE1:
	; HL points to arg string, BC number of chars
    PUSH HL  ; orig pos of args (w/o prefix S)
    PUSH BC  ; orig lenght of args (w/o prefix S)
    CALL CHECKCOMMA
    JR   Z, BINSAVE

    ; Check if we have contact
        
    LD   A, c_S
    CALL PRINTA
    
    CALL TRY_HANDSHAKE  ; See if WESPI responds, return 1 if so, 0 for timeout
    AND A
    ; Send SAVE request
    POP  BC
    POP  HL ; recover name pointer/length
    JR   Z, ERREXIT ; NO CONTACT after TRY_HANDSHAKE

    CALL SKIPEMPTY
    XOR  A
    CP   C
    JR   Z, ERREXIT ; NO NAME
    ld   B,C ; length, assume <256
    ld   C,91 ; packet ID ZX_QSAVE_TAG_SAVEPFILE
    call SEND_PACKET

    LD   A, c_A
    CALL PRINTA
	LD DE,4009h		; Get length
	LD HL,(ELINE)	
	AND A		; clear carry
	SBC HL,DE	; HL=length
	LD B,H
	LD C,L
	EX DE,HL	; Now HL=Start, BC=length
SVSENDFUL:
    LD   A, c_P
    CALL PRINTA
    XOR  A
    CP   B
    JR   Z, SVSENDLAST
    ; send full packets
    PUSH BC
    LD   B,0    ; 256 bytes
    LD   C,95 ; packet ID ZX_QSAVE_TAG_DATA
    call SEND_PACKET
    POP  BC
    DEC  B
    JR   SVSENDFUL

SVSENDLAST:
    LD   A, c_L
    CALL PRINTA
    XOR  A
    CP   C
    JR   Z, SVSENDEND
    LD   B,C ; length
    LD   C,95 ; packet ID ZX_QSAVE_TAG_DATA
    call SEND_PACKET

SVSENDEND:
    LD   A, c_V
    ;CALL PRINTA
    ;CALL QS_FINAL_ACK   ; Z set for success
    ;JR   NZ, ERREXIT
    XOR  A
    LD   BC, 1
	RET



BINSAVE: ; TODO PARSE ADDR, send header and go on to SVSENDFUL



HLPTXT:
	db c_Z,c_X,0,c_W,c_E,c_S,c_P,c_I,0,c_D,c_R,c_I,c_V,c_E,c_R,0, c_0,27,c_0+1,c_0,c_0,c_NEWLINE
    db c_NEWLINE
;	db "INFO  ",22h,"I",22h,0dh
;	db "DIR   ",22h,"D",22h,0dh
;	db "ERASE ",22h,"ENAME.P",22h,0dh
;	db "MKDIR ",22h,"DMVERZ",22h,0dh
;	db "RMDIR ",22h,"DEVERZ",22h,0dh
;	db "CHDIR ",22h,"DCVERZ",22h,0dh
;	db "LOAD  ",22h,"LNAME.P",22h,0dh
;	db "BLOAD ",22h,"LNAME.B,SSSS",22h,0dh
;	db "SAVE  ",22h,"SNAME.P",22h,0dh
;	db "BSAVE ",22h,"SNAME.B,SSSS,EEEE",22h,0dh
;	db "RENAME",22h,"ROLDNAME NEWNAME",22h,0dh
	db c_H,c_E,c_L,c_P, 0 , 0 , 0, 11, c_H, 11   ;  c_H,   "HELP  ",22h,"H",22h,0dh
;	db "FOR SIGGI'S UFM :V/R/K",0dh
    db c_NEWLINE
	db $ff


; === Subroutine print help text ====

HLP:	LD HL,HLPTXT
HLP1:	LD A,(HL)
	CP $FF
	JR Z, EXITHLP
    RST 10H
	INC HL
	JR HLP1
EXITHLP:
    LD   BC,42
    XOR  A
    RET

GO_QSAVE_MODE:
	CALL FAST	; go to fast mode
    IN      A,($FE)         ; signal to 0 pause    
    LD B,200  ; 200=200ms Pause
W1: push BC
    ld b,0
W2:
    djnz W2     ; 1 millisec (256*4)
    pop BC
    djnz W1
    LD E, 75    ; ID for ZX_SAVE_TAG_QSAVE_START
    call $031F  ; SAVE byte in E
    OUT     ($FF),A         ; ; signal to 1 / syncoff, send hsyncs
    ld b,0
W4:
    djnz W4     ; 1 millisec (256*4) to go to QSAVE mode
    RET

TRY_HANDSHAKE:  ; See if WESPI responds, return 1 if so, 0 for timeout
    CALL GO_QSAVE_MODE
    ld   hl, 16388 ; RAMTOP
    ld   B,2
    ld   C,90 ; packet ID ZX_QSAVE_TAG_HANDSHAKE
    call SEND_PACKET
    ld   b,181  ; timeout, 500ms (inner loop 3.15ms)
HS_LOOP1:
    PUSH BC
    LD   B,0
HS_LOOP2:                  ; 35 cycles=10.7us, inner Loop 2.75 millisec
    in a,($FE)  ; 11
    rla         ; 4
    jr c,HS_FOUND ; 12 / 7  (D7=0 is low level, wait for high)
    DJNZ HS_LOOP2 ;13
    ; re-check here to not have a blind spot for outer loop
    in a,($FE)  ; 11
    rla         ; 4
    jr c,HS_FOUND ; 12 / 7  (D7=0 is low level, wait for high)
    POP  BC
    DJNZ HS_LOOP1
    OUT     ($FF),A  ; signal to 1 / syncoff
    XOR  A
    RET

HS_FOUND
    POP  BC
    OUT     ($FF),A  ; signal to 1 / syncoff
    LD   A,1
    RET

QS_FINAL_SZ:
    db 0 ; size of return packet to request, use smallest possible

QS_FINAL_ACK:  ; Get info is operation was sussessful (Z) or failed (NZ), USES BC DE HL, A
    ld   hl, QS_FINAL_SZ ; Requested length
    ld   B,1
    LD   C,99 ; packet ID ZX_QSAVE_TAG_END_RQ
    call SEND_PACKET
    ; await reply, first tag
    CALL QLD_GETBYTE
    CP   42 ; tag
    RET  NZ
    ld B,8  ; 
QSFDLY:
    djnz lgapdly     ; 13*n-5 = 47 for 4
    CALL QLD_GETBYTE
    CP   1 ; result
    RET    ; Z on match



; gab between byte calls should be 77+12+7 - 17-10 = 69 clocks

QLD_GETBYTE:    ; uses BC D, result in A
    ld c,$fe   ; 7
    ld b,8  ; 7    for gap between start and bits

lwt_stdly:                  ; 25 cycles=7.7us
    in a,($FE)  ; 11
    rla         ; 4
    jr nc,lwt_stdly ; 12 / 7  (D7=0 is low level, wait for high)
    ; trigger is seen 4us too late in average, so wait 48-4 - 4 us now: 130 cy in-in
lgapdly:
    djnz lgapdly     ; 13*n-5 = 99 for 8
    ld b,8  ; 7
lbloop:                 ;  need 104 for 32us
    in d,(c)    ; 12
    rl d        ; 8
    rla         ; 4 (rl a is 8)
    ld d,4      ; 7
lbdly:
    dec d          ; 4          b*16-5 = 59
    jr nz,lbdly     ; 12 / 7
    djnz lbloop     ; 13 / 8
    RET     
    
    ;ORIGINAL:
    ;ld (hl),a   ; 7
	;CALL UPDATE  ; will use DE, inc HL    77 clks
    ;jr lwt_start     ; 12




;*
;* Evaluate string  start of the string is in HL and the length in BC
;*
NAME:	RST 20h
	CALL 0F55h ; evaluate
	LD A,(4001h)
	ADD A,A
	JP M,0D9Ah  ; error
	POP HL
	RET NC
	PUSH HL
	CALL 13F8h ; get indexvar fom calc stack STK-FETCH
    ; For strings, the start of the string is in DE and the length in BC
	LD H,D
	LD L,E
	RET


SEND_PACKET: ; HL points to data, C holds type, B lenght (0=256bytes), HL will point behind sent data afterwards
    PUSH AF
    PUSH BC ; BC needed twice, for header, and recovered at end
    PUSH BC

    LD B,225    ;   TODO 125 should be enough
SPWT:
    djnz SPWT     ; 500 microsec  (125*4)  ; let the video lines sync again, require 5 lines (320), some margin added

    ; we want all our pulses in sync with the HSYNC pulses to not interfere   
    XOR A       ; make sure A' not at sync or display position to
    EX AF,AF'   ; just cause short INT on MNI here:
                
    OUT ($FE),A  ; ENABLE NMI
    HALT
    OUT ($FD),A  ; Disable NMI   from here, start first bit in 54us =174cy
    IN      A,($FE)         ; signal to 0 /on - syncout
    LD   A,(HL)     ; 7 dummy 
    NOP             ; 4
    LD   B,7
    NOP             ; timing adjust to have bits symmetrical in (black-shouldered) line
    OUT     ($FF),A ;11        ; signal to 1 / syncoff
waitnline:
    DJNZ waitnline          ; delay for next line 13 per loop ..
    POP  BC            ;10


    ; Send packettype in C

    call SENDNIBBLE ;151, so we need 56 cycles between nibbles to get to 207 for one hsync line

    INC  HL         ; 6  ; DUMMY matching later dec
    LD   A,(HL)     ; 7 
    OUT     ($FF),A ; 11        ; signal to 1 /off
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    NOP

    call SENDNIBBLE ;151

    DEC  HL 
    LD   A,(HL)     ; 7 
    OUT     ($FF),A ;11        ; signal to 1 /off
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD C,B             ;4

    ; Send length in B

    call SENDNIBBLE ;151

    INC  HL         ; 6  ; DUMMY matching later dec
    LD   A,(HL)     ; 7 
    OUT     ($FF),A ; 11        ; signal to 1 /off
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    NOP             ; 4

    call SENDNIBBLE ;151

    DEC  HL 
    LD   A,(HL)     ; 7 
byteloop:
    OUT     ($FF),A ;11        ; signal to 1 /off
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   C,(HL)     ; 7 
    NOP
    call SENDNIBBLE ;151
    INC  HL         ; 6
    LD   A,(HL)     ; 7 
    OUT     ($FF),A ; 11        ; signal to 1 /off
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    NOP
                    ; 56
    CALL SENDNIBBLE ;151
    DJNZ byteloop   ; 13
    OUT     ($FF),A ; 11        ; signal to 1 /off

    POP  BC
    POP  AF
    ret


SENDNIBBLE: ;31 per bit, 134 incl RET 151 incl call, nibble in C lower half
    ; Four bits to send
    ;# bit 1 start
    RLC C       ; 8
    JR C, csendH1 ; 12/7
    ret c        ; 11/5 dummy for symmetric timing
    ;# bit 1 send 0
    IN      A,($FE)         ; signal to 0 /on

    ;# bit 2 start
    RLC C       ; 8
    JR C, csendH2 ; 12/7
    ret c        ; 11/5 dummy for symmetric timing
    ;# bit 2 send 0
    IN      A,($FE)         ; signal to 0 /on

    ;# bit 3 start
    RLC C       ; 8
    JR C, csendH3 ; 12/7
    ret c        ; 11/5 dummy for symmetric timing
    ;# bit 3 send 0
    IN      A,($FE)         ; signal to 0 /on

    ;# bit 4 start
    RLC C       ; 8
    JR C, csendH4 ; 12/7
    ret c        ; 11/5 dummy for symmetric timing
    ;# bit 4 send 0
    IN      A,($FE)         ; signal to 0 /on

    ret


csendH1:    ;# bit 1 send 1
    OUT     ($FF),A         ; signal to 1 /off

    ;# bit 2 start
    RLC C       ; 8
    JR C, csendH2 ; 12/7
    ret c        ; 11/5 dummy for symmetric timing
    ;# bit 2 send 0
    IN      A,($FE)         ; signal to 0 /on

    ;# bit 3 start
    RLC C       ; 8
    JR C, csendH3 ; 12/7
    ret c        ; 11/5 dummy for symmetric timing
    ;# bit 3 send 0
    IN      A,($FE)         ; signal to 0 /on

    ;# bit 4 start
    RLC C       ; 8
    JR C, csendH4 ; 12/7
    ret c        ; 11/5 dummy for symmetric timing
    ;# bit 4 send 0
    IN      A,($FE)         ; signal to 0 /on
    ret

csendH2:    ;# bit 2 send 1
    OUT     ($FF),A         ; signal to 1 /off

    ;# bit 3 start
    RLC C       ; 8
    JR C, csendH3 ; 12/7
    ret c        ; 11/5 dummy for symmetric timing
    ;# bit 3 send 0
    IN      A,($FE)         ; signal to 0 /on

    ;# bit 4 start
    RLC C       ; 8
    JR C, csendH4 ; 12/7
    ret c        ; 11/5 dummy for symmetric timing
    ;# bit 4 send 0
    IN      A,($FE)         ; signal to 0 /on
    ret

csendH3:    ;# bit 3 send 1
    OUT     ($FF),A         ; signal to 1 /off

    ;# bit 4 start
    RLC C       ; 8
    JR C, csendH4 ; 12/7
    ret c        ; 11/5 dummy for symmetric timing
    ;# bit 4 send 0
    IN      A,($FE)         ; signal to 0 /on
    ret

csendH4:    ;# bit 4 send 1
    OUT     ($FF),A         ; signal to 1 /off
    ret



   db $76   ;N/L 

line10:
   db 0,10  ;line number 
   dw dfile-$-2  ;line length 
   db $f5   ;PRINT 
   db $d4   ;USR 
   db $1d   ;1 
   db $22   ;6 
   db $21   ;5 
   db $1d   ;1 
   db $20   ;4 
   db $7e   ;FP mark 
   db $8f   ;5 bytes FP number 
   db $01   ; 
   db $04   ; 
   db $00   ; 
   db $00   ; 
   db $1a   ; ,
   db $0b   ; "
   db c_S   ; S
   db c_A   ; H
   db c_B   ; H
   db c_C   ; H
   db $0b   ; "
   db $76   ;N/L 
   db $76   ;N/L 


   
;- Display file -------------------------------------------- 
 
dfile: 
   db $76 
   db c_Z,c_X,
   db $76,$76,$76,$76,$76,$76,$76,$76 
   db $76,$76,$76,$76,$76,$76,$76,$76 
   db $76,$76,$76,$76,$76,$76,$76,$76 
 
;- BASIC-Variables ---------------------------------------- 
 
var: 
   db $80 
 
;- End of program area ---------------------------- 

last: 
 
   end 