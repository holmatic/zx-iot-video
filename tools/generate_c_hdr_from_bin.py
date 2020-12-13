
import os

os.system("dir")
if __name__ == "__main__":
    wf=open("zx-iot-mi/main/asm_code.c",'w')
    for filn,varn in [ ('loader','ldrfile'), ('menu1k','menufile'), ('strin1k','str_inp') ]:
        r=os.system(f'''"C:\Program Files (x86)\Tasm32\Tasm.exe" -80 -b zx-iot-mi/main/asm/{filn}.asm zx-iot-mi/main/asm/{filn}.p''')
        if r: break
        f=open(f"zx-iot-mi/main/asm/{filn}.p",'rb').read()
        hx=f"\nconst uint8_t {varn}[]="+"{"+",".join(["0x%02x"%v for v in f])+"};\n"
        #print( hx )
        print( r,len(f) )
        wf.write(hx)
    wf.close()
