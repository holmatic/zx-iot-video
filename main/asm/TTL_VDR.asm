; URSPRUNGSVERSION: 
; 	USB-Steruerungsprogramm f�r ZX81 mit GAL
; 	geschrieben 2010 Thomas Lienhard
; 	Forum: http://zx81.tlienhard.com
; 	Version 1.0.1



; 2010/2011: Modifiziert f�r Spar-Interface mit TTLs - Siehe Forum
; 			 256 Bytes RAM an 3E00h nicht mehr n�tig


; FIRMWARE 3.62 ok
; FIRMWARE 3.68 ??

; 	funktioniert mit Vinculum VDrive2 Modul von FTDI
; 	http://www.vinculum.com/documents/datasheets/DS_VDRIVE2.pdf

; 	VDRIVE2 Br�cke von GND nach UART/SPI gesetzt

; USB-Info:	PRINT USR 8192,"I"
; Diectory:	PRINT USR 8192,"D"
; Laden:	PRINT USR 8192,"LNAME.P"
; oder		PRINT USR 8192,"LNAME.C,XXXX"
; Speichern:	PRINT USR 8192,"SNAME.P"
; oder		PRINT USR 8192,"SNAME.C,XXXX,YYYY"
; L�schen:	PRINT USR 8192,"ENAME.P"
; Dir change:	PRINT USR 8192,"DCVERZ"
; Dir make:	PRINT USR 8192,"DMVERZ"
; Dir erase:	PRINT USR 8192,"DEVERZ"
; Hilfe:	PRINT USR 8192,"H"
; NAME.P : Dateiname 8.3, XXXX : Startadresse, YYYY : Endadresse

 
#define db .byte ;  cross-assembler definitions 
#define dw .word 
#define ds .block 
#define org .org 
#define end .end 

;;#define VERBOSE 1
 
; Diese Software kann als BASIC Lader gebaut werden oder als Bin�rfile f�r ROM/Direktaufruf 
   org     $1   ; 1= Lader,0=bin�r
ISLOADER 

 
#if ISLOADER
   org     $4009 ; BASIC PROGRAMM
;= System variables ============================================ 
 
   db 0     	;VERSN 
   dw 0     	;E_PPC 
   dw dfile      ;D_FILE 
   dw dfile+1    ;DF_CC 
   dw var   	;VARS 
   dw 0     	;DEST 
   dw var+1      ;E_LINE 
   dw last-1     ;CH_ADD 
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
   dw line10     ;NXTLIN 
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
   ds 33    ;Print buffer 
membot: 
   ds 30    ;Calculator�s memory area 
   ds 2     ;not used 
 
;= First BASIC line, asm code ================================== 
 
line0: 
   db 0,0   ;line number 
   dw line10-$-2 ;line length 
   db $ea   ; REM 

#ELSE

; Startadresse f�r das Bin�rprogram, vorgesehen f�r RAM/ROM ab 8192
   org     $2000 
      
#ENDIF  ;ISLOADER
 


#define ELINE	04014h  ; Systemvariable, die das Ende des abzuspeichernen BASIC-Programs anzeigt
#define SHOW	00207h  ; ROM-Routinen
#define FAST	002E7h
#define RCLS	00A2Ah
#define GETKEY	002BBh

#define UFM_ERRNO 16434 ; Definiert in UFM, Fehlercode 1 Byte (eigentlich SEED)
#define UFM_CMDBUF 16444 ; Definiert in UFM, 32 Byte, alternative Kommando�bergabe (eigentlich PRBUF)
#define UFM_COORDS 16438 ; L�ngen�bergabe 2 Byte (eigentlich COORDS)

;
;   === Haupteinsprungstelle ====
;

BASIC_START:
	CALL NAME	; Kommandozeile nach HL
	JR BASIC_CONT
	NOP
	NOP
	NOP

CALL_START:	; (IN UFM als START_ADDR+6 festgelegt)
	LD HL,UFM_CMDBUF
	CALL GENER_START
	LD (UFM_ERRNO),A
	RET

BASIC_CONT:
	CALL GENER_START ; A ist danach 0 bei ok, gesetzt bei Fehler
	AND A
	JR Z,BAS_OK
	LD E,45h		; Ausgabe ERR
	CALL TEX1
	LD E,52h
	CALL TEX1
	LD E,52h
	CALL TEX1
	LD E,0dh
	CALL TEX1
BAS_OK:
	XOR A		; sonst zurueck nach BASIC
	RST 08h
	db 0FFh
	
	
	
	

GENER_START: ; Allgemeiner Start unabhaengig von BASIC/Call Ursprung, Ret mit A=Errorcode
	LD A,(HL)
	CP 2Eh		; ist es ein I
	JP Z,INFO1
	CP 38h		; ist es ein S
	JP Z,SAVE1
	CP 31h		; ist es ein L
	JP Z,LOAD1
	CP 29h		; ist es ein D
	JP Z,DIR1
	CP 3Bh		; ist es ein V (UFM)
	JP Z,DIRV1
	CP 30h		; ist es ein K (UFM)
	JP Z,DIRK1
	CP 37h		; ist es ein R
	JP Z,RENAM1
	CP 2Ah		; ist es ein E
	JP Z,ERAS1
	CP 2Dh		; ist es ein H
	JP Z,HLP
	CP 0Fh		; ist es ein ?
	JP Z,HLP

EXIT_OK:
	CALL SHOW
	XOR A		; sonst zurueck nach BASIC
	RET
	
ERROR1: POP HL  ; Wenn Fehler in Unterprogramm, entferne Returnadresse
ERROR0:
	CALL RDFLUSH	
	CALL RDFLUSH	
	XOR A
	INC A		; Melde Fehler
	RET

	
;*
;* WAIT
;*
WAIT:	PUSH BC
	PUSH AF
	LD B,0FFh
	XOR A
WAIT1:	DEC B
	CP B
	JP NZ,WAIT1
	POP AF
	POP BC
	RET


;*
;* INFO
;*
INFO1:	CALL INIT	; PIO-Port und VDRIVE2 initialisieren
	LD A,12h	; get free space
	CALL SENDB	; sonst zeigt IDD den 
	LD A,0dh	; freien Platz nicht an
	CALL SENDB
	ld a,3Eh		; Das Prompt am Ende
	CALL RDUNTILL
	CALL RDFLUSH	; CR entfernen
	LD A,94h	; IDDE f�r >4GB
	CALL SENDB
	LD A,0dh
	CALL SENDB
INFO05:
	CALL RDWAIT
	CP 0DH		; Das Prompt am Ende?
	JR Z,INFO05
INFOS:
	LD E,A
	CALL TEX1
INFO2:
	CALL RDWAIT
	CP 3eh		; Das Prompt am Ende?
	JR NZ,INFOS
	CALL RDFLUSH	; CR entfernen
	CALL WAIT
	CALL WAIT
	; Version info
	LD A,13h
	CALL SENDB
	LD A,0dh
	CALL SENDB
INFOV:
	CALL RDWAIT
	CP 0dh		; Sobald kein CR mehr
	JR Z,INFOV	; kommt, weiter...
INFOW:	LD E,A
	CALL TEX1	; und ausgeben.
	CALL RDWAIT
	CP 3Eh		; Das Prompt am Ende?
	JR NZ,INFOW
	CALL RDFLUSH	; CR entfernen
	JP EXIT_OK

	

	
; Einsprung fuer "V" (DM in UFM-Konvention)
DIRV1:	CALL INIT	; PIO-Port und VDRIVE2 initialisieren
	INC HL
	LD A,(HL)
	JR DIMAK

; Einsprung fuer "K" (DE in UFM-Konvention)
DIRK1:	CALL INIT	; PIO-Port und VDRIVE2 initialisieren
	INC HL
	LD A,(HL)
	JR DIERA
	
;*
;* Directory
;*
DIR1:	CALL INIT	; PIO-Port und VDRIVE2 initialisieren
	INC HL
	LD A,(HL)
	CP 0Bh	; String-Ende-Zeichen
	JR Z,DIRS	; Nach DIR-SHOW
	CP 28H	; C hange
	JR Z,DICHG
	AND A;    Leer=DC (UFM-Konvention)
	JR Z,DICHG
	CP 2AH	; E rase
	JR Z,DIERA
	CP 32H	; M ake
	JR Z,DIMAK
	JP ERROR0
DICHG:	LD A,02h	; Command 02
	JP DIGO
DIMAK:	LD A,06h	; Command 06
	JP DIGO
DIERA:	LD A,05h	; Command 05
	JP DIGO
DIRS:	LD B,13h	; Zeilenz�hler = 19
	CALL SENDMULTI
	db 01h,0dh,0
DIRR:
	CALL RDWAIT
	CP 3eh	; Prompt? Dann Ende
	JP Z,DIRE
	CP 0dh	; CR-Zeichen ?
	JP Z,DIRC
DIRP:	LD E,A
	CALL TEX1
	JP DIRR
DIRE:	CALL RDFLUSH
	JP EXIT_OK	; Zur�ck, DIR fertig!

	; Teste auf Ende der Bildschirmseite
DIRC:	DEC B
	JP NZ,DIRP
	; Weitere Seite wird ben�tigt
	LD E,0dh
	CALL TEX1
	LD E,0dh
	CALL TEX1
	LD E,3Ch	; Prompt <CR> ausgeben
	CALL TEX1
	LD E,43h
	CALL TEX1
	LD E,52h
	CALL TEX1
	LD E,3Eh
	CALL TEX1
	CALL SHOW	; Bildschirm anzeigen
DIRW:	CALL GETKEY	; ROM-Routine zum Tastaturauslesen
	LD A,H	; Test auf FFFF in HL
	AND L
	INC A
	JR Z,DIRW ; Keine Taste gedr�ckt
	
	EX DE,HL
	LD HL,0FDBFh	; CR gedr�ckt?
	AND A		; Reset Z
	SBC HL,DE
	JP Z,DIRCR  ; SIGGI/UFM: Alle Tasten au�er CR beenden DIR
	CALL RDFLUSH
	JP EXIT_OK
DIRCR:
	CALL RCLS	; CLS
	LD B,13h
	JP DIRR		; Letztes Zeichen war CR, hole neues
	
DIGO: ; Kommando ist schon in A
	CALL SENDB
	LD A,20h	; Space
	CALL SENDB
	INC HL			; Zeiger auf Namen im ZX-Format
	CALL SENDZXSTR
	LD A,0Dh
	CALL SENDB
DIGO5:
	CALL RDWAIT
	CP 43h	; C = Fehler
	JR Z,DIGOE
	CP 44h	; D = Fehler
	JR Z,DIGOE
	CP 46h	; F = Fehler
	JR Z,DIGOE
	CP 3Eh	; > = OK
	JR Z,DIGOK
	JR DIGO5	; n�chstes Zeichen
DIGOE:
	CALL RDFLUSH
	JP ERROR0
DIGOK:
	CALL RDFLUSH
	JP EXIT_OK

		

; ZX-String-Zahl in (HL+1) wandeln,
; Zahl wird in BC zur�ck geliefert, benutzt A
; HL zeigt dann auf das erste Zeichen nach der Zahl
GETADDR:
	PUSH DE
	LD D,H	; (Start-Adresse -1) in DE
	LD E,L
	XOR A
	LD H,A		; HL auf Null
	LD L,A
	LD B,4		; vier Ziffern
GALOOP:
	INC DE		; Erst erh�hen, denn Zeiger war auf Addresse-1
	LD A,(DE)	; Zeichen holen
	SUB 1Ch		; 28 subtrahieren
	JR C,GAEND	; War keine Zahl, code kleiner
	CP 10h		; Muss jetzt im Bereich 0..0fh sein
	JR NC,GAEND	; War keine Zahl, code groesser
	ADD HL,HL	; 16-Bit Shift
	ADD HL,HL	
	ADD HL,HL	
	ADD HL,HL	
	OR	L		; Zahl zu HL addieren, kein �berlauf m�glich
	LD	L,A
	DJNZ GALOOP
GAEND:
	LD B,H		; Ergebnis in BC
	LD C,L
	INC DE
	LD H,D		; Letzte Addresse+1 wieder in HL
	LD L,E
	POP DE
	RET

	

HLPTXT:
	db "TTL-VDR 1.06a3",0dh,0dh
	db "INFO  ",22h,"I",22h,0dh
	db "DIR   ",22h,"D",22h,0dh
	db "ERASE ",22h,"ENAME.P",22h,0dh
	db "MKDIR ",22h,"DMVERZ",22h,0dh
	db "RMDIR ",22h,"DEVERZ",22h,0dh
	db "CHDIR ",22h,"DCVERZ",22h,0dh
	db "LOAD  ",22h,"LNAME.P",22h,0dh
	db "BLOAD ",22h,"LNAME.B,SSSS",22h,0dh
	db "SAVE  ",22h,"SNAME.P",22h,0dh
	db "BSAVE ",22h,"SNAME.B,SSSS,EEEE",22h,0dh
	db "RENAME",22h,"ROLDNAME NEWNAME",22h,0dh
	db "HELP  ",22h,"H",22h,0dh
	db "FOR SIGGI'S UFM :V/R/K",0dh
	db 00h


; 1.02-1.03  Alternative Startadresse war verschoben	
; 1.04       Anpassungen an neueste VDR-Firmware, SNAME.C,XXXX,YYYY Bug
; 1.05		 Version f�r silent-loading
; 1.06		 Aufruf vom C/assembler mit call, UFM-Konventionen
; 1.06a3	 Fix resend-wrong data issue

;
; Alternativer Start auf 8888 dez (22B8h)
;
; === ACHTUNG !   Die alternative Startadresse
; === erfordert MANUELLE ANPASSUNG bei �nderungen oberhalb dieser Zeile
;
;	db 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
	db 0,0,0,0,0,0,0,0,0,0,0,0 ; NOP-Rutschbahn, falls die Adresse nicht exakt passt
	JP BASIC_START

	
	
;*
;* ERASE
;*
ERAS1:
	CALL INIT
	CALL SENDMULTI
	db 07h,20h,0
	INC HL			; Zeiger auf Namen im ZX-Format
	CALL SENDZXSTR
	LD A,0Dh
	CALL SENDB
	LD A,3Eh; Prompt = Fertig
	CALL RDUNTILL
	JP EXIT_OK



;*
;* RENAME
;*
RENAM1:
	CALL INIT
	CALL SENDMULTI
	db 0Ch,20h,0
	INC HL			; Zeiger auf beide Namen (mit Leerzeichen dazwischen) im ZX-Format
	CALL SENDZXSTR
	LD A,0Dh
	CALL SENDB
	LD A,3Eh; Prompt = Fertig
	CALL RDUNTILL
	JP EXIT_OK
	
	
; Sende Zeichen hinter dem Aufruf bis 0, benutzt A,D,E,F
SENDMULTI:
	POP DE	; R�cksprungadresse, hier null-terminierter String
SENDMULT2:
	LD A,(DE)
	INC DE
	AND A	; Teste auf 0
	JR Z,SEMULEND
	CALL SENDB
	JR SENDMULT2
SEMULEND:
	PUSH DE
	RET
	
	
; Konvertiere und sende ZX-String in (HL), mit ende=0Bh
SENDZXSTR: ; Leerzeichen am Anfang entfernen
	LD A,(HL)
	AND A
	JR NZ,SENDZXNXT
	INC HL	; Leerzeichen �berspringen
	JR SENDZXSTR
SENDZXNXT:
	CP 0Bh	; String-Ende?
	RET Z
	CP 1Ah	; Komma? - Auch Ende
	RET Z
	CALL ZXASC	; nach ASCII
	CALL SENDB
	INC HL
	LD A,(HL)
	JR SENDZXNXT

;*
;* Hardware Port Init
;*
INIT:
	CALL RDFLUSH
	CALL SENDMULTI
	db 0dh,'S','C','S',0dh,0 ; VDRIVE2 auf Short Command Set
	CALL WAIT
	LD A,03Eh		; Prompt
	call RDUNTILL
	call RDFLUSH	; CR l�schen
	CALL WAIT
	LD A,0dh
	CALL SENDB
	CALL WAIT
	CALL RDWAIT
	CP 03Eh		; Ready ?
	JP NZ,ERROR1 ; Ausstieg Direkt aus dem Unterprogramm
	CALL RDFLUSH
	RET
	
;*
;* STRING AUSWERTEN
;*
NAME:	RST 20h
	CALL 0F55h
	LD A,(4001h)
	ADD A,A
	JP M,0D9Ah
	POP HL
	RET NC
	PUSH HL
	CALL 13F8h
	LD H,D
	LD L,E
	RET

	
HLP:	LD HL,HLPTXT
HLP1:	LD A,(HL)
	CP 00h
	JP Z,EXIT_OK
	LD E,A
	CALL TEX1
	INC HL
	JR HLP1
	
	
SAVE1:
	; Datei �ffnen
	CALL INIT
	CALL SENDMULTI
	db 09h,20h,0 	; Datei er�ffnen
	INC HL			; Zeiger auf Namen im ZX-Format
	PUSH HL			; Zeiger speichern f�r schliessen der Datei
	CALL SENDZXSTR
	LD A,0Dh
	CALL SENDB
	LD A,3Eh; Prompt = Fertig
	CALL RDUNTILL
	CALL RDFLUSH
	; Seek to start
	CALL SENDMULTI
	db 28h,20h,0 	; seek
	LD B,4
SVADLP:				; Addr 0000
	XOR A
	CALL SENDB
	DJNZ SVADLP
	LD A,0Dh
	CALL SENDB
	LD A,3Eh; Prompt = Fertig
	CALL RDUNTILL
	CALL RDFLUSH
	
	; Position und l�nge
	LD A,(HL)
	CP 1Ah	; ist es ein Komma ?
	JR Z,SVGETADDR   ;
	; Defaults f�r Start und Ende setzen
	LD DE,4009h		; Standard Start und Ende setzen
	LD HL,(ELINE)	
	JR SVWRITE
SVGETADDR:
	CALL GETADDR
	LD D,B			; Start nach DE
	LD E,C
	CALL GETADDR	; Ende nach HL
	INC BC			; End-Adresse um 1 erh�hen
	LD H,B
	LD L,C
SVWRITE:
	; L�nge aus Start und Ende berechnen
	AND A		; Carry l�schen f�r SBC
	SBC HL,DE	; HL=L�nge
	LD B,H
	LD C,L
	EX DE,HL	; Jetzt HL=Start, BC=L�nge
	
	; Schreibbefehl
	CALL SENDMULTI
	db 8h,20h,0 	; write
	XOR A
	CALL SENDB
	XOR A
	CALL SENDB
	LD A,B
	CALL SENDB
	LD A,C
	CALL SENDB
	LD A,0Dh
	CALL SENDB
	
	CALL FAST	; Schaltet in den FAST-Mode

SAVELOOP:
	LD A,(HL)
	CALL SENDB	; schreibe die eigentlichen Daten
	INC HL
	DEC BC
	LD A,B
	OR C
	JR NZ,SAVELOOP

	LD A,3Eh; Prompt = Fertig
	CALL RDUNTILL
	CALL RDFLUSH

	; Datei schlie�en, name vom stack

	CALL SENDMULTI
	db 0Ah,20h,0 	; Datei schliessen
	POP HL			; Zeiger auf Namen holen
	CALL SENDZXSTR
	LD A,0Dh
	CALL SENDB
	LD A,3Eh; Prompt = Fertig
	CALL RDUNTILL
	CALL RDFLUSH
	JP EXIT_OK
	
	
	
	
;Laden
LOAD1:
	;Speichere Addresse des Dateinamens
	INC HL		; Zeiger auf Namen im ZX-Format
	PUSH HL		
	;DIR+name f�r Dateil�nge	-> BC
	CALL INIT	; PIO-Port und VDRIVE2 initialisieren
	CALL RDFLUSH
	CALL SENDMULTI
	db 01h,20h,0
	CALL SENDZXSTR
	LD A,0Dh
	CALL SENDB
	CALL RDWAIT		; 0d = name kommt, sonst fehler
	CALL RDWAIT		; Zeichen entsorgen
	POP HL
LDWAITSP:
	CALL RDWAIT		; Warte auf Space (gut) oder Ende (schlecht)
	CP 0dh
	JP Z,ERROR0
	CP 20h
	JR NZ,LDWAITSP
	CALL RDWAIT		; Gr�sse in Bytes in BC
	LD C,A
	CALL RDWAIT
	LD B,A
	PUSH BC
	LD (UFM_COORDS),BC	; L�nge speichern
	LD A, 3Eh
	CALL RDUNTILL	; Prompt >
	CALL RDFLUSH
	;LOAD+name
	CALL SENDMULTI
	db 04h,20h,0
	CALL SENDZXSTR
	LD A,0Dh	; TODO in SENDZXSTR einbauen
	CALL SENDB
	LD A,(HL)	; A h�lt das letzte Zeichen von SENDZXSTR
	;Check explizite Adressangabe ->HL, sonst 4009
	LD B,40h	; Sonst Default Adresse
	LD C,09h
	CP 1Ah		; A h�lt das letzte Zeichen von SENDZXSTR
	JR Z,LDHAVAD; Bei Komma->Neue addresse holen
	CALL FAST	; Neu 1.5: Schaltet nur bei Laden eines BASIC-Programms in den FAST-Mode (nicht bei bin�r)
LDHAVAD:	
	CALL Z,GETADDR	; Bei Komma->Neue addresse holen
	LD H,B
	LD L,C
	; Ausgabe deaktiviert in 1.5
	;LD E,41h
	;CALL TEX1	; Anzeige A:Addr
	;LD E,3Ah
	;CALL TEX1
	;CALL PRHL	; Addr
	;LD E,4Ch
	;CALL TEX1	; Anzeige L:Length
	;LD E,3Ah
	;CALL TEX1
	EX (SP),HL	; Tausche mit L�nge, jetzt Position auf Stack
	;CALL PRHL	; L�nge - dann in BC
	LD B,H
	LD C,L
	POP HL		; Position
	;Lade
	;CALL FAST	; 1.5: Schaltet nur bei Laden eines Basic-Programms in den FAST-Mode (nicht bei bin�r)
LDLOOP:
	CALL RDWAIT; 
	LD  (HL),A
	INC HL
	DEC BC
	LD A,B	; Teste BC auf 0
	OR C
	JR NZ,LDLOOP
	JP EXIT_OK 

	
;*
;* TEXT PRINT GIBT ASCII-CHR
;* IN E AUF DISPLAY AUS
;*
;* AUSGABE N. IM DIREKTMODUS
TEX:	BIT 7,(IY+08h)
	RET Z
;*
;* UEBERPRUEFE PRINTPOS
;* ENDE,WENN BILDSCHIRM VOLL
;* AUSGABE AUCH IM PRG
TEX1:	LD A,(403Ah)
	CP 03h
	RET C
;*
	LD A,E
	PUSH BC
	PUSH HL
;* WERTEBEREICH MASKIEREN
	RES 7,A
	LD C,A
	AND 60h
ASCZX:	JR Z,CTCHR
;* IST ES EIN BUCHSTABE ?
	BIT 6,C
	JR Z,CDCON
;* IN GROSSBUCHST. WANDELN
	RES 5,C
CDCON:	XOR A
	LD B,A
	LD HL,ASCZX
	ADD HL,BC
	LD A,(HL)
	RST 10h
	POP HL
	POP BC
	RET
CTCHR:	LD A,C
	CP 0Dh
	JR NZ,NOCR
	LD A,76h
	RST 10h
NOCR:	POP HL
	POP BC
	RET
	NOP
	NOP
;*
;* CODE-TABELLE (ZX-CODE)
;*
	db 00h,1bh,0bh,0ch,0dh,18h,15h,0bh
	db 10h,11h,17h,15h,1ah,16h,1bh,18h
	db 1ch,1dh,1eh,1fh,20h,21h,22h,23h
	db 24h,25h,0eh,19h,13h,14h,12h,0fh
	db 0a6h,26h,27h,28h,29h,2ah,2bh,2ch
	db 2dh,2eh,2fh,30h,31h,32h,33h,34h
	db 35h,36h,37h,38h,39h,3ah,3bh,3ch
	db 3dh,3eh,3fh,10h,18h,11h,0eh,16h
	NOP
	NOP


; *
; * AUSGABE DEZIMAL
; *
PRHL
	PUSH HL
    XOR A
	PUSH AF
PRTLP   CALL DIV10
	ADD A,1Ch
	PUSH AF
	LD A,H
	OR L
	JR NZ,PRTLP
PRTL1   POP AF
	OR A
	JR NZ,PRT2
	LD A,76h
	RST 10h
	POP HL
	RET
PRT2    RST 10h
	JR PRTL1
; * HL=HL/10 A=REST
DIV10   LD B,10h
	XOR A
DIVLP   SLA L
	RL H
	RLA
	CP 0Ah
	JR C,SK
	SET 0,L
	SUB 0Ah
SK      DJNZ DIVLP
	RET


;*
;* TABELLE ZX IN ASCII
;*
ASCII:	db 020h,020h,020h,020h,020h,020h,020h,020h,020h,020h,020h
	db 022h,023h,024h,07eh,03Fh,028h,029h,03Eh,03Ch,03Dh
	db 02Bh,02Dh,02Ah,05Ch,03Bh,02Ch,02Eh
	db 030h,031h,032h,033h,034h,035h,036h,037h,038h,039h
	db 041h,042h,043h,044h,045h,046h,047h,048h,049h,04Ah,04Bh
	db 04Ch,04Dh,04Eh,04Fh,050h,051h,052h,053h,054h,055h,056h
	db 057h,058h,059h,05Ah
;* Konvertiert Zeichen in A nach ASCII
ZXASC:	PUSH BC
	PUSH HL
	LD HL,ASCII
	LD B,0
	AND 3Fh
	LD C,A
	ADD HL,BC
	LD A,(HL)
	POP HL
	POP BC
	RET

#define IPORT	07Fh
#define IP_OFF	00h
#define IP_S0	02h
#define IP_S1	03h
	
	
; Lese und verwerfe Zeichen bis keine Zeichen mehr im Buffer sind
; A+F wird ge�ndert
RDFLUSH:
	CALL READB
	JR Z,RDFLUSH ; n�chster Versuch
	; Leer, sicherheitshalber aber etwas warten und dann nochmal testen
	CALL WAIT
	CALL READB
	RET NZ		; auch nach warten kein Zeichen (mehr) im Buffer
	JR RDFLUSH  ; nochmal das ganze
	

; Lese und verwerfe Zeichen bis Zeichen mit A identisch
; A+F wird ge�ndert
RDUNTILL:
	PUSH DE
	LD	E,A			; Vergleichszeichen
RDUNT1:
	CALL READB
	JR NZ,RDUNT2	; kein neues Zeichen?
	CP E
	JR Z,RDMATCH
RDUNT2:
	; teste BREAK
	CALL TESTBREAK
	JR NZ,RDUNT1 	; n�chster Versuch
	; break gedr�ckt
	LD A,67
RDMATCH:
	POP DE
	RET
	
; Warte auf das n�chste Zeichen und Lese in A
; A+F wird ge�ndert
RDWAIT:
	CALL READB
	RET Z	;  neues Zeichen?
	CALL TESTBREAK
	JR NZ,RDWAIT
	; break gedr�ckt
	LD A,66
	RET
	
	
;*
;* Lese ein Byte
;*
; Return: Byte in A
;         Zeroflag : 0 = old data / 1 = valid data

READB:
	PUSH BC
	PUSH DE
	LD C,IPORT
	; Dummy clock, not selected
	LD B,IP_OFF
	IN D,(C)
	; Start bit, selected
	LD B,IP_S1
	IN D,(C)
	; Read indication bit
	LD B,IP_S1
	IN D,(C)
	; Read from data indication
	LD B,IP_S0
	IN D,(C)
	; Daten seriell in A einlesen
	LD E,4	; ein Byte hat 8=4*2 Bit ( hab's nachgeschlagen :-)
	LD B,IP_S0

RDBITLOOP:	  ; Geschwindigkeitskritische innere Schleife
	IN D,(C)  ;  Takt und lesen
	RL D
	RLA

	IN D,(C)  ;  Ein Zwiters mal pro schleife, damit es schneller geht
	RL D
	RLA

	DEC E
	JR NZ,RDBITLOOP

	; Jetzt noch den Status: 0=new data , 1=old
	LD E,A    ; Sichere gelesenes Zeichen
	XOR A
	LD B,IP_S0
	IN D,(C)
	RL D
	RLA
	; Zum Schluss noch ein Dummy Clock Impuls, not selected
	LD B,IP_OFF
	IN B,(C)
	OR A	; Zeroflag entsprechend dem Status in A setzen
#ifdef VERBOSE	
	PUSH DE
	PUSH AF
	CALL z,TEX1
	POP AF
	POP DE
#endif
	LD A,E  ; Zeichen zur�ck in A
	POP DE
	POP BC
	RET

;*
;* Sende Byte in A
;*
; Byte zm senden in A
; sendet bis erfolgreich gesendet oder BREAK gedr�ckt
; Return: A=0; ZF=0 : OK
; A=8; ZF=1 : Buffer full
SENDB:
	PUSH BC
	PUSH DE
	LD E,A		; save original data in case we have to resend
	PUSH DE
#ifdef VERBOSE	
	PUSH AF
	LD E,A
	CALL TEX1
	POP AF
#endif
	LD C,IPORT
	; Dummy clock, not selected
	LD B,IP_OFF
	IN B,(C)
	; Start bit
	LD B,IP_S1
	IN B,(C)
	; Write bit
	LD B,IP_S0
	IN B,(C)
	; 
	LD B,IP_S0
	IN B,(C)
	LD E,8
	LD D,IP_S0
WRBITLOOP:	
	; Send Bit 
	LD B,D
	ADD A,A
	JR NC, WRBITEND
	INC B
WRBITEND:
	IN B,(C)
	DEC E
	JR NZ,WRBITLOOP
	; Get Status 0=ok , 1=buffer full
	XOR A
	LD B,IP_S0
	IN B,(C)
	JP P,WROK
	LD A,8
WROK:
	; Final dummy clock, not selected
	LD B,IP_OFF
	IN B,(C)
	BIT 3,A
	POP DE	; Here E is original data
	LD A,E	; Original data back to A
	POP DE
	POP BC
	RET Z
	; Buffer war voll, teste auf break und dann nochmal
	CALL TESTBREAK
	JR NZ,SENDB
	; break gedr�ckt
	LD A,8
	RET
	
;	Zeroflag ist gesetzt, wenn BREAK gedr�ckt
;	A wird benutzt
TESTBREAK:
	LD A,7FH	; aktiviere halbreihe mit BREAK
	IN A,(0FEH)
	BIT 0,A
	RET	



 ;  | 
 ;  \ 
#IF ISLOADER
 
   db $76   ;N/L 
 
line10: ; Testweise einen DIR-Befehl ausf�hren
   db 0,10  ;line number 
   dw line20-$-2  ;line length 
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
   db $29   ; D
   db $0b   ; "
   db $76   ;N/L 

line20: ; Ank�ndigung zum Laden des Treibers ins gepufferte RAM 
   db 0,20  ;line number 
   dw line30-$-2  ;line length 
   db $f5   ;PRINT 
   db $1a   ; ,
   db $1a   ; ,
   db $0b   ; "
   db $12   ; >
   db $31   ; L
   db $29   ; D
   db $00   ; 
   db $3b   ; V
   db $29   ; D
   db $37   ; R
   db $1b   ; .
   db $27   ; B
   db $2E   ; I
   db $33   ; N
   db $0e   ; :
   db $0b   ; "
   db $76   ;N/L 
   
line30: ; Laden des Treibers ins gepufferte RAM 
   db 0,30  ;line number 
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
   db $31   ; L
   db $3b   ; V
   db $29   ; D
   db $37   ; R
   db $1b   ; .
   db $27   ; B
   db $2E   ; I
   db $33   ; N
   db $1a   ; ,
   db $1e   ; 2
   db $1c   ; 0
   db $1c   ; 0
   db $1c   ; 0
   db $0b   ; "
   db $76   ;N/L 

   
;- Display file -------------------------------------------- 
 
dfile: 
   db $76 
   db $76,$76,$76,$76,$76,$76,$76,$76 
   db $76,$76,$76,$76,$76,$76,$76,$76 
   db $76,$76,$76,$76,$76,$76,$76,$76 
 
;- BASIC-Variables ---------------------------------------- 
 
var: 
   db $80 
 
;- End of program area ---------------------------- 

#ENDIF ;ISLOADER
 
last: 
 
   end 