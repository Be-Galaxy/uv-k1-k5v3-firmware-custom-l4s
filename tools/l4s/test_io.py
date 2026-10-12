import ctypes
import re
import struct
import unittest
from pathlib import Path
from protocol import body, OPEN, STATUS, SETTINGS, MODE, PAIR, PTT, TONE, frame, Decoder, CONTEXT, crc
from simulator import Firmware, ROOT

class Config(ctypes.Structure):
    _fields_=[(name,ctypes.c_uint8) for name in ('mode','power','wpm','pitch','volume','tx_tone','rx_tone','arm_tone','squelch')]+[
        ('rx_base_hz',ctypes.c_uint32),('tx_base_hz',ctypes.c_uint32),('rx_correction_hz',ctypes.c_int32),('tx_correction_hz',ctypes.c_int32)]

class IoTest(unittest.TestCase):
    def setUp(self):
        self.fw=Firmware();self.lib=self.fw.lib;self.lib.Host_IoReset()
        self.lib.Host_Send.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_bool]
        self.lib.Host_Send.restype=ctypes.c_bool
        self.lib.Host_SendResult.argtypes=[ctypes.c_int,ctypes.c_bool]
        self.lib.Host_ReplyBusy.restype=ctypes.c_bool
        self.lib.Host_ReplyData.restype=ctypes.POINTER(ctypes.c_uint8)
        self.lib.Host_SaveConfig.argtypes=[ctypes.POINTER(Config)];self.lib.Host_SaveConfig.restype=ctypes.c_bool
        self.lib.Host_ReadConfig.argtypes=[ctypes.POINTER(Config)];self.lib.Host_ReadConfig.restype=ctypes.c_bool
        self.lib.Host_Key.argtypes=[ctypes.c_uint,ctypes.c_bool];self.lib.Host_Key.restype=ctypes.c_bool
        self.lib.Host_MenuKey.argtypes=[ctypes.c_uint];self.lib.Host_MenuKey.restype=ctypes.c_bool
        self.seq=0;self.send(OPEN)

    def send(self,cmd,data=b''):
        self.seq+=1
        return self.fw.request(body(cmd,self.seq,123,data),10)

    def test_delayed_host_read_keeps_buffer_owned_without_waiting(self):
        data=ctypes.create_string_buffer(b'first reply')
        self.assertTrue(self.lib.Host_Send(data,11,True))
        data[0]=b'X'
        self.assertFalse(self.lib.Host_Send(b'second',6,True))
        self.assertEqual(b'first reply',bytes(self.lib.Host_ReplyData()[:11]))
        self.assertTrue(self.lib.Host_ReplyBusy())
        self.lib.Host_ReplyDone()
        self.assertTrue(self.lib.Host_Send(b'second',6,True))
        self.assertEqual(2,self.lib.Host_Submits())

    def test_failed_submit_does_not_latch_busy_and_eager_completion_is_safe(self):
        self.lib.Host_SendResult(-3,False)
        self.assertFalse(self.lib.Host_Send(b'reply',5,True))
        self.assertFalse(self.lib.Host_ReplyBusy())
        self.lib.Host_SendResult(0,True)
        self.assertTrue(self.lib.Host_Send(b'reply',5,True))
        self.assertFalse(self.lib.Host_ReplyBusy())

    def test_no_dtr_or_invalid_size_never_submits(self):
        self.assertFalse(self.lib.Host_Send(b'reply',5,False))
        self.assertFalse(self.lib.Host_Send(b'',0,True))
        self.assertFalse(self.lib.Host_Send(b'x'*129,129,True))
        self.assertEqual(0,self.lib.Host_Submits())

    def test_multi_packet_settings_reply_keeps_owned_bytes_until_completion(self):
        reply=self.send(SETTINGS)
        wire=frame(reply)
        self.assertGreater(len(wire),64)
        self.assertTrue(self.lib.Host_Send(wire,len(wire),True))
        self.assertFalse(self.lib.Host_Send(b'replacement',11,True))
        self.assertEqual(wire,bytes(self.lib.Host_ReplyData()[:len(wire)]))
        decoder=Decoder()
        self.assertEqual([],decoder.feed(wire[:64]))
        self.assertEqual([reply],decoder.feed(wire[64:]))
        self.lib.Host_ReplyDone()

    def test_context_identity_can_be_primed_without_fake_position(self):
        payload=struct.pack('<Hh16s',65535,-32768,b'')+bytes(range(16))
        self.assertEqual(0,self.send(CONTEXT,payload)[5])

    def test_only_exit_ends_session_and_release_is_consumed(self):
        for key in list(range(13))+list(range(14,19)):
            self.assertTrue(self.lib.Host_Key(key,True))
            self.assertTrue(self.lib.Host_Key(key,False))
            self.assertEqual(123,struct.unpack_from('<I',self.send(STATUS),8)[0])
        self.assertTrue(self.lib.Host_Key(13,True))
        self.assertEqual(0,struct.unpack_from('<I',self.send(STATUS),8)[0])
        self.assertTrue(self.lib.Host_Key(13,False))
        self.assertFalse(self.lib.Host_Key(10,True))

    def test_profile_round_trip_and_unchanged_save_do_not_wear_flash(self):
        config=Config(1,3,25,70,4,1,9,4,6)
        self.lib.Host_ProfileKey.argtypes=[ctypes.c_void_p]
        self.lib.Host_ProfileKey(bytes(range(16)))
        self.assertTrue(self.lib.Host_SaveConfig(ctypes.byref(config)))
        decoded=Config()
        self.assertTrue(self.lib.Host_ReadConfig(ctypes.byref(decoded)))
        self.assertEqual(bytes(config),bytes(decoded))
        self.lib.Host_ReadProfileKey.restype=ctypes.POINTER(ctypes.c_uint8)
        self.assertEqual(bytes(range(16)),bytes(self.lib.Host_ReadProfileKey()[:16]))
        self.assertTrue(self.lib.Host_SaveConfig(ctypes.byref(config)))
        self.assertEqual(1,self.lib.Host_ProfileWrites())

    def test_default_squelch_is_off_and_menu_selects_all_levels(self):
        self.lib.Host_DefaultConfig.argtypes=[ctypes.POINTER(Config)]
        self.lib.Host_MenuDraft.argtypes=[ctypes.POINTER(Config)]
        config=Config()
        self.lib.Host_DefaultConfig(ctypes.byref(config))
        self.assertEqual(0,config.squelch)
        self.lib.Host_MenuOpen()
        for _ in range(9):self.lib.Host_MenuKey(12)
        self.lib.Host_MenuKey(10)  # Edit SQL; DOWN wraps 0 to 9.
        for expected in range(9,-1,-1):
            self.lib.Host_MenuKey(12)
            self.lib.Host_MenuDraft(ctypes.byref(config))
            self.assertEqual(expected,config.squelch)
        self.lib.Host_MenuKey(10)
        for _ in range(5):self.lib.Host_MenuKey(12)
        self.assertTrue(self.lib.Host_MenuKey(10))  # Save remains reachable.

    def test_v1_profile_migrates_to_open_squelch_and_v2_keeps_level(self):
        self.lib.Host_ProfileData.restype=ctypes.POINTER(ctypes.c_uint8)
        config=Config(0,3,20,60,4,1,0,4,7)
        self.assertTrue(self.lib.Host_SaveConfig(ctypes.byref(config)))
        record=self.lib.Host_ProfileData()
        self.assertEqual(2,record[4]);self.assertEqual(7,record[29])
        legacy=bytearray(record[:32]);legacy[4]=1;legacy[29]=0
        struct.pack_into('<H',legacy,30,crc(legacy[:30]))
        for index,value in enumerate(legacy):record[index]=value
        decoded=Config()
        self.assertTrue(self.lib.Host_ReadConfig(ctypes.byref(decoded)))
        self.assertEqual(0,decoded.squelch)
        self.assertEqual(bytes(config)[:8],bytes(decoded)[:8])
        self.assertTrue(self.lib.Host_SaveConfig(ctypes.byref(decoded)))
        self.assertEqual(2,record[4])
        config.squelch=10
        self.assertFalse(self.lib.Host_SaveConfig(ctypes.byref(config)))
        record[4]=3  # A future profile must never be overwritten.
        config.squelch=4
        self.assertFalse(self.lib.Host_SaveConfig(ctypes.byref(config)))

    def test_squelch_edit_is_read_only_during_tx_and_exit_discards_draft(self):
        self.lib.Host_MenuDraft.argtypes=[ctypes.POINTER(Config)]
        self.lib.Host_MenuOpen()
        for _ in range(9):self.lib.Host_MenuKey(12)
        self.lib.Host_MenuKey(10);self.lib.Host_MenuKey(11)
        config=Config();self.lib.Host_MenuDraft(ctypes.byref(config))
        self.assertEqual(1,config.squelch)
        self.lib.Host_MenuKey(13);self.lib.Host_MenuOpen()
        self.lib.Host_MenuDraft(ctypes.byref(config));self.assertEqual(0,config.squelch)
        self.lib.Host_MenuKey(13)
        self.send(MODE,b'\0\0\0');self.send(PAIR,struct.pack('<IIBI',435100000,145900000,1,1))
        self.send(PTT,b'\1'+struct.pack('<I',1))
        self.lib.Host_MenuOpen()
        for _ in range(9):self.lib.Host_MenuKey(12)
        self.lib.Host_MenuKey(10);self.lib.Host_MenuKey(11)
        self.lib.Host_MenuDraft(ctypes.byref(config));self.assertEqual(0,config.squelch)
        self.assertEqual(55,len(self.send(SETTINGS))-12)  # Existing Android wire layout unchanged.

    def test_unknown_storage_invalid_values_and_failed_readback_are_rejected(self):
        config=Config(0,3,20,60,4,1,0,4)
        self.lib.Host_UnknownProfile()
        self.assertFalse(self.lib.Host_SaveConfig(ctypes.byref(config)))
        self.assertEqual(0,self.lib.Host_ProfileWrites())
        self.lib.Host_IoReset();config.wpm=99
        self.assertFalse(self.lib.Host_SaveConfig(ctypes.byref(config)))
        config.wpm=20;self.lib.Host_CorruptWrite(True)
        self.assertFalse(self.lib.Host_SaveConfig(ctypes.byref(config)))
        self.assertFalse(self.lib.Host_ReadConfig(ctypes.byref(Config())))

    def test_menu_is_read_only_during_tx_and_exit_returns_without_disconnect(self):
        self.send(MODE,b'\0\0\0');self.send(PAIR,struct.pack('<IIBI',435100000,145900000,1,1))
        self.send(PTT,b'\1'+struct.pack('<I',1))
        self.lib.Host_MenuOpen();self.lib.Host_MenuKey(10);self.lib.Host_MenuKey(11)
        reply=self.send(SETTINGS)
        self.assertEqual(123,struct.unpack_from('<I',reply,8)[0])
        self.assertEqual(1,reply[12+24+14]&1)
        self.lib.Host_MenuKey(13)
        self.assertEqual(123,struct.unpack_from('<I',self.send(STATUS),8)[0])
        self.assertEqual(0,self.lib.Host_ProfileWrites())

    def test_open_menu_blocks_new_transmission(self):
        self.send(MODE,b'\0\0\0');self.send(PAIR,struct.pack('<IIBI',435100000,145900000,1,1))
        self.lib.Host_MenuOpen()
        self.assertEqual(4,self.send(PTT,b'\1'+struct.pack('<I',1))[5])
        self.assertEqual(4,self.lib.Host_LocalPtt(1,10))

    def test_arming_preset_requires_physical_ptt_and_restores_operating_tone(self):
        self.send(MODE,b'\0\0\0');self.send(PAIR,struct.pack('<IIBI',435100000,145900000,1,1))
        self.send(TONE,struct.pack('<H',670))
        self.lib.Host_ArmNext(4)
        self.assertEqual(0,self.lib.Host_Starts())
        self.assertEqual(0,self.lib.Host_LocalPtt(1,10))
        self.assertEqual(744,struct.unpack_from('<H',self.send(SETTINGS),12+24+8)[0])
        self.lib.Host_LocalPtt(0,10)
        self.assertEqual(670,struct.unpack_from('<H',self.send(SETTINGS),12+24+8)[0])

    def test_native_and_host_ctcss_tables_match(self):
        native=Path(ROOT/'App/dcs.c').read_text()
        table=re.search(r'CTCSS_Options\[50\]\s*=\s*\{([^}]+)',native).group(1)
        expected=list(map(int,re.findall(r'\d+',table)))
        self.lib.Host_CtcssValue.argtypes=[ctypes.c_uint8]
        self.assertEqual(expected,[self.lib.Host_CtcssValue(i) for i in range(1,51)])

if __name__=='__main__': unittest.main()
