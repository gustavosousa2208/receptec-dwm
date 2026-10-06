import importlib.util
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('rate', Path(__file__).parents[2] / 'tools/cycle-rate-benchmark.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
STATUS = 'cycle configured=1 running=1 period=500 tag=1 base_a=0 autostart=1 baseline=measured baseline_mm=0'

class RateTests(unittest.TestCase):
    def test_missing_status_does_not_guess(self):
        with self.assertRaises(RuntimeError): m.state('UWB log noise')

    def test_period_change_requires_confirmed_stop(self):
        with patch.object(m, 'console', return_value=STATUS) as console:
            with self.assertRaisesRegex(RuntimeError, 'did not stop'): m.set_period('receptec', 350)
            self.assertEqual(console.call_count, 1)

    def test_set_period_returns_its_already_verified_status(self):
        stopped=STATUS.replace('running=1','running=0')
        target=STATUS.replace('period=500','period=350')
        with patch.object(m,'console',side_effect=[stopped,target]) as console:
            result=m.set_period('receptec',350)
        self.assertEqual(result['period'],'350')
        self.assertEqual(console.call_count,2)

    def test_wrong_probe_never_touches_shell(self):
        args=SimpleNamespace(nick='receptec',probe='802008564')
        with patch.object(m,'command',side_effect=['inventory','{"connected":true,"serial":"wrong","device":"nRF52833_xxAA"}']), patch.object(m,'console') as console:
            with self.assertRaisesRegex(RuntimeError,'identity'): m.run(args)
            console.assert_not_called()

    def test_restores_original_period_on_capture_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            args=SimpleNamespace(nick='receptec',probe='802008564',address='test',period_ms=350,seconds=60,output=Path(directory)/'out')
            async def present(_): return True
            with patch.object(m,'command',side_effect=['inventory','{"connected":true,"serial":"802008564","device":"nRF52833_xxAA"}']), patch.object(m,'console',return_value=STATUS), patch.object(m,'advertiser_present',present), patch.object(m,'set_period',return_value=m.state(STATUS)) as configure, patch.object(m,'capture_process',side_effect=RuntimeError('capture failed')):
                with self.assertRaisesRegex(RuntimeError,'capture failed'): m.run(args)
                self.assertEqual([call.args[1] for call in configure.call_args_list],[350,500])
                self.assertTrue((args.output/'restored-state.json').exists())

if __name__=='__main__': unittest.main()
