import ast
import hashlib
import json
import re
import struct
import tempfile
import unittest
from pathlib import Path

from verify_pteronautos_image import HARDWARE_PATH, verify_image


class ImageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'firmware.bin'

    def image(self, hardware=None, options=None, flash=0x20):
        data = bytearray(0x1020)
        data[:4] = bytes([0xe9, 2, 3, flash])
        data[0x1000:0x1008] = struct.pack('<BBBBI', 0xe9, 1, 3, flash, 0)
        data[0x1008:0x1010] = struct.pack('<II', 0x40100000, 4)
        data += b'PteronautOS PWMP7'.ljust(128, b'\0') + b'PWMP7 RX'.ljust(16, b'\0')
        data += json.dumps(options if options is not None else {'wifi-on-interval': 30}).encode().ljust(512, b'\0')
        data += json.dumps(hardware if hardware is not None else json.loads(HARDWARE_PATH.read_text())).encode().ljust(2048, b'\0')
        self.path.write_bytes(data)
        return self.path

    def test_valid_with_and_without_binding(self):
        for interval in [-1, 0, 10, 30, 2147483]:
            for uid in [None, [1, 2, 3, 4, 5, 6]]:
                opts = {'wifi-on-interval': interval}
                if uid is not None:
                    opts['uid'] = uid
                self.assertEqual(verify_image(self.image(options=opts)), opts)

    def test_upstream_map_rejected(self):
        hw = json.loads(HARDWARE_PATH.read_text())
        del hw['radio_rst']
        hw['pwm_outputs'].append(2)
        with self.assertRaisesRegex(ValueError, 'Hardware differs'):
            verify_image(self.image(hardware=hw))

    def test_bad_clock_rejected(self):
        with self.assertRaisesRegex(ValueError, '40 MHz'):
            verify_image(self.image(flash=0x2f))

    def test_truncation_rejected(self):
        self.image()
        self.path.write_bytes(self.path.read_bytes()[:-2048])
        with self.assertRaisesRegex(ValueError, 'Missing embedded'):
            verify_image(self.path)

    def test_invalid_options_rejected(self):
        for opts in [{'uid': [1]}, {'wifi-on-interval': -2}, {'wifi-on-interval': 2147484}]:
            with self.assertRaises(ValueError):
                verify_image(self.image(options=opts))


class BuildFlagTests(unittest.TestCase):
    def test_flash_paths_use_same_frequency(self):
        root = Path(__file__).resolve().parents[2]
        self.assertIn('flashFreq: "40m"', (root / 'docs/assets/js/flasher.coffee').read_text())
        self.assertIn('--flash_freq 40m', (root / 'scripts/flash.sh').read_text())
        self.assertIn('board_build.f_flash = 40000000L', (root / 'src/targets/pteronautos-rx.ini').read_text())

    def parser(self, target):
        # Execute the actual pure parser functions and RX classification, not
        # PlatformIO's build side effects or local user_defines/secrets.
        tree = ast.parse(Path(__file__).with_name('build_flags.py').read_text())
        nodes = [n for n in tree.body if
                 (isinstance(n, ast.FunctionDef) and n.name in ('dequote', 'process_json_flag')) or
                 (isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'isRX' for t in n.targets))]
        scope = dict(re=re, hashlib=hashlib, target_name=target, json_flags={})
        exec(compile(ast.Module(body=nodes, type_ignores=[]), 'build_flags.py', 'exec'), scope)
        return scope

    def test_receiver_target_variants(self):
        for target in ['PTERONAUTOS_ESP8285_2400_RX', 'PTERONAUTOS_ESP8285_2400_RX_GEARBOX', 'UNIFIED_ESP8285_2400_RX_VIA_UART']:
            scope = self.parser(target)
            self.assertTrue(scope['isRX'])
            scope['process_json_flag']('-DRCVR_UART_BAUD=115200')
            self.assertEqual(scope['json_flags']['rcvr-uart-baud'], 115200)
        self.assertFalse(self.parser('UNIFIED_ESP8285_2400_TX')['isRX'])

    def test_wifi_disabled_and_seconds(self):
        scope = self.parser('PTERONAUTOS_ESP8285_2400_RX')
        for value in [-1, 0, 10, 30]:
            scope['process_json_flag'](f'-DAUTO_WIFI_ON_INTERVAL={value}')
            self.assertEqual(scope['json_flags']['wifi-on-interval'], value)
        with self.assertRaises(ValueError):
            scope['process_json_flag']('-DAUTO_WIFI_ON_INTERVAL=-2')


if __name__ == '__main__':
    unittest.main()
