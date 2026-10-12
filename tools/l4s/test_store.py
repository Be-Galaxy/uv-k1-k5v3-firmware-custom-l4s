import ctypes
import struct
import unittest
from simulator import Firmware
from test_io import Config

class StoreTest(unittest.TestCase):
    def setUp(self):
        self.lib=Firmware().lib;self.lib.Host_IoReset();self.now=0
        self.lib.Host_StoreUpsert.argtypes=[ctypes.c_void_p,ctypes.POINTER(Config),ctypes.c_uint32]
        self.lib.Host_StoreUpsert.restype=ctypes.c_uint8
        self.lib.Host_StoreReadSlot.argtypes=[ctypes.c_uint,ctypes.c_void_p]
        self.lib.Host_StoreCommand.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_bool,ctypes.POINTER(ctypes.c_uint16)]
        self.lib.Host_StoreCommand.restype=ctypes.c_uint8
        self.lib.Host_StoreData.restype=ctypes.POINTER(ctypes.c_uint8)
        self.lib.Host_StoreMutableData.restype=ctypes.POINTER(ctypes.c_uint8)
        self.lib.Host_SatelliteData.restype=ctypes.POINTER(ctypes.c_uint8)
        self.step=ctypes.c_uint16(10)

    def command(self,data):return self.lib.Host_StoreCommand(data,len(data),True,ctypes.byref(self.step))
    def tick(self):
        self.now+=10;self.lib.Host_StoreTick(self.now)
    def pump(self,phase=9):
        for _ in range(150):
            if self.lib.Host_StorePhase()==phase and (phase!=4 or self.lib.Host_StoreUploadReady()):return
            self.tick()
        self.fail(f'store did not reach {phase}: {self.lib.Host_StorePhase()} error={self.lib.Host_StoreError()}')
    def slot(self,index=0):
        out=ctypes.create_string_buffer(64);self.lib.Host_StoreReadSlot(index,out);return out.raw
    def config(self,rx=435100000):return Config(0,3,20,60,4,1,0,4,0,rx,145900000,10,-20)
    def save(self,key=bytes(range(16)),rx=435100000,txid=1):
        self.assertEqual(1,self.lib.Host_StoreUpsert(key,ctypes.byref(self.config(rx)),txid));self.pump()

    def test_commit_and_unchanged_save_preserve_legacy_sector(self):
        before=bytes(self.lib.Host_SatelliteData()[:3584])
        self.save();self.assertEqual(1,self.lib.Host_StoreGeneration())
        self.assertEqual(435100000,struct.unpack_from('<I',self.slot(),30)[0])
        writes=self.lib.Host_StoreWrites();erases=self.lib.Host_StoreErases()
        self.assertEqual(0,self.lib.Host_StoreUpsert(bytes(range(16)),ctypes.byref(self.config()),2))
        self.assertEqual(writes,self.lib.Host_StoreWrites());self.assertEqual(erases,self.lib.Host_StoreErases())
        self.assertEqual(before,bytes(self.lib.Host_SatelliteData()[:3584]))
        self.lib.Host_StoreRestart();self.assertEqual(1,self.lib.Host_StoreGeneration())

    def test_power_loss_after_each_transaction_tick_recovers_old_or_complete_new(self):
        for interruption in range(50):
            self.lib.Host_IoReset();self.now=0;self.save()
            self.assertEqual(1,self.lib.Host_StoreUpsert(bytes(range(16)),ctypes.byref(self.config(435200000)),2))
            for _ in range(interruption):self.tick()
            self.lib.Host_StoreRestart()
            actual=struct.unpack_from('<I',self.slot(),30)[0]
            self.assertIn(actual,(435100000,435200000),f'interruption {interruption}')
            self.assertEqual(0,self.lib.Host_StoreError())

    def test_partial_program_or_erase_never_loads_half_record(self):
        for partial in (0,1,4,16,31,64,127):
            self.lib.Host_IoReset();self.now=0;self.save()
            self.lib.Host_StoreCut(partial)
            self.assertEqual(1,self.lib.Host_StoreUpsert(bytes(range(16)),ctypes.byref(self.config(435200000)),2))
            for _ in range(70):self.tick()
            self.lib.Host_StoreRestart()
            self.assertIn(struct.unpack_from('<I',self.slot(),30)[0],(435100000,435200000))

    def test_delete_needs_local_confirmation_and_cancel_preserves_data(self):
        self.save();key=bytes(range(16))
        self.assertEqual(1,self.command(b'\5'+struct.pack('<II',1,2)+key+b'\0'));self.pump(6)
        self.assertNotEqual(b'\xff'*64,self.slot())
        self.lib.Host_StoreCancel();self.tick();self.lib.Host_StoreRestart()
        self.assertNotEqual(b'\xff'*64,self.slot())
        self.assertEqual(1,self.command(b'\5'+struct.pack('<II',1,3)+key+b'\0'));self.pump(6)
        self.lib.Host_StoreConfirm();self.pump()
        self.assertEqual(b'\xff'*64,self.slot())
        self.lib.Host_StoreRestart();self.assertEqual(2,self.lib.Host_StoreGeneration())

    def test_unknown_bank_is_not_erased_and_generation_is_checked(self):
        data=self.lib.Host_StoreMutableData();data[0]=0x55;self.lib.Host_StoreRestart()
        self.assertEqual(12,self.lib.Host_StoreUpsert(bytes(range(16)),ctypes.byref(self.config()),1))
        self.assertEqual(0,self.lib.Host_StoreErases());self.assertEqual(0x55,data[0])
        self.lib.Host_IoReset();self.save()
        self.assertEqual(9,self.command(b'\2'+struct.pack('<II',0,2)))

    def test_import_preserves_existing_entries_and_requires_physical_confirmation(self):
        self.save();original=self.slot()
        self.assertEqual(1,self.command(b'\2'+struct.pack('<II',1,2)));self.pump(4)
        self.assertEqual(5,self.command(b'\3'+struct.pack('<IB',2,0)+b'\xff'*64))
        for index in range(56):
            self.pump(4)
            self.assertEqual(0,self.command(b'\3'+struct.pack('<IB',2,index)+(original if index==0 else b'\xff'*64)))
        self.pump(4);self.assertEqual(1,self.command(b'\4'+struct.pack('<I',2)));self.pump(6)
        self.assertEqual(1,self.lib.Host_StoreGeneration())
        self.lib.Host_StoreConfirm();self.pump();self.assertEqual(2,self.lib.Host_StoreGeneration())
        self.assertEqual(original,self.slot())

    def test_power_loss_after_each_tick_with_both_banks_previously_committed(self):
        for interruption in range(50):
            self.lib.Host_IoReset();self.now=0
            self.save(rx=435100000,txid=1);self.save(rx=435200000,txid=2)
            self.assertEqual(1,self.lib.Host_StoreUpsert(bytes(range(16)),ctypes.byref(self.config(435300000)),3))
            for _ in range(interruption):self.tick()
            self.lib.Host_StoreRestart()
            self.assertIn(struct.unpack_from('<I',self.slot(),30)[0],(435200000,435300000))

    def test_corrupt_new_bank_falls_back_and_opaque_legacy_slots_are_not_dropped(self):
        legacy=self.lib.Host_SatelliteData()
        legacy[64]=0x55
        before=bytes(legacy[:3584])
        self.lib.Host_StoreRestart();self.save(rx=435100000,txid=1)
        self.assertEqual(0x55,self.slot(1)[0]);self.assertEqual(before,bytes(legacy[:3584]))
        self.save(rx=435200000,txid=2)
        data=self.lib.Host_StoreMutableData();data[4096+256+7]^=1
        self.lib.Host_StoreRestart()
        self.assertEqual(435100000,struct.unpack_from('<I',self.slot(),30)[0])
        self.assertEqual(0x55,self.slot(1)[0])

if __name__=='__main__':unittest.main()
