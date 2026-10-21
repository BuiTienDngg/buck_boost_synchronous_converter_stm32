from pathlib import Path
import sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from probe_cc_short import ShortProbe,summarize


class ShortChecks(unittest.TestCase):
    def sample(self,**updates):
        m=dict(enabled=1,fault=0,config=0,temp_dC=380,temp_valid=1,vin_mV=17800,
               vout_mV=100,iout_mA=2000,adc_age=1,cv_sat=0,state=3)
        m.update(updates);return m

    def test_experiment_limits(self):
        ShortProbe.guard(None,self.sample())
        for change in [dict(iout_mA=2201),dict(vout_mV=2001),dict(fault=1),dict(config=1),
                       dict(adc_age=3),dict(temp_valid=0),dict(temp_dC=501),dict(cv_sat=1),dict(vin_mV=7000)]:
            with self.subTest(change=change),self.assertRaises(RuntimeError):ShortProbe.guard(None,self.sample(**change))

    def test_summary_is_not_a_tuning_claim(self):
        result=summarize([self.sample(),self.sample(iout_mA=1980)])
        self.assertAlmostEqual(result['estimated_R_ohm'],.05025252525)
        self.assertEqual(result['cc_samples'],2)
        self.assertIn('insufficient',result['conclusion'])

    def test_fault_before_first_sample(self):
        self.assertEqual(summarize([])['samples'],0)

    def test_sustained_low_voltage_short_stops(self):
        board=ShortProbe.__new__(ShortProbe)
        board.guard(self.sample(ms=1000))
        board.guard(self.sample(ms=1199))
        with self.assertRaisesRegex(RuntimeError,'Sustained short'):
            board.guard(self.sample(ms=1200))
        board.guard(self.sample(ms=1200,vout_mV=500))


if __name__=='__main__':unittest.main()
