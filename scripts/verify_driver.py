#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run offline functional gates; passing regressions never means working Wi-Fi.

All binaries, logs and reports go to a new external directory. No device access,
module loading, network traffic, flashing or RF operation is implemented here.
Exit0: requested offline gates complete; exit1: check/trace failure; exit2:
honestly incomplete or missing proof. Hardware/kernel readiness stays unverified.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

ROOT=Path(__file__).resolve().parents[1]
MODULE_SHA256='8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c'
PRIORITIES=[1,3,4,5,6,7,8,9,10,11,12,13,16,19,20,21]
ARM_CHECKERS=(
    'check_phy_arm.py','check_phy_composed_arm.py','check_wake_policy_arm.py',
    'check_object_memory_arm.py','check_dma_descriptor_arm.py',
    'check_dma_tx_post_arm.py','check_dma_rx_take_arm.py',
    'check_radio_identity_arm.py','check_radio_control_arm.py',
    'check_dma_rx_list_arm.py','check_rx_processing_arm.py',
    'check_radio_power_arm.py','check_dma_rx_fill_idle_arm.py',
    'check_offload_status_arm.py')

def fingerprint(paths):
    digest=hashlib.sha256()
    for path in sorted(set(paths)):
        name=str(path.relative_to(ROOT)).encode();data=path.read_bytes()
        digest.update(len(name).to_bytes(8,'little'));digest.update(name)
        digest.update(len(data).to_bytes(8,'little'));digest.update(data)
    return digest.hexdigest()

def validate_probe(value):
    """Validate an observation, not the expected location of today's blocker."""
    if not isinstance(value,dict) or type(value.get('schema_version')) is not int or value['schema_version']!=1:raise ValueError('Unknown probe schema')
    priorities=value.get('registry_priorities')
    if type(priorities) is not list or any(type(p) is not int for p in priorities) or priorities!=PRIORITIES:raise ValueError('Probe did not run the entire target registry')
    if not isinstance(value.get('scenario'),str) or not value['scenario']:raise ValueError('Missing scenario identity')
    channel=value.get('chanspec')
    if not isinstance(channel,dict) or type(channel.get('raw')) is not int or type(channel.get('channel')) is not int:
        raise ValueError('Missing explicit synthetic chanspec')
    raw=channel['raw']
    if not 0<=raw<=65535 or raw&0xc000!=0xc000 or raw&0x3800!=0x1000 or channel.get('band')!='5GHz' or channel.get('bandwidth_mhz')!=20 or channel['channel']!=raw&255:
        raise ValueError('Expected explicit 5GHz20MHz scenario; do not bypass the radar band guard')
    if value.get('phase') not in('trace_complete','dependency','failed','cancelled'):raise ValueError('Probe did not terminate')
    for key in('callbacks_completed','current_callback','return_value','trace_events'):
        if type(value.get(key)) is not int:raise ValueError('Missing integer observation: '+key)
    if not 0<=value['callbacks_completed']<=16 or value['trace_events']<0:raise ValueError('Invalid progress bounds')
    for key in('return_valid','slice_up_stored','trace_consumed'):
        if type(value.get(key)) is not bool:raise ValueError('Missing boolean observation: '+key)
    if value['phase'] in('failed','cancelled'):return 'failed'
    if not value['trace_consumed']:raise ValueError('Independent trace/read queue was not consumed')
    complete=(value['phase']=='trace_complete' and value['callbacks_completed']==16 and value['current_callback']==PRIORITIES[-1] and
        value['return_valid'] and value['return_value']==0 and value['slice_up_stored'])
    if value['phase']=='trace_complete' and not complete:raise ValueError('Incomplete full-target success claim')
    return 'passed' if complete else 'incomplete'

def compare(previous,current):
    if previous is None:return 'first_observation'
    if not isinstance(previous,dict):return 'insufficient_observations'
    before=previous.get('target_observation');after=current.get('target_observation')
    if not isinstance(before,dict) or not isinstance(after,dict):return 'insufficient_observations'
    try:validate_probe(before);validate_probe(after)
    except ValueError:return 'insufficient_observations'
    if not previous.get('fixture_sha256') or not current.get('fixture_sha256'):return 'insufficient_observations'
    if before['scenario']!=after['scenario'] or previous['fixture_sha256']!=current['fixture_sha256']:
        return 'fixture_changed_requires_review'
    if after['callbacks_completed']>before['callbacks_completed']:return 'required_stages_advanced'
    if after['callbacks_completed']<before['callbacks_completed']:return 'required_stages_regressed'
    if (before['phase'],before.get('failure'))!=(after['phase'],after.get('failure')):return 'terminal_changed_requires_review'
    return 'same_functional_gate'

def execute(command,folder,label,timeout=1800):
    """Capture private diagnostics; bound and terminate this subprocess group."""
    print('Checking '+label,flush=True);start=time.monotonic();log=folder/(label+'.log')
    result={'id':label,'state':'failed','elapsed_seconds':0,'log':log.name}
    try:
        with log.open('wb') as stream:
            process=subprocess.Popen(command,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
            try:code=process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid,signal.SIGTERM)
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
                result['reason']='bounded check timed out';code=None
        result['return_code']=code
        if code==0:result['state']='passed'
    except OSError as error:
        # Do not echo private command arguments or arbitrary diagnostic payloads.
        result['reason']=type(error).__name__
    result['elapsed_seconds']=round(time.monotonic()-start,3)
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scope',choices=('target','checkpoint'),default='target')
    parser.add_argument('--module',type=Path,help='Optional caller-supplied pinned wl.ko')
    parser.add_argument('--unicorn-python',type=Path,help='Python containing pinned Unicorn2.1.4; requires --module')
    parser.add_argument('--report-dir',type=Path,help='NEW directory outside checkout; default external temporary directory')
    parser.add_argument('--previous-report',type=Path,help='Prior report.json for same-fixture gate comparison')
    args=parser.parse_args()
    if args.unicorn_python and not args.module:parser.error('--unicorn-python requires --module')
    if args.report_dir:
        folder=args.report_dir.resolve()
        if folder==ROOT or ROOT in folder.parents:parser.error('Report directory must be outside checkout')
        try:folder.mkdir(mode=0o700)
        except OSError:parser.error('Report directory must be new and its parent must exist')
    else:folder=Path(tempfile.mkdtemp(prefix='axd-driver-verification-'))
    previous=None
    if args.previous_report:
        try:previous=json.loads(args.previous_report.read_text())
        except (OSError,ValueError):parser.error('Previous report is unreadable or invalid JSON')
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True))
    def source_paths():return list((ROOT/'driver').glob('*.[ch]'))
    def fixture_paths():return [p for p in (ROOT/'tests').rglob('*') if p.suffix in('.c','.h')]
    def checker_paths():return [p for folder in ('scripts','tests') for p in (ROOT/folder).glob('*') if p.suffix in('.py','.sh')]
    report={'schema_version':1,'scope':args.scope,'revision':revision,'dirty':dirty,
        'source_sha256':fingerprint(source_paths()),'fixture_sha256':fingerprint(fixture_paths()),
        'checker_sha256':fingerprint(checker_paths()),
        'checks':[],'target_observation':None,'driver_working':False,
        'kernel_integration':{'state':'unverified','reason':'No supported kernel build/resource/DMA/lifecycle verification is registered with this offline tool'},
        'hardware_validation':{'state':'not_run','reason':'Existing no-hardware/RF constraint; host traces cannot establish physical readiness'}}
    check_failed=False;target_state='incomplete'
    if args.module:
        try:
            if args.module.stat().st_size>64*1024*1024:raise ValueError('size')
            digest=hashlib.sha256(args.module.read_bytes()).hexdigest()
            if digest!=MODULE_SHA256:raise ValueError('identity')
            report['input']={'name':'wl.ko','sha256':digest}
        except (OSError,ValueError):
            report['checks'].append({'id':'input_identity','state':'failed','reason':'Pinned input identity was not established'})
            check_failed=True
    probe=ROOT/'tests/probe_target51.c';program=folder/'probe_target51'
    if not check_failed and probe.exists():
        result=execute(['cc','-std=c11','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=address,undefined',
            *map(str,sorted((ROOT/'driver').glob('*.c'))),str(probe),'-o',str(program)],folder,'target_build')
        report['checks'].append(result);check_failed=result['state']!='passed'
        if not check_failed:
            result=execute([str(program)],folder,'target_execution',120);report['checks'].append(result)
            check_failed=result['state']!='passed'
            try:
                observation=json.loads((folder/result['log']).read_text());observed_state=validate_probe(observation)
                report['target_observation']=observation
                target_state='failed' if check_failed else observed_state
                check_failed=check_failed or target_state=='failed'
            except (ValueError,OSError):
                result['state']='failed';result['reason']='Invalid functional observation or independent trace'
                check_failed=True;target_state='failed'
    elif not check_failed:
        report['checks'].append({'id':'target_build','state':'not_run','reason':'Full-target probe is missing'})
    if args.scope=='checkpoint' and not check_failed:
        result=execute(['sh','scripts/test_driver.sh',*([str(args.module.resolve())] if args.module else [])],folder,'regressions')
        report['checks'].append(result);check_failed=result['state']!='passed'
        for path in [*sorted((ROOT/'driver').glob('*.c')),ROOT/'tests/check_driver_headers.c'] if not check_failed else []:
            result=execute(['clang','--target=armv7-none-eabi','-ffreestanding','-std=c11','-Wall','-Wextra','-Werror','-pedantic',
                '-c',str(path),'-o',str(folder/(path.stem+'.o'))],folder,'armv7_'+path.stem)
            report['checks'].append(result)
            if result['state']!='passed':check_failed=True;break
        if args.unicorn_python and not check_failed:
            for name in ARM_CHECKERS:
                # Preserve the venv launcher path: resolving its symlink to the
                # global Python executable discards that environment's modules.
                result=execute([str(args.unicorn_python.absolute()),str(ROOT/'tests'/name),str(args.module.resolve())],folder,name.removesuffix('.py'))
                report['checks'].append(result)
                if result['state']!='passed':check_failed=True;break
        else:report['checks'].append({'id':'stock_arm_comparisons','state':'not_run','reason':'Pinned input and Unicorn interpreter required, or an earlier check failed'})
    report['target_initialization']={'state':target_state,'scope':'Complete16-stage synthetic PHY vector only; not enclosing BMAC initialization or device readiness'}
    report['functional_gates']=[
        {'id':'phy_vector','state':target_state,'proof':'Actual full16-vector probe with independent synthetic trace'},
        {'id':'enclosing_initialization','state':'unverified','proof_required':'Full attach/reset/upload/start/BMAC initialization with all required real children and explicit resource ownership'},
        {'id':'receive_delivery','state':'unverified','proof_required':'Allocate/fill/completion/status/BMAC parsing and delivery through the selected kernel networking interface'},
        {'id':'transmit_completion','state':'unverified','proof_required':'Normal packet TX, completion/reclaim and ownership, beyond descriptor-only posting'},
        {'id':'kernel_integration','state':'unverified','proof_required':'Target kernel build plus resource, DMA, IRQ, locking and lifecycle integration'},
        {'id':'hardware_validation','state':'not_run','proof_required':'Exact-board identity, calibrated radio, real RX/TX/association and recovery validation; no hardware action in this tool'}]
    report['first_unproven_gate']=next(g['id'] for g in report['functional_gates'] if g['state']!='passed')
    report['functional_change']=compare(previous,report)
    # Prevent an edit during verification from being silently blessed as tested.
    if report['source_sha256']!=fingerprint(source_paths()) or report['fixture_sha256']!=fingerprint(fixture_paths()) or report['checker_sha256']!=fingerprint(checker_paths()):
        check_failed=True;report['checks'].append({'id':'stable_inputs','state':'failed','reason':'Sources/fixtures changed during checks; rerun the affected verification'})
    missing_proof=any(c['state']=='not_run' for c in report['checks'])
    report['offline_result']='failed' if check_failed else 'complete' if target_state=='passed' and not missing_proof else 'incomplete'
    output=folder/'report.json';output.write_text(json.dumps(report,indent=2)+'\n');output.chmod(0o600)
    functional_failure=(report['target_observation'] or {}).get('failure')
    check_failure=next((c for c in report['checks'] if c['state']=='failed'),None)
    first_failure=functional_failure if target_state=='failed' and functional_failure else check_failure or functional_failure
    print(json.dumps({'offline_result':report['offline_result'],'target_initialization':target_state,
        'first_unproven_gate':report['first_unproven_gate'],
        'first_failure':first_failure,'functional_dependency':functional_failure,
        'functional_change':report['functional_change'],'driver_working':False,'report':str(output)},indent=2),flush=True)
    return 1 if check_failed else 0 if report['offline_result']=='complete' else 2

if __name__=='__main__':sys.exit(main())
