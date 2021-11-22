
import os
"""
#define VBITMAP_IS_VALID_CHAR_L2 0x01
#define VBITMAP_IS_BLOCK_CHAR_L2 0x04
#define VBITMAP_IS_GREY_CHAR_L2  0x08
#define VBITMAP_IS_INV_CHAR_L2   0x02
#define VBITMAP_IS_BLOCK_CHAR_L6 0x40
#define VBITMAP_IS_GREY_CHAR_L6  0x80

"""
os.system("dir")
if __name__ == "__main__":
    try:
        rom=open("tools/zx81rom.bin",'rb').read()
    except FileNotFoundError:
        rom=open("C:/Inter/Archive/roms/zx81.rom",'rb').read()
    #C:\Inter\Archive\roms
    charset=rom[-512:]
    #print(charset)
    out_flags=[0]*256   # map bitmap to character flags
    hshmap={}
    for inv in (0,0xff):
        for ch in range(64):
            m2=charset[8*ch+2]^inv
            m6=charset[8*ch+6]^inv
            if inv: ch+=128
            f=0
            out_flags[m2] |= 0x01 # valid char
            if m2 in (0x0f,0xf0,0xff): out_flags[m2]  |= 0x04  # block grapics
            elif m2 in (0xaa,0x55): out_flags[m2]  |= 0x08  # chequered graphs
            elif (m2&0x81) == 0x81: out_flags[m2] |= 0x02  # inverse letter

            out_flags[m6] |= 0x10 # valid char
            if m6 in (0x0f,0xf0,0xff): out_flags[m6]  |= 0x40  # block grapics in lower part
            elif m6 in (0xaa,0x55): out_flags[m6]  |= 0x80  # chequered graphs in lower part
            elif (m6&0x81) == 0x81: out_flags[m6] |= 0x20  # inverse letter


            #hsh=(charset[8*ch+1]^inv) * 16  +  (charset[8*ch+4]^inv)*4 + (charset[8*ch+5]^inv) * 2 + (charset[8*ch+6]^inv)
            #if inv: ch+=128
            #if hsh in hshmap:
            #    hshmap[hsh].append(ch)
            #else:
            #    hshmap[hsh]=[ch]
    print(len(hshmap))
    #print(hshmap)
    print(out_flags)

    print(\
"""

/* ZX81 charset as stored in rom  */
"""
    )
    print(f"const uint8_t zx_charset[512]={{{','.join('0x%02X'%v for v in charset)}}}; \n\n")
    print(\
"""

#define VBITMAP_IS_VALID_CHAR_L2 0x01
#define VBITMAP_IS_BLOCK_CHAR_L2 0x04
#define VBITMAP_IS_GREY_CHAR_L2  0x08
#define VBITMAP_IS_INV_CHAR_L2   0x02
#define VBITMAP_IS_VALID_CHAR_L6 0x10
#define VBITMAP_IS_BLOCK_CHAR_L6 0x40
#define VBITMAP_IS_GREY_CHAR_L6  0x80
#define VBITMAP_IS_INV_CHAR_L6   0x06

/* For the standard ZX81 charset, every possible bit map pattern (when looking at line 2 or 6 of the character) maps to flags that show if this is a valid char, and possible also if it is graphics etc */
"""
    )
    print(f"const uint8_t vbitm_flags[256]={{{','.join('0x%02X'%v for v in out_flags)}}}; \n\n")
    print(f"Valid patterns: {len(out_flags)-out_flags.count(0)} of 256 possible patterns ")

