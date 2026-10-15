#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Reject false functional success and misleading cross-fixture progress."""
import copy
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('verify_driver',Path(__file__).resolve().parents[1]/'scripts/verify_driver.py')
verifier=importlib.util.module_from_spec(spec);spec.loader.exec_module(verifier)

def observation():
    return {'schema_version':1,'scenario':'synthetic-full-target51',
        'registry_priorities':list(verifier.PRIORITIES),'phase':'dependency',
        'chanspec':{'raw':0xd024,'band':'5GHz','bandwidth_mhz':20,'channel':36},
        'callbacks_completed':5,'current_callback':7,'return_valid':False,
        'return_value':0,'slice_up_stored':False,'trace_consumed':True,
        'trace_events':1163,'failure':{'symbol':'actual_required_child'}}

class VerificationTests(unittest.TestCase):
    def test_nonzero_probe_preserves_actual_failure_observation(self):
        with tempfile.TemporaryDirectory(prefix='axd-verifier-unit-') as temporary:
            root=Path(temporary)/'source';root.mkdir()
            for name in('driver','tests','scripts'):(root/name).mkdir()
            (root/'tests/probe_target51.c').write_text('/* isolated verifier plumbing test */\n')
            output=Path(temporary)/'report'
            failed=observation();failed.update(phase='failed',trace_consumed=False)
            failed['failure']={'layer':'independent_trace','symbol':'unexpected_operation'}
            def execute(command,folder,label,timeout=1800):
                del command,timeout
                if label=='target_execution':
                    (folder/'target_execution.log').write_text(json.dumps(failed))
                    return {'id':label,'state':'failed','return_code':1,'log':'target_execution.log'}
                return {'id':label,'state':'passed','return_code':0,'log':'target_build.log'}
            with patch.object(verifier,'ROOT',root),patch.object(verifier,'execute',execute),\
                 patch.object(verifier.subprocess,'check_output',return_value='unit-test'),\
                 patch.object(verifier.sys,'argv',['verify_driver.py','--report-dir',str(output)]),\
                 contextlib.redirect_stdout(io.StringIO()) as console:
                self.assertEqual(verifier.main(),1)
            report=json.loads((output/'report.json').read_text())
            self.assertEqual(report['target_initialization']['state'],'failed')
            self.assertEqual(report['target_observation']['failure'],failed['failure'])
            self.assertEqual(json.loads(console.getvalue())['first_failure'],failed['failure'])
            self.assertFalse(report['driver_working'])

    def test_dependency_is_incomplete_even_with_green_regressions(self):
        value=observation();self.assertEqual(verifier.validate_probe(value),'incomplete')
        value.update(phase='trace_complete',callbacks_completed=16,current_callback=21,return_valid=True,slice_up_stored=True)
        self.assertEqual(verifier.validate_probe(value),'passed')
        for field,replacement in [('callbacks_completed',15),('current_callback',7),('return_valid',False),('slice_up_stored',False),('return_value',1)]:
            broken=copy.deepcopy(value);broken[field]=replacement
            with self.assertRaises(ValueError):verifier.validate_probe(broken)
        for phase in('failed','cancelled'):
            broken=copy.deepcopy(value);broken['phase']=phase;broken['trace_consumed']=False
            self.assertEqual(verifier.validate_probe(broken),'failed')

    def test_partial_vector_bad_types_and_unused_trace_rejected(self):
        for field,replacement in [('registry_priorities',[7]),('trace_consumed',False),
            ('return_valid',1),('callbacks_completed',True),('phase','running'),
            ('scenario',''),('callbacks_completed',17),('schema_version',True),
            ('registry_priorities',[True,*verifier.PRIORITIES[1:]]),
            ('chanspec',{'raw':0x1024,'band':'2GHz','bandwidth_mhz':20,'channel':36})]:
            value=observation();value[field]=replacement
            with self.assertRaises(ValueError):verifier.validate_probe(value)
        value=observation();del value['trace_events']
        with self.assertRaises(ValueError):verifier.validate_probe(value)

    def test_only_same_fixture_stage_advance_counts(self):
        before={'fixture_sha256':'same','target_observation':observation()};after=copy.deepcopy(before)
        self.assertEqual(verifier.compare(None,after),'first_observation')
        self.assertEqual(verifier.compare(before,after),'same_functional_gate')
        after['target_observation']['trace_events']+=100
        self.assertEqual(verifier.compare(before,after),'same_functional_gate')
        after['target_observation']['callbacks_completed']=6
        self.assertEqual(verifier.compare(before,after),'required_stages_advanced')
        after['fixture_sha256']='changed'
        self.assertEqual(verifier.compare(before,after),'fixture_changed_requires_review')
        after=copy.deepcopy(before);after['target_observation']['callbacks_completed']=4
        self.assertEqual(verifier.compare(before,after),'required_stages_regressed')
        after=copy.deepcopy(before);after['target_observation']['failure']['symbol']='other_required_child'
        self.assertEqual(verifier.compare(before,after),'terminal_changed_requires_review')
        self.assertEqual(verifier.compare([],after),'insufficient_observations')

if __name__=='__main__':unittest.main()
