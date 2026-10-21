import ctypes
import struct
import unittest

from protocol import body, OPEN, CLOSE, DIAGNOSTICS, MODE, PAIR, CW, STOP, CONTEXT
from simulator import Firmware


class RssiTest(unittest.TestCase):
    def setUp(self):
        self.fw = Firmware()
        self.sequence = 0
        self.fw.lib.Host_SampleRssi.argtypes = [ctypes.c_uint32, ctypes.c_uint16, ctypes.c_int16]
        self.fw.lib.Host_SampleRssi.restype = ctypes.c_bool
        self.send(OPEN)

    def send(self, command, data=b'', session=123):
        self.sequence += 1
        return self.fw.request(body(command, self.sequence, session, data), 10)

    def dbm(self):
        reply = self.send(DIAGNOSTICS, b'\2')[12:]
        self.assertEqual(37, len(reply))
        self.assertEqual(2, reply[0])
        return struct.unpack_from('<h', reply, 3)[0]

    def test_half_db_conversion_and_band_correction_keep_full_signed_range(self):
        self.assertEqual(-32768, self.dbm())
        for now, raw, correction, expected in (
            (10, 100, -6, -116), (110, 0, -15, -175),
            (210, 101, -6, -116), (310, 200, 12, -48),
            (410, 320, 0, 0), (510, 511, 0, 95),
        ):
            self.assertTrue(self.fw.lib.Host_SampleRssi(now, raw, correction))
            self.assertEqual(expected, self.dbm())

    def test_legacy_reply_keeps_its_layout_and_calibrated_bar_levels(self):
        for index, (raw, level) in enumerate(((99, 0), (100, 1), (150, 2), (200, 4), (250, 6))):
            self.fw.lib.Host_SampleRssi(index * 100 + 10, raw, 0)
            reply = self.send(DIAGNOSTICS)[12:]
            self.assertEqual(36, len(reply))
            self.assertEqual((1, level), (reply[0], reply[3]))
            self.assertGreater(struct.unpack_from('<I', reply, 4)[0], 0)
        self.assertEqual(2, self.send(DIAGNOSTICS, b'\3')[5])

    def test_sampling_is_rate_limited_and_resumes_after_physical_ptt(self):
        self.assertTrue(self.fw.lib.Host_SampleRssi(10, 100, -6))
        self.assertFalse(self.fw.lib.Host_SampleRssi(109, 250, 0))
        self.assertEqual(-116, self.dbm())
        self.send(MODE, b'\0\0\0')
        self.send(PAIR, struct.pack('<IIBI', 435100000, 145900000, 1, 1))
        self.assertEqual(0, self.fw.lib.Host_LocalPtt(1, 10))
        self.assertFalse(self.fw.lib.Host_SampleRssi(110, 250, 0))
        self.assertEqual(-116, self.dbm())
        self.fw.lib.Host_LocalPtt(0, 10)
        self.assertTrue(self.fw.lib.Host_SampleRssi(110, 250, 0))
        self.assertEqual(-35, self.dbm())

    def test_entire_cw_window_holds_the_last_receive_reading(self):
        self.fw.lib.Host_SampleRssi(10, 100, -6)
        self.send(MODE, b'\3\3\0')
        self.send(PAIR, struct.pack('<IIBI', 435100000, 145900000, 1, 1))
        self.send(CW, b'\24\2CQ')
        self.assertFalse(self.fw.lib.Host_SampleRssi(110, 300, 0))
        self.fw.lib.Host_Tick(300)
        self.assertFalse(self.fw.lib.Host_SampleRssi(310, 300, 0))
        self.assertEqual(-116, self.dbm())
        self.send(STOP)
        self.assertTrue(self.fw.lib.Host_SampleRssi(310, 300, 0))

    def test_sampling_requires_a_session_and_handles_timer_wrap(self):
        self.assertTrue(self.fw.lib.Host_SampleRssi(0xfffffff0, 100, 0))
        self.assertFalse(self.fw.lib.Host_SampleRssi(83, 200, 0))
        self.assertTrue(self.fw.lib.Host_SampleRssi(84, 200, 0))
        self.send(CLOSE)
        self.assertFalse(self.fw.lib.Host_SampleRssi(200, 300, 0))
        self.send(OPEN, session=456)
        reply = self.send(DIAGNOSTICS, b'\2', session=456)[12:]
        self.assertEqual(-32768, struct.unpack_from('<h', reply, 3)[0])

    def test_handset_shows_dbm_without_overflow(self):
        self.fw.lib.Host_Render.argtypes = [ctypes.c_void_p]
        self.fw.lib.Host_DisplayText.argtypes = [ctypes.c_uint]
        self.fw.lib.Host_DisplayText.restype = ctypes.c_char_p
        self.send(CONTEXT, struct.pack('<Hh16s', 3599, -900, b'LONG SATELLITE'))
        for now, raw, correction, expected in ((0, None, 0, b'---dBm'), (10, 0, -15, b'-175dBm')):
            if raw is not None:
                self.fw.lib.Host_SampleRssi(now, raw, correction)
            frame = ctypes.create_string_buffer(896)
            self.assertEqual(0, self.fw.lib.Host_Render(frame))
            self.assertIn(expected, self.fw.lib.Host_DisplayText(1))


if __name__ == '__main__':
    unittest.main()
