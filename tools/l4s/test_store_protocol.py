import struct
import unittest
from simulator import Firmware
from protocol import body, OPEN, CLOSE, HELLO, MODE, PAIR, PTT, STATUS, PROFILE, PROFILE_STORE

class StoreProtocolTest(unittest.TestCase):
    def setUp(self):
        self.fw=Firmware();self.fw.lib.Host_IoReset();self.sequence=0;self.now=0
        self.assertEqual(0,self.send(OPEN)[5])
    def send(self,command,data=b'',session=123):
        self.sequence+=1;self.now+=10
        return self.fw.request(body(command,self.sequence,session,data),self.now)
    def test_store_capability_and_wire_shapes_leave_profile_reply_unchanged(self):
        hello=self.send(HELLO)
        self.assertTrue(struct.unpack_from('<I',hello,12+5)[0]&512)
        self.assertEqual(79,len(self.send(PROFILE,b'\2'))-12)
        self.assertEqual(23,len(self.send(PROFILE_STORE,b'\0'))-12)
        reply=self.send(PROFILE_STORE,b'\1\0')
        self.assertEqual(88,len(reply)-12);self.assertEqual(0,reply[12+23])
        self.assertEqual(b'\xff'*64,reply[12+24:])
        self.assertEqual(6,self.send(PROFILE_STORE,b'\10')[5])  # No remote COMMIT operation.
    def test_edit_authorization_requires_fresh_pair_and_is_rejected_during_tx(self):
        self.send(MODE,b'\0\0\0');self.send(PAIR,struct.pack('<IIBI',435100000,145900000,1,1))
        self.assertEqual(0,self.send(PROFILE_STORE,b'\7'+struct.pack('<H',50))[5])
        self.assertEqual(10,self.send(PTT,b'\1'+struct.pack('<I',1))[5])
        self.assertEqual(0,self.fw.lib.Host_Starts())
        self.send(PAIR,struct.pack('<IIBI',435100000,145900000,1,2))
        self.assertEqual(0,self.send(PTT,b'\1'+struct.pack('<I',2))[5])
        self.assertEqual(4,self.send(PROFILE_STORE,b'\7'+struct.pack('<H',10))[5])
        reply=self.send(PROFILE_STORE,b'\0')
        self.assertEqual(50,struct.unpack_from('<H',reply,12+17)[0])
    def test_old_session_cannot_mutate_and_close_cancels_uncommitted_import(self):
        request=b'\2'+struct.pack('<II',0,7)
        self.assertEqual(1,self.send(PROFILE_STORE,request)[5])
        self.assertEqual(3,self.send(PROFILE_STORE,b'\4'+struct.pack('<I',7),session=456)[5])
        self.assertEqual(0,self.send(CLOSE)[5])
        for _ in range(5):self.send(STATUS)
        self.assertEqual(10,self.fw.lib.Host_StorePhase())
        self.assertEqual(0,self.fw.lib.Host_StoreGeneration())
        self.assertEqual(0,self.send(OPEN,session=456)[5])
        self.assertEqual(10,self.send(PTT,b'\1'+struct.pack('<I',1),session=456)[5])

if __name__=='__main__':unittest.main()
