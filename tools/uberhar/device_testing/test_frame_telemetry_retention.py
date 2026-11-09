#!/usr/bin/env python3
"""CodexAstraLocal: Actual incremental retention using local fixture files only; no adb."""
import hashlib
import json
from pathlib import Path
import tempfile
from retain_frame_telemetry import LocalSource, collect_once


def record(seq):
    return json.dumps({'schema': 1, 'kind': 'context' if seq == 1 else 'window',
                       'run_ns': 99, 'session': 1, 'sequence': seq}).encode() + b'\n'


with tempfile.TemporaryDirectory(prefix='uberhar-compact-retention-') as folder:
    root = Path(folder); source = root / 'source'; source.mkdir(); output = root / 'retained'
    name = 'azahar_log.txt.frames-100-000000.jsonl'
    path = source / name
    first, second = record(1), record(2)
    path.write_bytes(first + second[:12])
    a = collect_once(LocalSource(source), output)
    assert a['caught_up'] and a['files'][0]['partial_tail_bytes'] == 12
    raw = output / a['files'][0]['segment']
    assert raw.read_bytes() == path.read_bytes()
    with path.open('ab') as stream: stream.write(second[12:] + record(3))
    b = collect_once(LocalSource(source), output)
    assert not b['events'] and b['files'][0]['partial_tail_bytes'] == 0
    assert b['files'][0]['span_begin'] == len(first) + 12
    assert b['files'][0]['span_sha256'] == hashlib.sha256(second[12:] + record(3)).hexdigest()
    assert raw.read_bytes() == path.read_bytes()
    c = collect_once(LocalSource(source), output)
    assert c['files'][0]['span_begin'] == c['files'][0]['span_end']
    # Bounded invocation budget is a backlog, never a recorder/session cutoff.
    other = source / 'azahar_log.txt.frames-101-000000.jsonl'
    other.write_bytes(record(1).replace(b'99', b'100'))
    d = collect_once(LocalSource(source), output, max_bytes_per_file=10)
    assert not d['caught_up'] and d['files'][-1]['retained_offset'] == 10
    e = collect_once(LocalSource(source), output)
    assert e['caught_up']
    original = raw.read_bytes()
    path.write_bytes(record(1))
    f = collect_once(LocalSource(source), output)
    assert f['event_counts']['source_replaced_or_truncated'] == 1
    assert raw.read_bytes() == original
    assert f['files'][0]['segment'] != a['files'][0]['segment']
    assert (output / 'STATE.json').is_file() and (output / 'RETENTION.jsonl').is_file()
print('PASS: bounded local retention, append offsets/hash, partial framing, backlog, replacement preservation; no device')
