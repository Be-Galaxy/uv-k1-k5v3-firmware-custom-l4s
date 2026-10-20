import ctypes
import struct
import unittest
from protocol import body, OPEN, MODE, PAIR, PTT, CW, STOP, CONTEXT, TONE
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
        self.assertIn('WAIT PHONE SYNC',self.render()[6])
        self.send(MODE,b'\0\0\0')
        self.pair()
        lines=self.render()
        self.assertEqual('RX 435.10001',lines[2])
        self.assertEqual('TX 145.90000',lines[4])
        self.assertIn('FM',lines[0])
        self.assertIn('MENU',lines[6])

    def test_voice_tx_hold_rejects_retune_and_keeps_displayed_pair(self):
        self.send(MODE,b'\0\0\0');self.pair()
        self.send(PTT,b'\1'+struct.pack('<I',1))
        self.assertEqual(4,self.pair(rx=435110000,revision=2)[5])
        lines=self.render()
        self.assertEqual('RX 435.10001',lines[2])
        self.assertIn('HOLD',lines[6])
        self.send(STOP)
        self.assertNotIn('HOLD',self.render()[6])

    def test_completed_cw_window_remains_held_until_stop(self):
        self.send(MODE,b'\3\3\0');self.pair()
        self.send(CW,b'\24\4TEST')
        self.fw.lib.Host_Tick(500)
        self.assertIn('CW 4/4',self.render()[6])
        self.assertIn('HOLD',self.render()[6])

    def test_receive_only_and_largest_rx_frequency_fit_screen(self):
        self.pair(rx=1300000000,tx=0,enabled=0)
        lines=self.render()
        self.assertEqual('RX 1300.00000',lines[2])
        self.assertEqual('TX DISABLED',lines[4])

    def test_physical_ptt_has_its_own_source_label(self):
        self.send(MODE,b'\0\0\0');self.pair()
        self.assertEqual(0,self.fw.lib.Host_LocalPtt(1,10))
        self.assertIn('HOLD',self.render()[6])

    def test_satellite_live_context_and_below_horizon_elevation(self):
        self.pair()
        self.send(CONTEXT,struct.pack('<Hh16s',1370,-340,b'DOSAAF-85'))
        lines=self.render()
        self.assertEqual('DOSAAF-85 FM',lines[0])
        self.assertEqual('Az:137 El:-34 R0',lines[1])
        self.send(CONTEXT,struct.pack('<Hh16s',3599,900,b'ABCDEFGHIJKLMNOP'))
        self.render()  # Longest name / angular extrema remain inside the LCD.

    def test_context_update_during_tx_does_not_tune_or_steal_ptt(self):
        self.send(MODE,b'\0\0\0');self.pair()
        self.fw.lib.Host_LocalPtt(1,10)
        writes=self.fw.lib.Host_Writes()
        self.assertEqual(0,self.send(CONTEXT,struct.pack('<Hh16s',1000,200,b'ISS'))[5])
        self.assertEqual(writes,self.fw.lib.Host_Writes())
        self.assertIn('HOLD',self.render()[6])

    def test_invalid_context_does_not_replace_valid_display(self):
        self.send(CONTEXT,struct.pack('<Hh16s',1000,200,b'ISS'))
        self.assertEqual(5,self.send(CONTEXT,struct.pack('<Hh16s',3600,200,b'BAD'))[5])
        self.assertEqual('ISS FM',self.render()[0])

    def test_mode_and_ctcss_remain_visible_before_transmission(self):
        self.send(MODE,b'\0\0\0');self.pair()
        self.send(TONE,struct.pack('<H',670))
        self.assertIn('FM',self.render()[0])
        self.assertIn('T67.0',self.render()[6])
        self.send(MODE,b'\3\3\0')
        self.assertIn('CW',self.render()[0])
        self.assertIn('Toff Roff',self.render()[6])

    def test_menu_pages_are_bounded_and_have_power_mode_tone_cw_entries(self):
        self.lib=self.fw.lib
        self.lib.Host_MenuOpen()
        first=self.render()
        self.assertIn('MODE',first[1]);self.assertIn('POWER',first[2]);self.assertIn('TX CTCSS',first[3])
        for _ in range(5):self.lib.Host_MenuKey(12)
        second=self.render()
        self.assertIn('ARM NEXT',second[1]);self.assertIn('CW WPM',second[2]);self.assertIn('CW TONE',second[3])
        self.assertIn('CW VOLUME',second[4]);self.assertIn('SQUELCH 0 OFF',second[5])
        for _ in range(5):self.lib.Host_MenuKey(12)
        third=self.render()
        self.assertIn('RX B ',third[1]);self.assertIn('TX B ',third[2]);self.assertIn('RX CORR',third[3])
        self.assertIn('TX CORR',third[4]);self.assertIn('CW HANG',third[5])
        for _ in range(7):self.lib.Host_MenuKey(12)
        final=self.render()
        self.assertIn('CW HANG',final[2]);self.assertIn('CW KEYER',final[3]);self.assertIn('CW SPACING',final[4])
        self.assertIn('APPLY + SAVE',final[5])

if __name__=='__main__': unittest.main()
