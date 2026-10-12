import struct
import unittest
from protocol import *
from simulator import Firmware

class ProtocolTest(unittest.TestCase):
    def test_physical_ptt_keeps_session_and_holds_both_frequencies(self):
        self.ready()
        self.assertEqual(0,self.fw.lib.Host_LocalPtt(1,self.now))
        reply=self.send(STATUS)
        self.assertEqual(self.session,struct.unpack_from('<I',reply,8)[0])
        self.assertEqual(3,reply[31])
        self.assertEqual(4,self.pair(2,rx=435110000)[5])
        self.assertEqual(0,self.fw.lib.Host_LocalPtt(0,self.now))
        self.assertEqual(self.session,struct.unpack_from('<I',self.send(KEEPALIVE),8)[0])
        self.assertEqual(0,self.pair(2,rx=435110000)[5])

    def test_local_ptt_requires_ready_pair_and_obeys_tx_rejection(self):
        self.assertEqual(10,self.fw.lib.Host_LocalPtt(1,self.now))
        self.ready();self.fw.lib.Host_Deny(1)
        self.assertEqual(7,self.fw.lib.Host_LocalPtt(1,self.now))
        self.assertEqual(0,self.fw.lib.Host_Starts())

    def test_local_press_cancels_remote_cw_without_rekey_or_disconnect(self):
        self.ready(cw=True)
        self.send(CW,b'\24\4TEST')
        starts=self.fw.lib.Host_Starts()
        self.assertEqual(8,self.fw.lib.Host_LocalPtt(1,self.now))
        self.assertEqual(starts,self.fw.lib.Host_Starts())
        status=self.send(STATUS)
        self.assertEqual(self.session,struct.unpack_from('<I',status,8)[0])
        self.assertEqual(0,status[31])

    def test_watchdog_still_stops_local_ptt(self):
        self.ready();self.fw.lib.Host_LocalPtt(1,self.now)
        self.fw.lib.Host_Tick(self.now+1500)
        reply=self.send(STATUS)
        self.assertEqual(0,struct.unpack_from('<I',reply,8)[0])
        self.assertEqual(0,reply[31])

    def test_expired_session_cannot_start_physical_ptt_before_next_poll(self):
        self.ready()
        self.assertEqual(3,self.fw.lib.Host_LocalPtt(1,self.now+1500))
        self.assertEqual(0,self.fw.lib.Host_Starts())
        self.assertEqual(1,self.fw.lib.Host_Leaves())

    def test_heartbeat_deadline_handles_millisecond_wrap(self):
        self.now=0xfffffff0
        self.fw.lib.Host_Reset()
        self.send(OPEN)
        self.ready()
        self.fw.lib.Host_Tick((self.now+1499)&0xffffffff)
        self.assertEqual(0,self.fw.lib.Host_Leaves())
        self.fw.lib.Host_Tick((self.now+1500)&0xffffffff)
        self.assertEqual(1,self.fw.lib.Host_Leaves())

    def setUp(self):
        self.fw = Firmware()
        self.seq = 1
        self.now = 10
        self.session = 0x12345678
        self.send(OPEN)

    def send(self, cmd, data=b"", session=None, sequence=None):
        seq = self.seq if sequence is None else sequence
        request = body(cmd, seq, self.session if session is None else session, data)
        reply = self.fw.request(request, self.now)
        self.seq += 1
        self.assertEqual(seq, struct.unpack_from("<H", reply, 6)[0])
        return reply

    def pair(self, revision=1, rx=435100000, tx=145900000):
        return self.send(PAIR, struct.pack("<IIBI", rx, tx, 1, revision))

    def ready(self, cw=False):
        self.assertEqual(0, self.pair()[5])
        self.assertEqual(0, self.send(MODE, bytes([3 if cw else 0, 3 if cw else 0, 0]))[5])

    def test_incremental_and_concatenated_frames(self):
        expected = body(HELLO, 7, 0)
        wire = frame(expected)
        self.assertEqual(wire, self.fw.encode(expected))
        decoder = Decoder()
        received = []
        for byte in b"noise" + wire + wire:
            received += decoder.feed(bytes([byte]))
        self.assertEqual([expected, expected], received)

    def test_known_crc_and_production_reply_codec(self):
        self.assertEqual(0x31c3, crc(b'123456789'))
        reply = self.send(HELLO, session=0)
        self.assertEqual(frame(reply), self.fw.encode(reply))
        self.assertEqual([reply], Decoder().feed(self.fw.encode(reply)))

    def test_bad_crc_and_length_resynchronize(self):
        good = frame(body(HELLO, 1, 0))
        corrupt = bytearray(good); corrupt[9] ^= 1
        decoder = Decoder()
        self.assertEqual([body(HELLO, 1, 0)], decoder.feed(b"\xab\xcd\xff\xff" + corrupt + good))

    def test_length_mismatch_never_reaches_commands(self):
        request = bytearray(body(PTT, 2, self.session, bytes([1])+struct.pack('<I', 1)))
        request[2] = 0
        self.assertEqual(b'', self.fw.request(bytes(request), self.now))
        self.assertEqual(0, self.fw.lib.Host_Starts())

    def test_protocol_identification(self):
        reply = self.send(HELLO, session=0)
        self.assertEqual(b"L4S1", reply[12:16])
        self.assertEqual(10, struct.unpack_from('<H', reply, 21)[0])

    def test_pair_rounds_to_10_hz_and_is_atomic(self):
        status = Snapshot.parse(self.pair(rx=435100006, tx=145900004))
        self.assertEqual((435100010, 145900000), (status.rx_hz, status.tx_hz))
        self.assertEqual(5, self.pair(2, tx=1000000)[5])
        self.assertEqual(status, Snapshot.parse(self.send(STATUS)))

    def test_ptt_requires_ready_frequency_revision(self):
        self.assertEqual(10, self.send(PTT, b'\1'+struct.pack('<I', 1))[5])
        self.ready()
        self.assertEqual(10, self.send(PTT, b'\1'+struct.pack('<I', 99))[5])
        self.assertEqual(0, self.send(PTT, b'\1'+struct.pack('<I', 1))[5])

    def test_unchanged_quantized_pair_does_not_restart_receive_but_stop_reapplies(self):
        self.ready()
        writes=self.fw.lib.Host_Writes()
        reply=self.pair(2,rx=435100004,tx=145900004)
        self.assertEqual(0,reply[5])
        self.assertEqual(2,Snapshot.parse(reply).revision)
        self.assertEqual(writes,self.fw.lib.Host_Writes())
        self.send(STOP)
        self.assertEqual(0,self.pair(3)[5])
        self.assertEqual(writes+1,self.fw.lib.Host_Writes())
        self.assertEqual(0,self.send(PTT,b'\1'+struct.pack('<I',3))[5])

    def test_duplicate_ptt_and_cw_are_not_replayed(self):
        self.ready()
        seq = self.seq
        data = b'\1'+struct.pack('<I', 1)
        self.send(PTT, data)
        self.send(PTT, data, sequence=seq)
        self.assertEqual(1, self.fw.lib.Host_Starts())
        self.send(STOP)
        self.assertEqual(9, self.send(PTT, data, sequence=seq)[5])

    def test_cw_duplicate_and_completion(self):
        self.ready(cw=True)
        seq = self.seq
        data = bytes([20, 2])+b'CQ'
        self.assertEqual(1, self.send(CW, data)[5])
        self.send(CW, data, sequence=seq)
        self.assertEqual(1, self.fw.lib.Host_Starts())
        self.now += 300
        self.assertEqual(3, Snapshot.parse(self.send(KEEPALIVE)).cw_state)

    def test_heartbeat_timeout_stops_and_restores(self):
        self.ready()
        self.send(PTT, b'\1'+struct.pack('<I', 1))
        self.fw.lib.Host_Tick(self.now+1500)
        reply = self.send(STATUS)
        self.assertEqual(3, reply[5])
        self.assertEqual(0, Snapshot.parse(reply).tx_state)
        self.assertEqual(1, self.fw.lib.Host_Leaves())

    def test_replay_does_not_refresh_watchdog(self):
        seq = self.seq
        self.send(KEEPALIVE)
        self.now += 1000
        self.send(KEEPALIVE, sequence=seq)
        self.now += 501
        self.assertEqual(3, self.send(STATUS)[5])

    def test_transmit_freezes_both_frequencies_and_rejects_updates(self):
        self.ready()
        self.send(PTT, b'\1'+struct.pack('<I', 1))
        self.assertEqual(4, self.pair(2, tx=145900010)[5])
        reply = self.pair(3, tx=145900020)
        self.assertEqual(4, reply[5])
        self.assertEqual((1, 0), (Snapshot.parse(reply).revision, Snapshot.parse(reply).pending_revision))
        self.assertEqual((435100000,145900000), (Snapshot.parse(reply).rx_hz,Snapshot.parse(reply).tx_hz))
        self.send(STOP)
        self.assertEqual(0, Snapshot.parse(self.send(STATUS)).pending_revision)
        self.assertEqual(1, self.fw.lib.Host_Writes())

    def test_cw_operation_freezes_pair_until_message_is_done(self):
        self.ready(cw=True)
        self.send(CW, bytes([20, 2])+b'CQ')
        self.assertEqual(4, self.pair(2,rx=435100010,tx=145900010)[5])
        self.now += 300
        self.send(KEEPALIVE)
        self.assertEqual(4, self.pair(3,rx=435100020,tx=145900020)[5])
        self.send(STOP)
        self.assertEqual(0, self.pair(3,rx=435100020,tx=145900020)[5])

    def test_old_session_cannot_key_and_new_open_cannot_steal(self):
        self.ready()
        self.assertEqual(3, self.send(PTT, b'\1'+struct.pack('<I', 1), session=99)[5])
        self.assertEqual(4, self.send(OPEN, session=99)[5])
        self.assertEqual(0, self.fw.lib.Host_Starts())

    def test_local_stop_and_new_session_require_reconfiguration(self):
        self.ready(cw=True)
        self.send(CW, bytes([20, 2])+b'CQ')
        self.fw.lib.Host_LocalStop()
        self.session += 1
        self.assertEqual(0, Snapshot.parse(self.send(OPEN)).tx_state)
        self.assertEqual(10, self.send(CW, bytes([20, 2])+b'CQ')[5])

    def test_tx_denial_and_unsupported_mode(self):
        self.ready()
        self.fw.lib.Host_Deny(1)
        self.assertEqual(7, self.send(PTT, b'\1'+struct.pack('<I', 1))[5])
        self.assertEqual(6, self.send(MODE, b'\2\2\0')[5])

    def test_close_stops_without_replaying_on_reconnect(self):
        self.ready()
        self.send(PTT, b'\1'+struct.pack('<I', 1))
        reply = self.send(CLOSE)
        self.assertEqual(0, struct.unpack_from('<I', reply, 8)[0])
        self.assertEqual(0, Snapshot.parse(reply).tx_state)
        self.assertEqual(3, self.send(OPEN)[5])
        self.session += 1
        self.send(OPEN)
        self.assertEqual(10, self.send(PTT, b'\1'+struct.pack('<I', 1))[5])

    def test_stop_rejects_a_delayed_newer_ptt_without_rearming(self):
        self.ready()
        self.send(STOP)
        self.assertEqual(10, self.send(PTT, b'\1'+struct.pack('<I', 1))[5])
        self.assertEqual(0, self.fw.lib.Host_Starts())

if __name__ == '__main__':
    unittest.main()
