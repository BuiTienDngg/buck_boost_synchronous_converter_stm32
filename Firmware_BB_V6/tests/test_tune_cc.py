from pathlib import Path
import sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from tune_cc import CurrentBoard,metric


class CurrentChecks(unittest.TestCase):
    def sample(self,**changes):
        m=dict(enabled=1,ms=1000,fault=0,config=0,temp_dC=330,temp_valid=1,vin_mV=17800,
               vout_mV=1100,iout_mA=2000,adc_age=1,cv_sat=0,state=3)
        m.update(changes);return m

    def test_guards(self):
        board=CurrentBoard.__new__(CurrentBoard);board.guard(self.sample())
        for update in [dict(iout_mA=2501),dict(fault=1),dict(adc_age=3),dict(temp_valid=0),dict(vout_mV=2001),dict(ms=1200,vout_mV=100)]:
            with self.subTest(update=update),self.assertRaises(RuntimeError):board.guard(self.sample(**update))

    def test_metrics_require_stable_cc(self):
        samples=[self.sample(ms=i*30,iout_mA=1995+10*(i%2)) for i in range(80)]
        result=metric(samples);self.assertTrue(result['valid']);self.assertEqual(result['p2p_mA'],10)
        self.assertFalse(metric([dict(s,state=2) for s in samples])['valid'])
        self.assertFalse(metric([dict(s,iout_mA=1800) for s in samples])['valid'])
        self.assertFalse(metric(samples[:10])['valid'])
        longer=[self.sample(ms=i*30) for i in range(300)]
        longer[50]['iout_mA']=2400
        self.assertFalse(metric(longer)['valid'])


if __name__=='__main__':unittest.main()
