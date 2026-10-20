import ctypes
import struct
import unittest
from protocol import *
from simulator import Firmware
from test_io import Config


class Alpha8Test(unittest.TestCase):
    def setUp(self):
        self.fw=Firmware();self.lib=self.fw.lib;self.lib.Host_IoReset()
        self.seq=0;self.now=10;self.session=123;self.send(OPEN)
        self.lib.Host_MacrosData.restype=ctypes.POINTER(ctypes.c_uint8)
        self.lib.Host_LocalCw.argtypes=[ctypes.c_void_p,ctypes.c_uint8,ctypes.c_uint32]
        self.lib.Host_LoadSatellite.argtypes=[ctypes.c_void_p,ctypes.POINTER(Config)]
        self.lib.Host_LoadSatellite.restype=ctypes.c_bool
        self.lib.Host_SaveSatellite.argtypes=[ctypes.c_void_p,ctypes.POINTER(Config)]
        self.lib.Host_SatelliteData.restype=ctypes.POINTER(ctypes.c_uint8)

    def send(self,cmd,data=b''):
        self.seq+=1;self.now+=1
        return self.fw.request(body(cmd,self.seq,self.session,data),self.now)

    def select(self):
        return self.send(PROFILE,b'\0'+bytes(range(16))+bytes(range(16))+b'SAT'.ljust(16,b'\0'))

    def ready(self,cw=False):
        self.send(MODE,bytes([3 if cw else 0,3 if cw else 0,0]))
        return self.send(PAIR,struct.pack('<IIBI',435100000,145900000,1,1))

    def stage(self,text=b'CQ',transaction=1):
        generation=self.lib.Host_MacrosGeneration()
        self.assertEqual(1,self.send(MACROS,b'\1'+struct.pack('<II',generation,transaction))[5])
        for i in range(8):
            value=text+str(i+1).encode()
            self.assertEqual(0,self.send(MACROS,bytes([2,i,len(value)])+value)[5])
        self.assertEqual(1,self.send(MACROS,b'\3')[5])

    def pump(self,phase=8):
        for _ in range(80):
            if self.lib.Host_MacrosPhase()==phase:return
            self.now+=10;self.lib.Host_MacrosTick(self.now,self.session)
        self.fail(f'macro phase {self.lib.Host_MacrosPhase()}, wanted {phase}')

    def slot(self,index=0):
        data=self.send(MACROS,bytes([0,index]))[12:]
        return data[10:10+data[9]]

    def save(self,text=b'CQ',transaction=1):
        self.stage(text,transaction);self.lib.Host_MacrosConfirm();self.pump()

    def test_macro_batch_waits_for_local_confirmation_and_restores_all_eight(self):
        before=bytes(self.lib.Host_SatelliteData()[:3584])
        self.stage()
        self.assertEqual(0,self.lib.Host_MacrosErases());self.assertEqual(0,self.lib.Host_MacrosWrites())
        self.assertEqual(2,self.lib.Host_MacrosPhase())
        self.lib.Host_MacrosConfirm();self.pump();self.lib.Host_MacrosRestart()
        self.assertEqual(1,self.lib.Host_MacrosGeneration())
        for i in range(8):self.assertEqual(b'CQ'+str(i+1).encode(),self.slot(i))
        self.assertEqual(before,bytes(self.lib.Host_SatelliteData()[:3584]))

    def test_power_loss_after_every_write_step_keeps_complete_old_or_new_batch(self):
        for interruption in range(22):
            self.lib.Host_IoReset();self.save(b'OLD')
            self.stage(b'NEW',2);self.lib.Host_MacrosConfirm()
            for _ in range(interruption):self.now+=10;self.lib.Host_MacrosTick(self.now,self.session)
            self.lib.Host_MacrosRestart()
            values=[self.slot(i) for i in range(8)]
            self.assertIn(values,[[b'OLD'+str(i+1).encode() for i in range(8)],
                                  [b'NEW'+str(i+1).encode() for i in range(8)]],interruption)

    def test_partial_program_cancel_disconnect_and_unknown_flash_are_safe(self):
        self.save(b'OLD');self.stage(b'NEW',2);self.lib.Host_MacrosCut(7)
        self.lib.Host_MacrosConfirm();self.pump(10);self.lib.Host_MacrosCut(-1);self.lib.Host_MacrosRestart()
        self.assertEqual(b'OLD1',self.slot())
        self.stage(b'NEW',3);self.send(MACROS,b'\4');self.now+=10;self.lib.Host_MacrosTick(self.now,self.session)
        self.assertEqual(9,self.lib.Host_MacrosPhase());self.assertEqual(b'OLD1',self.slot())
        self.stage(b'NEW',4);self.lib.Host_MacrosTick(self.now+10,0)
        self.assertEqual(9,self.lib.Host_MacrosPhase());self.lib.Host_MacrosRestart()
        self.assertEqual(b'OLD1',self.slot())
        self.lib.Host_IoReset();self.lib.Host_MacrosData()[3000]=17;self.lib.Host_MacrosRestart()
        before=bytes(self.lib.Host_MacrosData()[:8192])
        self.assertEqual(12,self.send(MACROS,b'\1'+struct.pack('<II',0,9))[5])
        self.assertEqual(before,bytes(self.lib.Host_MacrosData()[:8192]))

    def test_corrupted_saved_macro_length_or_character_never_overruns_reply(self):
        self.save()
        memory=self.lib.Host_MacrosData()
        memory[256]=255
        self.assertEqual(b'',self.slot())
        memory[256]=3;memory[257]=ord('!')
        self.assertEqual(b'',self.slot())

    def test_profile_switch_clears_old_pass_countdown(self):
        self.send(PASS_INFO,b'\1'+struct.pack('<I',65));self.select()
        self.ready();frame=ctypes.create_string_buffer(896);self.lib.Host_Render.argtypes=[ctypes.c_void_p]
        self.lib.Host_DisplayText.restype=ctypes.c_char_p
        self.lib.Host_Render(frame)
        self.assertNotIn(b'A01:05',self.lib.Host_DisplayText(6))

    def test_macro_limits_and_stale_generation_reject_without_partial_commit(self):
        self.assertEqual(9,self.send(MACROS,b'\1'+struct.pack('<II',7,1))[5])
        self.assertEqual(1,self.send(MACROS,b'\1'+struct.pack('<II',0,1))[5])
        self.assertEqual(2,self.send(MACROS,b'\2\10\1A')[5])
        self.assertEqual(5,self.send(MACROS,b'\2\0\1!')[5])
        self.assertEqual(0,self.send(MACROS,b'\2\0\134'+b'A'*92)[5])
        self.assertEqual(10,self.send(MACROS,b'\3')[5])
        self.assertEqual(0,self.lib.Host_MacrosWrites())

    def test_cw_options_are_ram_only_busy_while_tx_and_persist_in_v2_profiles(self):
        self.select();options=b'\1'+bytes([25,70,4,45,3,12])
        self.assertEqual(0,self.send(CW_OPTIONS,options)[5])
        self.assertEqual(bytes([1,25,70,4,45,3,12,1]),self.send(CW_OPTIONS,b'\0')[12:])
        self.assertEqual(0,self.lib.Host_ProfileWrites());self.assertEqual(55,len(self.send(SETTINGS))-12)
        self.assertEqual(79,len(self.send(PROFILE,b'\2'))-12)
        self.ready();self.send(PTT,b'\1'+struct.pack('<I',1))
        self.assertEqual(4,self.send(CW_OPTIONS,options)[5])
        self.assertEqual(4,self.send(MACROS,b'\1'+struct.pack('<II',0,1))[5])
        self.send(STOP)
        config=Config(1,3,25,70,4,1,0,0,0,435100000,145900000,10,-20,45,3,12)
        self.assertEqual(0,self.lib.Host_SaveSatellite(bytes(range(16)),ctypes.byref(config)))
        decoded=Config();self.assertTrue(self.lib.Host_LoadSatellite(bytes(range(16)),ctypes.byref(decoded)))
        self.assertEqual((45,3,12),(decoded.cw_hang,decoded.cw_keyer,decoded.cw_spacing))
        self.lib.Host_SatelliteData()[62]^=1
        self.assertFalse(self.lib.Host_LoadSatellite(bytes(range(16)),ctypes.byref(decoded)))

    def test_local_macro_freezes_both_frequencies_and_returns_to_receive_on_completion(self):
        self.select();self.ready(cw=True)
        self.assertEqual(1,self.lib.Host_LocalCw(b'CQ',2,self.now))
        self.assertEqual(4,self.send(PAIR,struct.pack('<IIBI',435200000,145950000,1,2))[5])
        self.now+=200;status=self.send(KEEPALIVE)
        self.assertEqual(0,status[31]);self.assertEqual(3,status[32])
        self.assertEqual(0,self.send(PAIR,struct.pack('<IIBI',435200000,145950000,1,2))[5])

    def test_countdown_diagnostics_and_shortcuts_do_not_retune_or_transmit(self):
        self.ready();writes=self.lib.Host_Writes();starts=self.lib.Host_Starts()
        self.assertEqual(0,self.send(PASS_INFO,b'\1'+struct.pack('<I',65))[5])
        self.now+=1100;self.send(KEEPALIVE)
        frame=ctypes.create_string_buffer(896);self.lib.Host_Render.argtypes=[ctypes.c_void_p]
        self.lib.Host_DisplayText.restype=ctypes.c_char_p
        self.assertEqual(0,self.lib.Host_Render(frame));self.assertIn(b'A01:04',self.lib.Host_DisplayText(6))
        self.send(MODE,b'\7\7\0');diag=self.send(DIAGNOSTICS)[12:]
        self.assertEqual(36,len(diag));self.assertGreater(struct.unpack_from('<I',diag,4)[0],0)
        self.assertEqual(6,diag[2])
        self.send(KEEPALIVE);self.assertEqual(6,self.send(DIAGNOSTICS)[14])
        self.assertGreater(struct.unpack_from('<I',diag,8)[0],0)
        self.assertEqual([10,11,12,13,1,9],[self.lib.Host_Shortcut(k) for k in (11,12,14,15,9,0)])
        self.assertEqual(writes,self.lib.Host_Writes());self.assertEqual(starts,self.lib.Host_Starts())


if __name__=='__main__':unittest.main()
