"""Validate experiment guards and rejection of unsuitable identification data."""
from pathlib import Path
import sys, unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from tune_cv import Board, metric


class ExperimentChecks(unittest.TestCase):
    def sample(self,**updates):
        m=dict(enabled=1,fault=0,config=0,temp_valid=1,temp_dC=320,vin_mV=17800,
               vout_mV=5000,iout_mA=200,cv_sat=0,adc_age=1,state=2,ramp_active=0,buck_pm=250)
        m.update(updates);return m

    def test_measurement_interlocks(self):
        for change in [dict(fault=1),dict(config=4),dict(temp_dC=510),dict(temp_valid=0),
                       dict(vin_mV=7000),dict(vin_mV=40000),dict(vout_mV=5501),
                       dict(iout_mA=1201),dict(cv_sat=1),dict(adc_age=3)]:
            with self.subTest(change=change),self.assertRaises(RuntimeError):Board.guard(None,self.sample(**change))

    def test_off_config_sensor_reset_allowed(self):
        Board.guard(None,self.sample(enabled=0,temp_valid=0,adc_age=10000))

    def test_no_load_not_accepted_as_tuned_regulation(self):
        samples=[self.sample(ms=30*i,iout_mA=0,buck_pm=0) for i in range(30)]
        self.assertFalse(metric(samples,5000)['valid'])

    def test_current_limit_not_accepted_as_cv(self):
        samples=[self.sample(ms=30*i,state=3) for i in range(30)]
        self.assertFalse(metric(samples,5000)['valid'])

    def test_loaded_metrics_and_timing(self):
        samples=[self.sample(ms=30*i,vout_mV=4990+20*(i%2)) for i in range(30)]
        result=metric(samples,5000)
        self.assertTrue(result['valid']);self.assertEqual(result['mae_mV'],10)
        self.assertEqual(result['tail_p2p_mV'],20);self.assertEqual(result['median_gap_ms'],30)


if __name__=='__main__':unittest.main()
