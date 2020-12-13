
import os


gmain="""
      # # #               #    #     ##  ###  ##   #   B Y Z X
0 O # # # # #     ###  # #    #   # #   #    #  #      
##### # # # # #    #    #  ## # # # ###  ##  ###  ##   T E A M
  ++# # # # #     #    # #    # # # #      # #     #   
      # # #      ##### #  #    # #   ### ##  #   ####  2 0 2 0
"""


gwmain="""
      # # #        #   #    #    ##    #   #      ###  W I F I
0 O # # # # #     #   #    #    #  #  # # #      #   
##### # # # # #   # # # ## #    # ##  # # #     #  ##    C O N 
  ++# # # # #     # # #    #    ## #  #  #      # #  
      # # #        # #      ### #  # #   #      # # #    F I G
"""

gwvideo="""
      # # #       #    # ### ###     ###  ########   V I D E O
0 O # # # # #      #   #  #   # #   #    #++++++++#     
##### # # # # #     #  #  #   #  # ####  #++++++++#      C O N 
  ++# # # # #       # #   #   #  # #     #++++++++#    
      # # #          #   ### ####   ###   ########       F I G
"""

gsystem="""
      # # #           #####                              S Y S
0 O # # # # #       ##++++#########################  
##### # # # # #            ###++++++++++++++++++++##     C O N 
  ++# # # # #       ##++++#########################    
      # # #           #####                              F I G
"""



def adjust_lines( l_tupl ):
    ll=max( len(l) for l in l_tupl )
    if ll%2: ll+=1

    return [ l.ljust(ll) for l in l_tupl ]




def feedlines(inps):
    m=None
    for l in inps.splitlines():
        if m: 
            yield adjust_lines( [m,l] ) 
            m=None
        else:
            m=l
    if m:
        yield adjust_lines( [m,''] ) 

def feedchar(lins):
    m=None
    for c in zip(*lins):
        if m: 
            yield (m,c)
            m=None
        else:
            m=c


def cvrt(gstr,line):
    for lins in feedlines(gstr):
        #print(lins)
        zxl=[]
        for c in feedchar(lins):
            #print(c)
            l,r=c
            ul,ll=l
            ur,lr=r
            zxc=0
            if ul=='#': zxc|=1
            if ur=='#': zxc|=2
            if ll=='#': zxc|=4
            if lr=='#': zxc^=0x87
            urow=ul+ur
            lrow=ll+lr
            cs=urow+lrow
            if cs=='++++' : zxc=8
            if cs=='  ++' : zxc=9
            if cs=='++  ' : zxc=10
            if cs=='##++' : zxc=137
            if cs=='++##' : zxc=138
            #if '+' in cs:print("MMM:",cs)
            for c in cs:
                if ord('A')<= ord(c)<=ord('Z'): zxc=38+ord(c)-ord('A')
                if ord('0')<= ord(c)<=ord('9'): zxc=28+ord(c)-ord('0')
                if c=='-': zxc=22
            zxl.append(zxc)
        while zxl[-1]==0: del zxl[-1]
        zxlstr=''.join( f'\\x{c:02x}' for c in zxl )            
        print (f'    zxfimg_cpzx_video ({line}, (const uint8_t *) "{zxlstr}", {len(zxl)});')
        line+=1
    


if __name__ == "__main__":
    print("MAIN")
    cvrt(gmain,0)
    print("WIFI")
    cvrt(gwmain,0)
    print("VIDEO")
    cvrt(gwvideo,0)
    print("SYS")
    cvrt(gsystem,0)

    
