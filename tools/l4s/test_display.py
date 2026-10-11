import ctypes
import struct
import unittest
from protocol import body, OPEN, MODE, PAIR, PTT, CW, STOP
from simulator import Firmware

class DisplayTest(unittest.TestCase):
    def setUp(self):
        self.fw=Firmware()
        self.seq=0
        self.send(OPEN)
        self.fw.lib.Host_Render.argtypes=[ctypes.c_void_p]
        self.fw.lib.Host_DisplayText.argtypes=[ctypes.c_uint]
        self.fw.lib.Host_DisplayText.restype=ctypes.c_char_p

    def send(self,command,data=b''):
        self.seq+=1
        return self.fw.request(body(command,self.seq,123,data),10)

    def pair(self,rx=435100010,tx=145900000,enabled=1,revision=1):
        return self.send(PAIR,struct.pack('<IIBI',rx,tx,enabled,revision))

    def render(self):
        frame=ctypes.create_string_buffer(896)
        self.assertEqual(0,self.fw.lib.Host_Render(frame),'LCD text overflow')
        return [self.fw.lib.Host_DisplayText(i).decode() for i in range(7)]

    def test_received_pair_is_rendered_in_rf_hz(self):
        self.pair()
        lines=self.render()
        self.assertEqual('RX 435.10001',lines[1])
        self.assertEqual('TX 145.90000',lines[3])
        self.assertIn('TRACKING',lines[5])
        self.assertIn('EXIT:STOP',lines[6])

    def test_voice_tx_hold_rejects_retune_and_keeps_displayed_pair(self):
        self.send(MODE,b'\0\0\0');self.pair()
        self.send(PTT,b'\1'+struct.pack('<I',1))
        self.assertEqual(4,self.pair(rx=435110000,revision=2)[5])
        lines=self.render()
        self.assertEqual('RX 435.10001',lines[1])
        self.assertIn('HOLD',lines[5])
        self.send(STOP)
        self.assertNotIn('HOLD',self.render()[5])

    def test_completed_cw_window_remains_held_until_stop(self):
        self.send(MODE,b'\3\3\0');self.pair()
        self.send(CW,b'\24\4TEST')
        self.fw.lib.Host_Tick(500)
        self.assertIn('CW 4/4',self.render()[5])
        self.assertIn('HOLD',self.render()[5])

    def test_receive_only_and_largest_rx_frequency_fit_screen(self):
        self.pair(rx=1300000000,tx=0,enabled=0)
        lines=self.render()
        self.assertEqual('RX 1300.00000',lines[1])
        self.assertEqual('TX ---.-----',lines[3])

    def test_physical_ptt_has_its_own_source_label(self):
        self.send(MODE,b'\0\0\0');self.pair()
        self.assertEqual(0,self.fw.lib.Host_LocalPtt(1,10))
        self.assertEqual('LOCAL TX / HOLD',self.render()[5])

if __name__=='__main__': unittest.main()
