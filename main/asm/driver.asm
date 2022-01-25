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
DRIVER_START:
BASIC_START:
AA45:
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
AA44:
	CALL GENER_START
	LD (UFM_ERRNO),A
	RET

BASIC_CONT:
AA01:
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
	; RET  ; TODO check if RET or better RST8 with -1
    XOR A		; sonst zurueck nach BASIC
	RST 08h
	db 0FFh
	

GENER_START: ; Interpreting the string independent of its origin, on ret A=0 for okay, BC retval to basic
	LD A,(HL)   ; HL=start, BC=length
    INC  HL
    DEC  BC
AA00:
    LD   DE,GENER_END
    PUSH DE     ; ret adddess
;	CP c_I		; Info
;	JP Z,INFO1
	CP c_S		; Save
AA41:
	JP Z,SAVE1
	CP c_L		; Load
AA42:
	JP Z,LOAD1
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
	CP c_I		;  I
AA02:
	JP Z,INST_RELOC
	CP c_T		; Test, return 0...1023 dependng on number of correct bytes
AA43:
	JP Z,TESTPATTERN
	CP c_D		; Directory
AA52:
	JP Z,LOAD1
	CP c_H		; Help
AA03:
	JP Z,HLP
	CP 0Fh		; ist es ein ?
AA04:
	JP Z,HLP
    ret 

GENER_END:
    PUSH AF ; holds our error status
    PUSH BC ; ret value...
	CALL SHOW
    POP  BC
    POP  AF
	RET
	
INST_RELOC:
    ; address with new memory location follows. Cpoies the driver and corrects the absolute adresses accordingly
AA09:
    CALL PARS_DEC_NUM   ; HL points to char in arg line, C holds remaining ARG size, return dec in DE, uses AF, Z set when okay
    LD   A,1
    RET  NZ
    PUSH DE
	CALL FAST	; go to fast mode so we can use index regs
    POP  DE
    PUSH IX     ; save IX till end
    PUSH DE
    POP  IX     ; new start addr in IX, will need it quite often
    ; copy RAW driver
AA06:
    LD HL,DRIVER_START
    LD BC,DRIVER_END-DRIVER_START
    LDIR
    ; correct abs addr occurrences, all in this table:
AA07:
    LD HL, RELOC_TABLE
RLCLOOP:
    LD   E,(HL)
    INC  HL
    LD   D,(HL)
    INC  HL
    LD   A,D
    OR  E
    JR   Z, ENDRLCCP    ; null end marker
    PUSH HL ; next RELOC_TABLE pos
    ; DE is position of the addr tag relative to start
    PUSH IX
    POP  HL
    ADD  HL,DE
    ; HL is the absolute label pos to modify
    PUSH HL ; need it later to write
    LD   E,(HL) 
    INC  HL
    LD   D,(HL)
AA05:
    LD   HL, DRIVER_START
    EX   DE,HL
	AND A		; clear carry
	SBC HL,DE	; actual address minus old offset

    ; HL is relative addr
    PUSH IX
    POP  DE
    ADD  HL,DE  ; add new offset
    EX   DE,HL
    ; DE is new abs addr
    POP  HL
    LD   (HL),E
    INC  HL
    LD   (HL),D
    POP HL ; restore RELOC_TABLE pos
    JR RLCLOOP
ENDRLCCP:
    POP  IX
    XOR  A
    LD   BC,DRIVER_END-DRIVER_START
    RET
 

PRINTHEX:
	PUSH AF
	PUSH HL
	PUSH BC
	LD C,A		; SAVE
	SRL A
	SRL A
	SRL A
	SRL A
	ADD A,1CH	; Offset to '0'
	RST 10H
	LD A,C
	AND	0FH		; MASK
	ADD A,1CH	; Offset to '0'
	RST 10H
	POP BC
	POP HL
	POP AF
	RET


RELOC_TABLE:
 ;   dw 0
    dw AA00+1-DRIVER_START
    dw AA01+1-DRIVER_START
    dw AA02+1-DRIVER_START
    dw AA03+1-DRIVER_START
    dw AA04+1-DRIVER_START
    dw AA05+1-DRIVER_START
    dw AA06+1-DRIVER_START
    dw AA07+1-DRIVER_START
    dw AA08+1-DRIVER_START
    dw AA09+1-DRIVER_START

    dw AA10+1-DRIVER_START
    dw AA11+1-DRIVER_START
    dw AA12+1-DRIVER_START
    dw AA13+1-DRIVER_START
    dw AA14+1-DRIVER_START
    dw AA15+1-DRIVER_START
    dw AA16+1-DRIVER_START
    dw AA17+1-DRIVER_START
    dw AA18+1-DRIVER_START
    dw AA19+1-DRIVER_START

    dw AA20+1-DRIVER_START
    dw AA21+1-DRIVER_START
    dw AA22+1-DRIVER_START
    dw AA23+1-DRIVER_START
    dw AA24+1-DRIVER_START
    dw AA25+1-DRIVER_START
    dw AA26+1-DRIVER_START
    dw AA27+1-DRIVER_START
    dw AA28+1-DRIVER_START
    dw AA29+1-DRIVER_START

    dw AA30+1-DRIVER_START
    dw AA31+1-DRIVER_START
    dw AA32+1-DRIVER_START
    dw AA33+1-DRIVER_START
    dw AA34+1-DRIVER_START
    dw AA35+1-DRIVER_START
    dw AA36+1-DRIVER_START
    dw AA37+1-DRIVER_START
    dw AA38+1-DRIVER_START
    dw AA39+1-DRIVER_START

    dw AA40+1-DRIVER_START
    dw AA41+1-DRIVER_START
    dw AA42+1-DRIVER_START
    dw AA43+1-DRIVER_START
    dw AA44+1-DRIVER_START
    dw AA45+1-DRIVER_START
    dw AA46+1-DRIVER_START
    dw AA47+1-DRIVER_START
    dw AA48+1-DRIVER_START
    dw AA49+1-DRIVER_START

    dw AA50+1-DRIVER_START
    dw AA51+1-DRIVER_START
    dw AA52+1-DRIVER_START


    dw 0    ; final



CHECKCOMMA:  ; Z is set when comma seperator found in string, NZ if not, uses A
    ; BC =0?
    XOR  A
    CP   C
    JR   NZ,CHKK_CONT
    INC  A ; clear the Z flag
    RET     ; no match till end
CHKK_CONT:
    LD   A, (HL)
    CP   26     ; comma
    RET  z
    CP   25     ; also check for semicolon
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
    

LOAD1: ; both load and dir
	; HL points to arg string, BC number of chars
    DEC HL  ; back to original CMD - as it coulf be LOAD or DIR
    INC BC  ; also transmit CMD char here as we need to distinguish L and D
    PUSH HL  ; orig pos of args (w/o prefix T)
    PUSH BC  ; orig lenght of args (w/o prefix T)
    ; Check if we have contact
AA10:    
    CALL TRY_HANDSHAKE  ; See if WESPI responds, return 1 if so, 0 for timeout
    AND A
    ; Send LOAD request
    POP  BC
    POP  HL ; recover name pointer/length
    JR   Z, LD_ERR ; NO CONTACT after TRY_HANDSHAKE
    PUSH HL  ; and again store for binload
    PUSH BC  ; 

    ld   B,C ; length, assume <256
    ld   C,93 ; packet ID ZX_QSAVE_TAG_LOADPFILE
AA11:
    CALL SEND_PACKET

    ; now retrieve key, must be 123
AA12:
    CALL QLD_GETBYTE    ; uses BC D, result in A
    CP  123
    JR NZ,LD_ERR2
    LD   B,4
    LD  A,(HL)  ; dummy for timing
 
LOADELY1:         ;    //47 delay between the header bytes
    DJNZ LOADELY1

    ; now retrieve length, 0 for error
AA13:
    CALL QLD_GETBYTE    ; uses BC D, result in A
    LD   L,A
    LD   B,5

LOADELY2:         ;    //60 delay between the length bytes
    DJNZ LOADELY2

    NOP
AA14:
    CALL QLD_GETBYTE    ; uses BC D, result in A
    LD   H,A
    OR   L
    JR Z,LD_ERR2
    EX   DE,HL
    ; Here we have DE= len
    ; get addr
    POP  BC
    POP  HL ; recover name pointer/length
    PUSH DE             ; length, will go to BC below
    PUSH DE             ; length, again, for end result
    LD A,(HL) ; check command L or D
    CP c_D
    JR Z,SHOWDIR
    INC HL  ; skip cmd
    DEC BC
    ;  test if saving binary or regular basic
AA15:
    CALL CHECKCOMMA
    JR   Z, BINLOAD

    ; LOAD BASIC program if not BINLOAD, set addr
	LD HL,4009h		; 
    LD   E,1        ; mark as basic load

LOADLOOP:
    ; timing  - 74 between calls seems to be more reliable than 70!
AA16:
    CALL QLD_GETBYTE    ; uses BC D, result in A
    LD   (HL),A
    LD   (HL),A ; dummy for timing
    INC  HL
    POP  BC     ; get remaining length
    DEC  BC
    LD   A,C
    OR   B
    JR Z, LD_END
    PUSH BC
    JR  LOADLOOP
LD_END:  
    POP  BC ; orig length
    XOR  A
    CP   E  ; 0 for binary
    RET   Z ; normal return for binload with length in BC
    ; end for BASIC loader
    POP  DE ; dummy, ret addr
	RST 08h ;
	db 0FFh


LD_ERR2:
    POP  DE ; dummy
    POP  DE ; dummy
LD_ERR:
    LD   A,1
    RET ; BC WILL be at maximum now


BINLOAD: ; HL points to the comma in arg string, now parse addr, length
    INC  HL
    DEC  C
AA17:
    CALL PARS_DEC_NUM ; HL points to char in arg line, C holds remaining ARG size, return dec in DE, uses AF, Z set when okay
    JR NZ, LD_ERR2   ; parse error
    ; addr in DE
    EX   DE,HL
    LD   E,0    ; mark as binary
    JR LOADLOOP

SHOWDIR:
    ; this is like a binload but into the screen area
    ; first prepare space for it
    LD HL,(16396) ; D_FILE pos
    INC HL
    LD (16398),HL  ; restore print position to start of screen as we clean all
    ;XOR A
    ;LD (16441),A      ; PRINT col 24
    ;LD (16442),A      ; PRINT line 33?
    EX DE,HL       ; start addr now in DE
    LD HL,(16400)  ; VARS pos as end of screen
    DEC HL;        ; leave one byte in there, otherwise pointers will collapse
    CALL $A5D      ; reclaim-diff: release mem from DE to HL: CF_CC to VARS-1
    POP BC         ; get length of new screen
    PUSH BC        ; put back, needed later 
    DEC BC         ; we left one byte already
    LD (16398),HL  ; start from print position
    CALL $99E      ; make-room: reserve BC bytes at HL
    LD   E,0       ; from now on it is just a binload, mark
    JR LOADLOOP    ; 


; parse a decimal number
PARS_DEC_NUM:   ; HL points to char in arg line, C holds remaining ARG size, return dec in DE, uses AF, Z set when okay
	LD DE, 0
    XOR  A
    CP   C
    JR Z,PARSFAIL

PARS_LLOOP:     ; look for first number
	LD A,(HL)
	AND A
	JR Z, PARS_SKIPWS ; skip whitespace
PARS_LLP2:
	SUB 01ch	;"0"
	JR C,PARSFAIL
	CP 10
	JR NC,PARSFAIL
	; have a digit in A,
	PUSH HL
	; DE times ten
	LD H,D      ; now in both HL and DE
	LD L,E
	ADD HL,HL
	ADD HL,HL
	ADD HL,HL ; times 8 so far
	ADD HL,DE
	ADD HL,DE
	; Add new digit
	LD D,0
	LD E,A
	ADD HL,DE
	EX DE,HL    ; new value of DE
	POP HL  ; pointer to argline back in HL
	INC HL
	DEC C
    JR Z,PARSDONE
	LD A,(HL)   ; load next char to see if end
    CP   26 ;  ','
    JR Z,PARSDONE
    CP   14 ;  ';'
    JR Z,PARSDONE
    CP   11 ;  '"'
    JR Z,PARSDONE
	JR PARS_LLP2 
PARS_SKIPWS:
	INC HL
	DEC C
    JR Z,PARSFAIL
	JR PARS_LLOOP 

PARSDONE:
    XOR  A
	RET
PARSFAIL:
    XOR  A
    INC  A
	RET


TESTPATTERN:
	; HL points to arg string, BC number of chars
    PUSH HL  ; orig pos of args (w/o prefix T)
    PUSH BC  ; orig lenght of args (w/o prefix T)
    ; Check if we have contact
AA18:    
    CALL TRY_HANDSHAKE  ; See if WESPI responds, return 1 if so, 0 for timeout
    AND A
    ; Send LOAD request
    POP  BC
    POP  HL ; recover name pointer/length
    JR   Z, ERREXIT ; NO CONTACT after TRY_HANDSHAKE

    ld   B,C ; length, assume <256
    ld   C,93 ; packet ID ZX_QSAVE_TAG_LOADPFILE
AA19:
    CALL SEND_PACKET

    ; now retrieve 1024 bytes and see how many are correct
    ; gap between byte calls should be 77+12+7 - 17-10 = 69 clocks
    LD   BC,0
TESTBLOOP:
    PUSH BC
    NOP         ; timing adjust 66/70
    NOP         ; timing adjust 70/74 - 74 seems to be more reliable than 70!
AA20:
    CALL QLD_GETBYTE    ; uses BC D, result in A
    POP  BC
    CP   C  ; incomming data in A is 0,1,2,3,4...255,0,1... as byte
    LD   A,0 ; not affect flags, test never reports errors to better automatize
    RET  NZ ; report BC at point of first failure
    INC  BC
    LD   A,16 ; 4kbyte testsize
    CP   B
    JR   NZ, TESTBLOOP
    XOR  A
    RET ; BC WILL be at maximum now



ERREXIT:
    LD   A,1
    LD   BC,0
    RET

SAVE1:
	; HL points to arg string, BC number of chars
    PUSH HL  ; orig pos of args (w/o prefix S)
    PUSH BC  ; orig lenght of args (w/o prefix S)


;    LD   A, c_S
;    CALL PRINTA

    ; Check if we have contact
AA21:   
    CALL TRY_HANDSHAKE  ; See if WESPI responds, return 1 if so, 0 for timeout
    ; Send SAVE request
    POP  BC
    POP  HL ; recover name pointer/length
    ; evauate connect result
    AND A
    JR   Z, ERREXIT ; NO CONTACT after TRY_HANDSHAKE

    ; again store original argument string, will need it now and when sending name
    PUSH HL
    PUSH BC

    ;  test if saving binary or regular basic
AA22:
    CALL CHECKCOMMA
    JR   Z, BINSAVE

    ; SAVE BASIC program if not BINSAVE, pre-calc addr,size into HL and BC
	LD DE,4009h		; Get length
	LD HL,(ELINE)	
	AND A		; clear carry
	SBC HL,DE	; HL=length
	LD B,H
	LD C,L
	EX DE,HL	; Now HL=Start, BC=length

SAVE_CONT: ;continue common path of BIN and BASIC save
    EXX     ; store payload addr and length for now
	EX DE,HL	; HL' must be restored, save in DE'
    POP  BC
    POP  HL ; recover name pointer/length
AA23:
    CALL SKIPEMPTY 
    XOR  A
    CP   C
    JR   Z, ERREXIT3 ; NO NAME
    ld   B,C ; length, assume <256
    ld   C,91 ; packet ID ZX_QSAVE_TAG_SAVEPFILE
AA24:
    CALL SEND_PACKET
	EX DE,HL	; HL' must be restored, was saved in DE'
    EXX ; Recover, now HL=Start, BC=length
	
SVSENDFUL:
    XOR  A
    CP   B
    JR   Z, SVSENDLAST
    ; send full packets
    PUSH BC
    LD   B,0    ; 256 bytes
    LD   C,95 ; packet ID ZX_QSAVE_TAG_DATA
AA25:
    CALL SEND_PACKET
    POP  BC
    DEC  B
    JR   SVSENDFUL

SVSENDLAST:
    XOR  A
    CP   C
    JR   Z, SVSENDEND
    LD   B,C ; length
    LD   C,95 ; packet ID ZX_QSAVE_TAG_DATA
AA26:
    CALL SEND_PACKET

SVSENDEND:
AA27:
    CALL QS_FINAL_ACK   ; Z set for success
    JR   NZ, ERREXIT
    XOR  A
    LD   BC, 1
	RET


BINSAVE: ; HL points to the comma in arg string, now parse addr, length
    INC  HL
    DEC  C
AA28:
    CALL PARS_DEC_NUM ; HL points to char in arg line, C holds remaining ARG size, return dec in DE, uses AF, Z set when okay
    JR NZ, BSERREXIT   ; parse error
    PUSH DE ; store addr, from now on have to use BSERREXIT2 to stack-unwind here
    INC  HL
    DEC  C
AA29:
    CALL PARS_DEC_NUM ; HL points to char in arg line, C holds remaining ARG size, return dec in DE, uses AF, Z set when okay
    JR NZ, BSERREXIT2   ; parse error
    ; length in DE
    ; Now put HL=Start, BC=length
    LD   B,D
    LD   C,E
    POP  HL
    JR SAVE_CONT

ERREXIT3:
	EX DE,HL	; HL' must be restored, saved in DE'
    EXX ; Recover
    LD   A,1
	RET

BSERREXIT2:
    POP  DE
BSERREXIT:
    POP  BC
    POP  HL ; recover name pointer/length
    LD   A,1
	RET


HLPTXT1:
	db c_Z,c_X,0,c_W,c_E,c_S,c_P,c_I,0,c_D,c_R,c_I,c_V,c_E,c_R,c_NEWLINE
    db c_NEWLINE,c_NEWLINE
	db c_P,c_R,c_I,c_N,c_T, 0 , c_U, c_S, c_R, 0 
	db $ff

;	db "INFO  ",22h,"I",22h,0dh
;	db "DIR   ",22h,"D",22h,0dh
;	db "ERASE ",22h,"ENAME.P",22h,0dh
;	db "MKDIR ",22h,"DMVERZ",22h,0dh
;	db "RMDIR ",22h,"DEVERZ",22h,0dh
;	db "CHDIR ",22h,"DCVERZ",22h,0dh
HLPTXT2:
	db 26, 11, 19, c_C,c_M,c_D, 18,  11,c_NEWLINE   ; "LOAD  ",22h,"LNAME.P",22h,0dh
    db c_NEWLINE,c_NEWLINE
	db c_L,c_O,c_A,c_D, 0 , 0 , 0, 11, c_L, c_N, c_A, c_M, c_E, 11,c_NEWLINE   ; "LOAD  ",22h,"LNAME.P",22h,0dh
	db c_S,c_A,c_V,c_E, 0 , 0 , 0, 11, c_S, c_N, c_A, c_M, c_E, 11,c_NEWLINE   ; "SAVE  ",22h,"SNAME.P",22h,0dh
	db c_B,c_L,c_O,c_A,c_D, 0 , 0, 11, c_L, c_N, c_A, c_M, c_E,26, c_A, c_D, c_D, c_R, 11,c_NEWLINE   ; "BLOAD ",22h,"LNAME.B,SSSS",22h,0dh
	db c_B,c_S,c_A,c_V,c_E, 0 , 0, 11, c_S, c_N, c_A, c_M, c_E,26, c_A, c_D, c_D, c_R, 26, c_L, c_E, c_N,  11,c_NEWLINE   ; "BSAVE ",22h,"SNAME.B,SSSS,EEEE",22h,0dh
	db c_D,c_I,c_R,  0, 0 , 0 , 0, 11, c_D, 11, 0, c_O, c_R, 0, 11, c_D,0, c_P, c_A, c_G, c_E, 11,     ,c_NEWLINE   ;  c_H,   "DIR  ",22h,"H",22h,0dh
	db c_H,c_E,c_L,c_P, 0 , 0 , 0, 11, c_H, 11,c_NEWLINE   ;  c_H,   "HELP  ",22h,"H",22h,0dh
;	db "BSAVE ",22h,"SNAME.B,SSSS,EEEE",22h,0dh
;	db "RENAME",22h,"ROLDNAME NEWNAME",22h,0dh
    db c_NEWLINE
	db c_I,c_N,c_S,c_T,c_A,c_L,c_L, 0 , c_D,c_R,c_V, 0 ,c_T,c_O, 0, c_R,c_A,c_M,  0, 11, c_I, 0, c_A, c_D, c_D, c_R,  11,c_NEWLINE   ; "SAVE  ",22h,"SNAME.P",22h,0dh
;	db c_T,c_E,c_S,c_T, 0 , 0 , 0, 11, c_T, c_L, c_T, c_T, c_T, c_0+2, 11,c_NEWLINE   ; "SAVE  ",22h,"SNAME.P",22h,0dh
;	db "FOR SIGGI'S UFM :V/R/K",0dh
    db c_NEWLINE
    db c_NEWLINE
    db c_R, c_E, c_V, 0, c_A, c_0+2,0,0,0, c_S, c_I, c_Z, c_E, 0
	db $ff


; === Subroutine print help text ====
AA08:
HLP:
	LD HL,HLPTXT1
AA46:
    CALL PRINTTEXT
AA47:
	LD HL,DRIVER_START
AA48:
    CALL PRINTBASE10
AA49:
	LD HL,HLPTXT2
AA50:
    CALL PRINTTEXT
    LD HL,DRIVER_END-DRIVER_START
AA51:
    CALL PRINTBASE10
    LD   BC,42
    XOR  A
    RET

; Text in HL, FF marks end
PRINTTEXT:
	LD A,(HL)
	CP $FF
	RET Z
    RST 10H
	INC HL
	JR PRINTTEXT


; *
; * PRINT HL DECIMAL
; *
PRINTBASE10:
	PUSH HL
	PUSH BC
	PUSH DE
    XOR A
	PUSH AF
_PRTLP:
_DIV10: ; * HL=HL/10 A=REMAINDE
	LD B,10h
	XOR A
_DIVLP:
	SLA L
	RL H
	RLA
	CP 0Ah
	JR C,_SK
	SET 0,L
	SUB 0Ah
_SK:
	DJNZ _DIVLP

	ADD A,1CH
	PUSH AF
	LD A,H
	OR L
	JR NZ,_PRTLP
_PRTL1:
	POP AF
	OR A
	JR NZ,_PRT2
	POP DE
	POP BC
	POP HL
	RET
_PRT2
	RST 10H
	JR _PRTL1



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
    CALL $031F  ; SAVE byte in E
    OUT     ($FF),A         ; ; signal to 1 / syncoff, send hsyncs
    ld b,0
W4:
    djnz W4     ; 1 millisec (256*4) to go to QSAVE mode
    RET

TRY_HANDSHAKE:  ; See if WESPI responds, return 1 if so, 0 for timeout
AA30:
    CALL GO_QSAVE_MODE
    ld   hl, 16388 ; RAMTOP
    ld   B,2
    ld   C,90 ; packet ID ZX_QSAVE_TAG_HANDSHAKE
AA31:
    CALL SEND_PACKET
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
    OUT     ($FF),A  ; signal to 1 / syncoff in between to not let levels drift (not good for follow-up cmds?)
    POP  BC
    DJNZ HS_LOOP1
    ; no signal found
    XOR  A
    RET

HS_FOUND
    OUT     ($FF),A  ; signal to 1 / syncoff
    ; let the output recover with active signal after being low from input. takes about 1ms as seen in oscilloscope
    LD   B,0
HS_FINALDELAY: ; 3ms here before next cmd
    LD   A,(HL)
    LD   A,(HL)
    LD   A,(HL)    
    LD   A,(HL)    
    DJNZ HS_FINALDELAY
    POP  BC
    LD   A,1
    AND  A
    RET

QS_FINAL_SZ:
    db 0 ; size of return packet to request, use smallest possible

QS_FINAL_ACK:  ; Get info is operation was sussessful (Z) or failed (NZ), USES BC DE HL, A
    ld   hl, QS_FINAL_SZ ; Requested length
    ld   B,1
    LD   C,99 ; packet ID ZX_QSAVE_TAG_END_RQ
AA32:
    CALL SEND_PACKET

    ; await reply, first byte is tag, then result
AA33:
    CALL QLD_GETBYTE
    CP   42 ; tag
    RET  NZ
    ld B,8  ; 
QSFDLY:
    djnz lgapdly     ; 13*n-5 = 47 for 4
AA34:
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
AA35:
    CALL SENDNIBBLE ;151, so we need 56 cycles between nibbles to get to 207 for one hsync line

    INC  HL         ; 6  ; DUMMY matching later dec
    LD   A,(HL)     ; 7 
    OUT     ($FF),A ; 11        ; signal to 1 /off
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    NOP
AA36:
    CALL SENDNIBBLE ;151

    DEC  HL 
    LD   A,(HL)     ; 7 
    OUT     ($FF),A ;11        ; signal to 1 /off
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD C,B             ;4

    ; Send length in B
AA37:
    CALL SENDNIBBLE ;151

    INC  HL         ; 6  ; DUMMY matching later dec
    LD   A,(HL)     ; 7 
    OUT     ($FF),A ; 11        ; signal to 1 /off
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    LD   A,(HL)     ; 7 
    NOP             ; 4
AA38:
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
AA39:
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
AA40:    
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

DRIVER_END:

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
   db c_H   ; Help
 ;  db c_D   ; Dir

 ;  db c_I   ; I 10000 install
   ;db c_I   ; TTTT2 = QLOAD test
   ;db c_T   ; SNNN = dummy save for testing
   ;db c_S   ; STST,1024,100 binsave
   ;db c_T   ; LTST,1024   binload
 ;  db 0   ; 
 ;  db c_0+1   ; 
 ;  db c_0+0   ; 
 ;  db c_0+0   ; 
 ;  db c_0+0   ; 
 ;  db c_0+0   ; 
;   db 26
;   db c_0+1   ; 
;   db c_0+0   ; 
;   db c_0+0   ; 
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
