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



#define COM_ADDR  0EFh
#define COM_DAT   06Fh

#define ADDR_LED  020h
#define ADDR_SELA 030h
#define ADDR_SELB 038h
#define A_RHR 00h
#define A_THR 00h
#define A_IER 01h
#define A_FCR 02h
#define A_ISR 02h
#define A_LCR 03h
#define A_MCR 04h
#define A_LSR 05h
#define A_MSR 06h
#define A_SCPAD 07h

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


   ;ds 33    ;Print buffer --- now used for loader code, all loaded programs need to have the same !
; relocatible loader code
PLOADER:
lwt_start:
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
    rla         ; 4 (rr a is 8)
    ld d,4      ; 7
lbdly:
    dec d          ; 4          b*16-5 = 59
    jr nz,lbdly     ; 12 / 7
    djnz lbloop     ; 13 / 8
    ld (hl),a   ; 7

	CALL UPDATE  ; will use DE, inc HL    77 clks
    jr lwt_start     ; 12
PLOADEND:
   ds PLOADER+33-PLOADEND    ; Remaining space of 33 byte print buffer, after 29 byte loader

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

#DEFINE c_A 38
#DEFINE c_B (c_A+1)
#DEFINE c_C (c_A+2)
#DEFINE c_D (c_A+3)
#DEFINE c_E (c_A+4)
#DEFINE c_F (c_A+5)
#DEFINE c_G (c_A+6)
#DEFINE c_H (c_A+7)
#DEFINE c_I (c_A+8)
#DEFINE c_J (c_A+9)
#DEFINE c_K (c_J+1)
#DEFINE c_L (c_J+2)
#DEFINE c_M (c_J+3)
#DEFINE c_N (c_J+4)
#DEFINE c_O (c_J+5)
#DEFINE c_P (c_J+6)
#DEFINE c_Q (c_J+7)
#DEFINE c_R (c_J+8)
#DEFINE c_S (c_J+9)
#DEFINE c_T (c_S+1)
#DEFINE c_U (c_S+2)
#DEFINE c_V (c_S+3)
#DEFINE c_W (c_S+4)
#DEFINE c_X (c_S+5)
#DEFINE c_Y (c_S+6)
#DEFINE c_Z (c_S+7)



;
;   === Main entry point ====
;

BASIC_START:

	CALL FAST	; go to fast mode
    ; send msg back
    CALL $0F46  ; what is this?
    
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

    OUT     ($FF),A         ; signal 0/off just in case

    LD B,20  ; 20ms to go to QSAVE mode
W3: push BC
    ld b,0
W4:
    djnz W4     ; 1 millisec (256*4)
    pop BC
    djnz W3

bloop:
    ld b,0
W5:
    djnz W5     ; 
    ld   hl, testdata
    ld   b,5
    ld   c,$42 ; packet ID
    call SyncSendBytes
    jr   bloop


testdata:
    db $55, $AA, $FF, 0, $55

SyncSendBytes:
    ; we want all our pulses in sync with the HSYNC pulses to not interfere   
    XOR A       ; make sure A' not at sync or display position to
    EX AF,AF'   ; just cause short INT on MNI here:
                
    OUT ($FE),A  ; ENABLE NMI
    HALT
    OUT ($FD),A  ; Disable NMI   from here, start first bit in 54us =174cy
    IN      A,($FE)         ; signal to 0 /on - syncout
    PUSH BC
    LD   B,7
    NOP
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
    ret

WAITGAP:


SENDNIBBLE: ;31 per bit, 134 incl RET 151 incl call
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
   db $c5   ;VAL
   db $0b   ;"
   db $1d   ;1 
   db $22   ;6 
   db $21   ;5 
   db $1d   ;1 
   db $20   ;4 
   db $0b   ;"
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
