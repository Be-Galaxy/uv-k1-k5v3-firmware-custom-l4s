"""Execute the production CW adapter with hardware/queue boundaries stubbed."""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class CwStartTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='l4s-cw-')
        folder = Path(cls.temp.name)
        source = (ROOT / 'App/app/l4s_control.c').read_text(encoding='utf-8')
        harness = r'''
#include <stdbool.h>
#include <stdint.h>
#define VFO_STATE_NORMAL 0
#define FUNCTION_RECEIVE 1
static struct { bool menu_open; int applied; } state;
static struct { bool KEY_LOCK; uint8_t CW_KEY_WPM; } gEeprom;
static struct { bool BUSY_CHANNEL_LOCK; } vfo, *gTxVfo = &vfo;
static bool release_latch, g_SquelchLost, gTxTimeoutReached;
static uint8_t gBatteryDisplayLevel, VfoState[2];
static unsigned gTxTimerCountdown_500ms;
static int gCurrentFunction;
static bool retune, pressed, locked, valid, queued;
static unsigned queue_calls, status_clears;
static bool can_retune(void) { return retune; }
static bool GPIO_IsPttPressed(void) { return pressed; }
static bool CW_GuardIsLocked(void) { return locked; }
static bool validate_pair(const void *p) { (void)p; return valid; }
static bool CW_StartRemoteTextPlayback(const uint8_t *p, uint8_t n) {
    (void)p; (void)n; queue_calls++; return queued;
}
static void RADIO_SetVfoState(uint8_t value) {
    VfoState[0] = VfoState[1] = value; status_clears++;
}
'''
        harness += function(source, 'static bool cw_start(')
        harness += '\n' + function(source, 'static bool cw_failed(')
        harness += r'''
void reset(void) {
    state.menu_open = release_latch = gEeprom.KEY_LOCK = pressed = locked = false;
    vfo.BUSY_CHANNEL_LOCK = g_SquelchLost = false;
    retune = valid = queued = true;
    gBatteryDisplayLevel = 4; gCurrentFunction = 0;
    gTxTimeoutReached = false; VfoState[0] = VfoState[1] = 0;
    gTxTimerCountdown_500ms = 1;
    queue_calls = status_clears = 0;
}
void fault(unsigned mask) {
    gTxTimeoutReached = (mask & 1) != 0;
    VfoState[1] = (mask & 2) ? 4 : 0;
    locked = (mask & 4) != 0;
}
void reject(unsigned reason) {
    switch (reason) {
    case 0: gBatteryDisplayLevel = 0; break;
    case 1: gBatteryDisplayLevel = 7; break;
    case 2: valid = false; break;
    case 3: gEeprom.KEY_LOCK = true; break;
    case 4: pressed = true; break;
    case 5: retune = false; break;
    case 6: vfo.BUSY_CHANNEL_LOCK = g_SquelchLost = true; break;
    case 7: state.menu_open = true; break;
    case 8: queued = false; break;
    }
}
int start(void) { return cw_start((const uint8_t *)"CQ DE BG5JSU", 12, 11); }
int failed(void) { return cw_failed(); }
void tick500ms(void) {
    if (gTxTimerCountdown_500ms && !--gTxTimerCountdown_500ms) gTxTimeoutReached = true;
}
unsigned clears(void) { return status_clears; }
'''
        c_file = folder / 'cw_start.c'
        c_file.write_text(harness, encoding='utf-8')
        binary = folder / ('cw_start.dll' if os.name == 'nt' else 'cw_start.so')
        compiler = os.environ.get('L4S_HOST_CC') or shutil.which('gcc')
        if not compiler and os.name == 'nt':
            compiler = 'H:/tools/msys64/ucrt64/bin/gcc.exe'
        environment = os.environ.copy()
        environment['PATH'] = str(Path(compiler or 'gcc').parent) + os.pathsep + environment['PATH']
        subprocess.run([compiler or 'gcc', '-std=c11', '-Wall', '-Wextra', '-shared',
                        '-fPIC', str(c_file), '-o', str(binary)], check=True, env=environment)
        cls.lib = ctypes.CDLL(str(binary))

    @classmethod
    def tearDownClass(cls):
        if os.name == 'nt':
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)
        del cls.lib
        cls.temp.cleanup()

    def setUp(self):
        self.lib.reset()

    def test_previous_timeout_does_not_fail_new_queued_message(self):
        self.lib.fault(1)
        self.assertEqual(1, self.lib.start())
        self.assertEqual(0, self.lib.failed())

    def test_previous_vfo_error_does_not_fail_new_queued_message(self):
        self.lib.fault(2)
        self.assertEqual(1, self.lib.start())
        self.assertEqual(0, self.lib.failed())

    def test_previous_transmit_countdown_cannot_expire_during_lead_in(self):
        self.assertEqual(1, self.lib.start())
        self.lib.tick500ms()
        self.assertEqual(0, self.lib.failed())

    def test_current_failures_remain_visible_after_start(self):
        for mask in (1, 2, 4):
            with self.subTest(mask=mask):
                self.lib.reset()
                self.assertEqual(1, self.lib.start())
                self.lib.fault(mask)
                self.assertEqual(1, self.lib.failed())

    def test_guard_lock_is_not_cleared_to_force_a_start(self):
        self.lib.fault(4)
        self.assertEqual(0, self.lib.start())
        self.assertEqual(1, self.lib.failed())
        self.assertEqual(0, self.lib.clears())

    def test_refused_or_invalid_queue_does_not_clear_history(self):
        for reason in range(9):
            with self.subTest(reason=reason):
                self.lib.reset()
                self.lib.fault(3)
                self.lib.reject(reason)
                self.assertEqual(0, self.lib.start())
                self.assertEqual(1, self.lib.failed())
                self.assertEqual(0, self.lib.clears())


if __name__ == '__main__':
    unittest.main()
