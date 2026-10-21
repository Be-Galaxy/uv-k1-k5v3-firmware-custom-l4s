import ctypes
from pathlib import Path
import unittest

from simulator import Firmware


class UpstreamCompatibilityTest(unittest.TestCase):
    def test_l4s_and_native_meter_share_conversion_corrections_and_display_ceiling(self):
        root = Path(__file__).resolve().parents[2]
        native = (root / 'App/ui/main.c').read_text(encoding='utf-8')
        control = (root / 'App/app/l4s_control.c').read_text(encoding='utf-8')
        radio = (root / 'App/radio.c').read_text(encoding='utf-8')
        self.assertIn('BK4819_GetRSSI_dBm() + RADIO_GetRssiCorrection()', native)
        self.assertIn('RADIO_GetRssiCorrection()', control)
        self.assertNotIn('dBmCorrTable', control)
        correction = radio.split('int16_t RADIO_GetRssiCorrection(void)', 1)[1].split('return correction;', 1)[0]
        self.assertIn('dBmCorrTable[gRxVfo->Band]', correction)
        self.assertIn('gSetting_AM_fix && gRxVfo->Modulation == MODULATION_AM', correction)
        self.assertIn('AM_fix_get_gain_diff()', correction)
        for driver in ('bk4819.c', 'bk4829.c'):
            source = (root / 'App/driver' / driver).read_text(encoding='utf-8')
            self.assertIn('return RSSI_RawToDbm(rssi);', source)
        l4s = (root / 'App/app/l4s_rssi.c').read_text(encoding='utf-8')
        self.assertIn('RSSI_RawToDbm(raw) + correction_db', l4s)
        self.assertIn('RSSI_DisplayDbm(rssi_dBm)', native)
        self.assertIn('RSSI_DisplayDbm(s->rssi_dbm)', (root / 'App/ui/l4s.c').read_text(encoding='utf-8'))

    def test_saved_volume_levels_keep_the_original_loudness_curve(self):
        lib = Firmware().lib
        lib.Host_NativeSidetoneLevel.argtypes = [ctypes.c_uint8]
        lib.Host_NativeSidetoneLevel.restype = ctypes.c_uint8
        self.assertEqual([0, 6, 11, 12, 13, 14, 15],
                         [lib.Host_NativeSidetoneLevel(level) for level in range(7)])
        self.assertEqual(0, lib.Host_NativeSidetoneLevel(255))

    def test_local_offsets_are_gated_without_removing_cw_bfo_compensation(self):
        source = (Path(__file__).resolve().parents[2] / 'App/app/cwrit.c').read_text(encoding='utf-8')
        self.assertIn('return !L4S_Active();', source)
        self.assertIn('s_rit_on && pVfo == gTxVfo && LocalOffsetAllowed()', source)
        self.assertIn('s_xit_on && LocalOffsetAllowed()', source)
        self.assertIn('frequency -= gEeprom.CW_TONE_FREQUENCY;', source)
        self.assertIn('frequency += gEeprom.CW_TONE_FREQUENCY;', source)

    def test_remote_playback_resets_new_modes_and_reports_guard_failures(self):
        root = Path(__file__).resolve().parents[2]
        keyer = (root / 'App/app/cwkeyer.c').read_text(encoding='utf-8')
        remote = keyer.split('bool CW_StartRemoteTextPlayback(', 1)[1].split('void CW_StopPlayback(', 1)[0]
        self.assertIn('s_play_long_dah=false;', remote)
        self.assertIn('CW_BeginPlayback(false,gEeprom.CW_FARNSWORTH_WPM);', remote)
        control = (root / 'App/app/l4s_control.c').read_text(encoding='utf-8')
        self.assertIn('CW_StartRemoteTextPlayback(text,length)', control)
        self.assertIn('gTxTimeoutReached || CW_GuardIsLocked()', control)
        app = (root / 'App/app/cwapp.c').read_text(encoding='utf-8')
        self.assertIn('CW_Guard_Filter(CW_ACTION_NONE,millis(),&idle_guard);', app)

    def test_legacy_version_reply_is_bounded_and_zero_initialized(self):
        source = (Path(__file__).resolve().parents[2] / 'App/app/uart.c').read_text(encoding='utf-8')
        version = source.split('static void SendVersion(', 1)[1].split('SendReply(Port, &Reply, sizeof(Reply));', 1)[0]
        self.assertIn('REPLY_0514_t Reply = {0};', version)
        self.assertIn('i+1<sizeof(Reply.Data.Version)', version)
        self.assertNotIn('strcpy(Reply.Data.Version', version)


if __name__ == '__main__':
    unittest.main()
