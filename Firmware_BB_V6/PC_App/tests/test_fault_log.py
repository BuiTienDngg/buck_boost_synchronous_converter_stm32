from pathlib import Path
import sys,tempfile,unittest,json
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from fault_log import FaultJournal


class FaultLogTests(unittest.TestCase):
    def test_tip_fault_uses_solder_reason_and_retains_raw_sample(self):
        with tempfile.TemporaryDirectory() as root:
            journal=FaultJournal(root)
            sample=dict(ms=100,mode=2,solder=dict(reason=4,adc=1273,temp_dC=253))
            event=journal.observe(sample)
            self.assertEqual(event['kind'],'protection')
            self.assertIn('không tăng',event['summary'])
            self.assertEqual(event['data']['solder']['adc'],1273)
            self.assertIsNone(journal.observe(dict(sample,ms=200)))
            event=journal.observe(dict(ms=300,mode=2,solder=dict(reason=0)))
            self.assertEqual(event['kind'],'cleared')
    def test_dedup_snapshot_recovery_and_reboot(self):
        with tempfile.TemporaryDirectory() as root:
            journal=FaultJournal(root);sample=dict(ms=100,fault=0,config=0,start_block=0)
            self.assertIsNone(journal.observe(sample));self.assertFalse(journal.path.exists())
            sample.update(ms=200,fault=2,trip=dict(cause=2,vo_adc=334,ovp_adc=310))
            event=journal.observe(sample,settings=dict(cv_kp=15000))
            self.assertIn('Quá áp',event['summary'])
            sample['trip']['vo_adc']=0
            self.assertEqual(event['data']['trip']['vo_adc'],334)
            self.assertIsNone(journal.observe(sample))
            journal.observe(dict(sample,ms=300,fault=0));journal.observe(dict(sample,ms=10,fault=2))
            rows=[json.loads(s) for s in journal.path.read_text(encoding='utf-8').splitlines()]
            self.assertEqual([r['kind'] for r in rows],['protection','cleared','protection'])
            self.assertTrue(rows[0]['time'].endswith('+07:00'))

    def test_touch_io_counter_does_not_spam(self):
        with tempfile.TemporaryDirectory() as root:
            journal=FaultJournal(root)
            journal.observe(dict(ms=1,fault=0,touch=dict(errors=1)))
            for ms in range(2,100):journal.observe(dict(ms=ms,fault=0,touch=dict(errors=ms)))
            self.assertEqual(len(journal.entries),1)

    def test_charge_error_and_full_are_logged_once(self):
        with tempfile.TemporaryDirectory() as root:
            journal=FaultJournal(root)
            event=journal.observe(dict(ms=100,mode=1,charge=dict(phase=6,reason=4)))
            self.assertEqual(event['kind'],'protection');self.assertIn('thời gian',event['summary'])
            self.assertIsNone(journal.observe(dict(ms=200,mode=1,charge=dict(phase=6,reason=4))))
            journal.observe(dict(ms=300,mode=1,charge=dict(phase=3,reason=0)))
            event=journal.observe(dict(ms=400,mode=1,enabled=0,charge=dict(phase=4,reason=0,mAh10=123)))
            self.assertEqual(event['kind'],'complete');self.assertEqual(event['data']['charge']['mAh10'],123)
            self.assertIsNone(journal.observe(dict(ms=500,mode=1,charge=dict(phase=4,reason=0))))
