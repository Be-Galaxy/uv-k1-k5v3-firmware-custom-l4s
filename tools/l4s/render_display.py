"""Render the production LCD UI with host hooks and the actual firmware fonts. No radio access."""
import argparse
import ctypes
import struct
from pathlib import Path
from PIL import Image, ImageDraw
from protocol import body, OPEN, MODE, PAIR, PTT, CW, CONTEXT, TONE
from simulator import Firmware

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output')
    parser.add_argument('--menu',action='store_true')
    args=parser.parse_args()
    labels=('Mode/power/operating CTCSS','CW parameters and squelch','Apply and save') if args.menu else (
        'FM: mode and operating CTCSS','Phone PTT: Doppler held','CW: mode visible before/while sending','Physical PTT: session kept')
    canvas=Image.new('RGB',(544,300*len(labels)),'#202020')
    draw=ImageDraw.Draw(canvas)
    for index,label in enumerate(labels):
        fw=Firmware();seq=0
        def send(command,data=b''):
            nonlocal seq
            seq+=1
            return fw.request(body(command,seq,123,data),10)
        send(OPEN);send(MODE,bytes([3 if index==2 else 0]*2+[0]))
        send(CONTEXT,struct.pack('<Hh16s',1370,340,b'DOSAAF-85'))
        send(PAIR,struct.pack('<IIBI',435100010,145900000,1,1))
        if index!=2 or args.menu: send(TONE,struct.pack('<H',670))
        if args.menu:
            fw.lib.Host_MenuOpen()
            for _ in range(index*5):fw.lib.Host_MenuKey(12)
        else:
            if index==1: send(PTT,b'\1'+struct.pack('<I',1))
            if index==2: send(CW,b'\24\4TEST')
            if index==3: fw.lib.Host_LocalPtt(1,10)
        frame=ctypes.create_string_buffer(896)
        fw.lib.Host_Render.argtypes=[ctypes.c_void_p]
        if fw.lib.Host_Render(frame): raise RuntimeError('LCD layout overflow')
        lcd=Image.new('RGB',(128,56),'#d4dc8d')
        for row in range(7):
            for x in range(128):
                for bit in range(8):
                    if frame.raw[row*128+x] & (1<<bit): lcd.putpixel((x,row*8+bit),(20,28,14))
        draw.text((16,index*300+8),label,fill='white')
        canvas.paste(lcd.resize((512,224),Image.Resampling.NEAREST),(16,index*300+32))
    canvas.save(Path(args.output))

if __name__=='__main__': main()
