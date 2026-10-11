"""Preview production LCD transaction screens with host hooks, not device screenshots."""
import ctypes
import struct
import sys
from PIL import Image, ImageDraw
from simulator import Firmware
from test_io import Config
from protocol import body, OPEN, MODE, PAIR

def main(output):
    canvas=Image.new('RGB',(544,600),'#202020');draw=ImageDraw.Draw(canvas)
    for index,label in enumerate(('Nonblocking snapshot save','Physical confirmation before deleting')):
        fw=Firmware();lib=fw.lib;lib.Host_IoReset()
        for seq,(cmd,data) in enumerate(((OPEN,b''),(MODE,b'\0\0\0'),(PAIR,struct.pack('<IIBI',435100000,145900000,1,1))),1):
            fw.request(body(cmd,seq,123,data),10)
        lib.Host_StoreUpsert.argtypes=[ctypes.c_void_p,ctypes.POINTER(Config),ctypes.c_uint32]
        config=Config(0,3,20,60,4,1,0,4,0,435100000,145900000,10,-20);key=bytes(range(16))
        if lib.Host_StoreUpsert(key,ctypes.byref(config),1)!=1:raise RuntimeError('Cannot begin save')
        now=0
        for _ in range(150):
            now+=10;lib.Host_StoreTick(now)
            if index==0 and lib.Host_StoreProgress()>=35:break
            if index==1 and lib.Host_StorePhase()==9:break
        if index==1:
            lib.Host_StoreCommand.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_bool,ctypes.POINTER(ctypes.c_uint16)]
            request=b'\5'+struct.pack('<II',1,2)+key+b'\0';step=ctypes.c_uint16(10)
            lib.Host_StoreCommand(request,len(request),True,ctypes.byref(step))
            for _ in range(150):
                now+=10;lib.Host_StoreTick(now)
                if lib.Host_StorePhase()==6:break
        frame=ctypes.create_string_buffer(896);lib.Host_Render.argtypes=[ctypes.c_void_p]
        if lib.Host_Render(frame):raise RuntimeError('LCD layout overflow')
        lcd=Image.new('RGB',(128,56),'#d4dc8d')
        for row in range(7):
            for x in range(128):
                for bit in range(8):
                    if frame.raw[row*128+x] & (1<<bit):lcd.putpixel((x,row*8+bit),(20,28,14))
        draw.text((16,index*300+8),label,fill='white')
        canvas.paste(lcd.resize((512,224),Image.Resampling.NEAREST),(16,index*300+32))
    canvas.save(output)

if __name__=='__main__':main(sys.argv[1])
