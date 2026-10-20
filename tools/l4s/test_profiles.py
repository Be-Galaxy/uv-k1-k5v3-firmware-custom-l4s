import ctypes
import struct
import unittest
from protocol import body, PROFILE, OPEN, SETTINGS, MODE, PAIR, PTT, STOP
from simulator import Firmware
from test_io import Config

class ProfilesTest(unittest.TestCase):
    def setUp(self):
        self.fw=Firmware();self.lib=self.fw.lib;self.lib.Host_IoReset()
        self.lib.Host_SaveSatellite.argtypes=[ctypes.c_void_p,ctypes.POINTER(Config)]
        self.lib.Host_SaveSatellite.restype=ctypes.c_uint8
        self.lib.Host_LoadSatellite.argtypes=[ctypes.c_void_p,ctypes.POINTER(Config)]
        self.lib.Host_LoadSatellite.restype=ctypes.c_bool
        self.lib.Host_SatelliteData.restype=ctypes.POINTER(ctypes.c_uint8)
        self.seq=0;self.send(OPEN)

    def send(self,command,data=b''):
        self.seq+=1
        return self.fw.request(body(command,self.seq,123,data),10)

    def select(self,key):
        return self.send(PROFILE,b'\0'+key+bytes(range(16))+b'SATELLITE'.ljust(16,b'\0'))

    def config(self,rx=435100000,tx=145900000):
        return Config(1,3,25,70,4,1,9,4,3,rx,tx,-500,750)

    def test_multiple_satellite_records_survive_reselection_and_restart(self):
        first=bytes(range(16));second=bytes(range(16,32))
        a=self.config();b=self.config(435500000,145950000);b.mode=0
        self.assertEqual(0,self.lib.Host_SaveSatellite(first,ctypes.byref(a)))
        self.assertEqual(0,self.lib.Host_SaveSatellite(second,ctypes.byref(b)))
        for key,expected in ((first,a),(second,b),(first,a)):
            reply=self.select(key)
            self.assertEqual(79,len(reply)-12)
            self.assertTrue(reply[12+38]&16)
            self.assertEqual(expected.mode,reply[12+24])
            self.assertEqual(expected.rx_base_hz,struct.unpack_from('<I',reply,12+56)[0])
            self.assertEqual(expected.rx_correction_hz,struct.unpack_from('<i',reply,12+64)[0])
        self.lib.Host_Reset();self.seq=0;self.send(OPEN)
        self.assertEqual(a.rx_base_hz,struct.unpack_from('<I',self.select(first),12+56)[0])

    def test_unchanged_write_skips_flash_and_full_bank_never_evicts(self):
        config=self.config();first=bytes([1])*16
        self.assertEqual(0,self.lib.Host_SaveSatellite(first,ctypes.byref(config)))
        self.assertEqual(0,self.lib.Host_SaveSatellite(first,ctypes.byref(config)))
        self.assertEqual(1,self.lib.Host_ProfileWrites())
        for index in range(2,57):
            self.assertEqual(0,self.lib.Host_SaveSatellite(bytes([index])*16,ctypes.byref(config)))
        before=bytes(self.lib.Host_SatelliteData()[:56*64])
        self.assertEqual(13,self.lib.Host_SaveSatellite(bytes([57])*16,ctypes.byref(config)))
        self.assertEqual(before,bytes(self.lib.Host_SatelliteData()[:56*64]))
        config.rx_correction_hz=1000
        self.assertEqual(0,self.lib.Host_SaveSatellite(first,ctypes.byref(config)))
        loaded=Config();self.assertTrue(self.lib.Host_LoadSatellite(first,ctypes.byref(loaded)))
        self.assertEqual(1000,loaded.rx_correction_hz)

    def test_crc_unknown_version_and_failed_readback_are_safe(self):
        key=bytes(range(16));config=self.config()
        self.assertEqual(0,self.lib.Host_SaveSatellite(key,ctypes.byref(config)))
        data=self.lib.Host_SatelliteData();data[7]^=1
        loaded=Config();self.assertFalse(self.lib.Host_LoadSatellite(key,ctypes.byref(loaded)))
        self.assertEqual(0,self.lib.Host_SaveSatellite(key,ctypes.byref(config)))
        data[4]=3
        before=bytes(data[:56*64])
        self.assertEqual(12,self.lib.Host_SaveSatellite(key,ctypes.byref(config)))
        self.assertEqual(before,bytes(data[:56*64]))
        self.lib.Host_IoReset();self.lib.Host_CorruptWrite(True)
        self.assertEqual(12,self.lib.Host_SaveSatellite(key,ctypes.byref(config)))
        self.assertFalse(self.lib.Host_LoadSatellite(key,ctypes.byref(loaded)))

    def test_bases_are_ram_only_and_generation_guards_radio_edits(self):
        selected=self.select(bytes(range(16)))
        generation=struct.unpack_from('<I',selected,12+74)[0]
        payload=b'\1'+struct.pack('<IIHI',435100000,145900000,250,generation)
        self.assertEqual(0,self.send(PROFILE,payload)[5])
        self.assertEqual(0,self.lib.Host_ProfileWrites());self.assertEqual(0,self.lib.Host_Writes())
        self.assertEqual(9,self.send(PROFILE,payload[:-4]+struct.pack('<I',generation-1))[5])
        self.assertEqual(55,len(self.send(SETTINGS))-12)
        self.assertEqual(5,self.send(PROFILE,b'\1'+struct.pack('<IIHI',435100000,145900000,251,generation))[5])

    def test_profile_switch_and_base_change_are_rejected_during_tx_or_menu(self):
        selected=self.select(bytes(range(16)))
        generation=struct.unpack_from('<I',selected,12+74)[0]
        payload=b'\1'+struct.pack('<IIHI',435100000,145900000,250,generation)
        self.send(MODE,b'\0\0\0');self.send(PAIR,struct.pack('<IIBI',435100000,145900000,1,1))
        self.send(PTT,b'\1'+struct.pack('<I',1))
        writes=self.lib.Host_Writes()
        self.assertEqual(4,self.send(PROFILE,payload)[5])
        self.assertEqual(4,self.select(bytes(range(16,32)))[5])
        self.assertEqual(writes,self.lib.Host_Writes())
        self.assertEqual(0,self.send(PROFILE,b'\2')[5])
        self.send(STOP);self.lib.Host_MenuOpen()
        self.assertEqual(4,self.send(PROFILE,payload)[5])

    def test_menu_frequency_and_correction_extremes_fit_lcd(self):
        config=self.config(1300000000,1300000000)
        config.rx_correction_hz=-100000;config.tx_correction_hz=100000
        key=bytes(range(16))
        self.assertEqual(0,self.lib.Host_SaveSatellite(key,ctypes.byref(config)))
        self.select(key);self.lib.Host_MenuOpen()
        for _ in range(10):self.lib.Host_MenuKey(12)
        frame=ctypes.create_string_buffer(896)
        self.lib.Host_Render.argtypes=[ctypes.c_void_p]
        self.assertEqual(0,self.lib.Host_Render(frame))

    def test_menu_uses_phone_step_for_independent_bases_and_corrections(self):
        selected=self.select(bytes(range(16)))
        generation=struct.unpack_from('<I',selected,12+74)[0]
        self.assertEqual(0,self.send(PROFILE,b'\1'+struct.pack('<IIHI',435100000,145900000,250,generation))[5])
        self.lib.Host_MenuDraft.argtypes=[ctypes.POINTER(Config)]
        self.lib.Host_MenuOpen()
        for _ in range(10):self.lib.Host_MenuKey(12)
        for direction in (11,12,11,12):
            self.lib.Host_MenuKey(10);self.lib.Host_MenuKey(direction);self.lib.Host_MenuKey(10);self.lib.Host_MenuKey(12)
        config=Config();self.lib.Host_MenuDraft(ctypes.byref(config))
        self.assertEqual((435100250,145899750,10,-10),
            (config.rx_base_hz,config.tx_base_hz,config.rx_correction_hz,config.tx_correction_hz))
        self.lib.Host_MenuKey(13)
        self.assertEqual(0,self.send(PROFILE,b'\1'+struct.pack('<IIHI',435100000,145900000,1000,generation))[5])
        self.lib.Host_MenuOpen()
        for _ in range(10):self.lib.Host_MenuKey(12)
        self.lib.Host_MenuKey(10);self.lib.Host_MenuKey(11)
        self.lib.Host_MenuDraft(ctypes.byref(config));self.assertEqual(435101000,config.rx_base_hz)

if __name__=='__main__':unittest.main()
