
import os

os.system("dir")
if __name__ == "__main__":
    preamb=""#"zx-iot-mi/"
    wf=open(preamb+"main/asm_code.c",'w')
    for filn,varn in [ ('loader','ldrfile'), ('menu1k','menufile'), ('strin1k','str_inp') ]:
        r=os.system(f'''"C:\Program Files (x86)\Tasm32\Tasm.exe" -80 -b {preamb}main/asm/{filn}.asm {preamb}main/asm/{filn}.p''')
        if r: break
        f=open(f"{preamb}main/asm/{filn}.p",'rb').read()
        hx=f"\nconst uint8_t {varn}[]="+"{"+",".join(["0x%02x"%v for v in f])+"};\n"
        #print( hx )
        print( r,len(f) )
        wf.write(hx)
    wf.close()
