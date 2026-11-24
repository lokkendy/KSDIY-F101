#!/usr/bin/env python3
"""Offline, identity-free rates from section-prefixed F101 JSON status snapshots."""
import argparse
import json
import math
from pathlib import Path
import re

MAP = {
    'video': {'frames':'frames', 'errors':'errors', 'dropped':'dropped', 'free':'free', 'min':'min_free'},
    'video async': {'decoded':'decoded', 'converted':'converted', 'submitted':'submitted', 'drawn':'drawn'},
    'display perf': {'pans':'pans'},
    'video path': {'feed_fail':'feed_fail'},
    'transport': {'aus':'aus', 'video_packets':'video_packets'},
    'video backlog': {'sbm_bytes':'sbm_bytes', 'sbm_frames':'sbm_frames'},
    'video recovery': {'generation':'generation'},
    'osmo': {'state':'state', 'link':'link'},
}
COUNTERS = ('frames','errors','dropped','decoded','converted','submitted','drawn','pans','feed_fail','aus','video_packets')
RATES = ('decoded','converted','submitted','drawn','pans','aus','video_packets')

def parse(sample):
    if not isinstance(sample, dict) or not isinstance(sample.get('raw'), str):
        raise ValueError('Every sample requires textual raw and numeric time')
    value = sample.get('time')
    if isinstance(value, bool) or not isinstance(value, (int,float)) or not math.isfinite(value):
        raise ValueError('time must be finite numeric seconds')
    result = {'time': float(value)}
    for line in sample['raw'].splitlines():
        match = re.match(r'^\[([^\]]+)\]\s*(.*)', line.strip())
        if not match or match[1] not in MAP:
            continue
        fields = dict(re.findall(r'\b(\w+)=(-?\d+)\b', match[2]))
        for key, out in MAP[match[1]].items():
            if key in fields:
                result[out] = int(fields[key])
    return result

def analyze(samples):
    if not isinstance(samples, list) or len(samples) < 2:
        raise ValueError('At least two samples are required')
    parsed = [parse(x) for x in samples]
    intervals, rejected = [], []
    previous_segment = None
    segment = 0
    for i, (a,b) in enumerate(zip(parsed, parsed[1:])):
        dt = b['time'] - a['time']
        reason = None
        if dt <= 0:
            reason = 'non_increasing_time'
        elif any(k in a and k in b and b[k] < a[k] for k in COUNTERS):
            reason = 'counter_rollback_or_reset'
        elif any(a.get(k) != b.get(k) for k in ('state','link','generation')):
            reason = 'session_or_state_change'
        elif any(k in x and x[k] != target for x in (a,b) for k,target in (('state',9),('link',3))):
            reason = 'not_active_preview'
        if reason:
            rejected.append({'from_sample':i,'to_sample':i+1,'reason':reason})
            previous_segment = None
            continue
        if previous_segment != i-1:
            segment += 1
        previous_segment = i
        delta = {k:b[k]-a[k] for k in COUNTERS if k in a and k in b}
        rate = {k:delta[k]/dt if k in delta else None for k in RATES}
        diagnosis = 'insufficient_stage_data'
        if 'aus' in delta and delta['aus'] == 0:
            diagnosis = 'no_complete_AU_progress_check_video_packets_and_control'
        elif delta.get('aus',0) > 0 and 'decoded' in delta and delta['decoded'] == 0:
            diagnosis = 'AU_progress_without_decode_progress'
        elif delta.get('decoded',0) > 0 and 'drawn' in delta and delta['drawn'] == 0:
            diagnosis = 'decode_progress_without_draw_progress'
        elif delta.get('drawn',0) > 0:
            diagnosis = 'display_progress'
        intervals.append({'segment':segment,'from_sample':i,'to_sample':i+1,
                          'seconds':dt,'delta':delta,'fps':rate,'observation':diagnosis})
    segments = []
    for sid in sorted({x['segment'] for x in intervals}):
        rows = [x for x in intervals if x['segment'] == sid]
        seconds = sum(x['seconds'] for x in rows)
        totals = {k:sum(x['delta'][k] for x in rows) for k in COUNTERS if all(k in x['delta'] for x in rows)}
        indexes = set(n for x in rows for n in (x['from_sample'],x['to_sample']))
        memory = [parsed[n][k] for n in indexes for k in ('free','min_free') if k in parsed[n]]
        segments.append({'segment':sid,'seconds':seconds,'delta':totals,
                         'fps':{k:totals[k]/seconds if k in totals else None for k in RATES},
                         'observed_heap_lower_bound_bytes':min(memory) if memory else None})
    return {'schema_version':1,'samples':len(parsed),'intervals':intervals,'segments':segments,
            'rejected_intervals':rejected,
            'notes':['Rates are counter deltas per measured second; missing stages stay null.',
                     'No wraparound is assumed. Session changes split windows.',
                     'A session change and back between snapshots cannot be detected.',
                     'pans is valid only if firmware increments it on completed scanout flips.']}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path)
    parser.add_argument('--output',type=Path)
    args = parser.parse_args()
    try:
        result = analyze(json.loads(args.input.read_text(encoding='utf-8-sig')))
    except (ValueError,OSError) as exc:
        parser.error(str(exc))
    output = json.dumps(result,ensure_ascii=False,indent=2)+'\n'
    if args.output:
        args.output.write_text(output,encoding='utf-8')
    else:
        print(output,end='')

if __name__ == '__main__':
    main()
