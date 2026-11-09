#!/usr/bin/env python3
"""CodexAstraLocal: Finite incremental retention of compact frame JSONL, never game input.

Import collect_once into the root-owned capture loop, or invoke once per capture cycle.
There is no recorder lifetime cutoff. Local-source mode never calls adb.
"""
import argparse
import base64
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import time

NAME = re.compile(r"azahar_log\.txt\.frames-(\d+)-(\d+)\.jsonl\Z")
CHUNK = 64 * 1024
MAX_LINE = 256 * 1024


class LocalSource:
    def __init__(self, directory):
        self.directory = Path(directory)

    def listing(self):
        return [(p.name, p.stat().st_size) for p in self.directory.iterdir()
                if p.is_file() and NAME.fullmatch(p.name)]

    def read(self, name, offset, count):
        assert NAME.fullmatch(name) and 0 <= count <= CHUNK
        with (self.directory / name).open('rb') as stream:
            stream.seek(offset)
            return stream.read(count)


class AdbSource:
    # CodexAstraLocal: Construction is inert. Only the explicitly selected root CLI
    # mode issues read-only exec-out calls; timeout is per probe, never session duration.
    def __init__(self, serial, directory):
        self.serial, self.directory = serial, directory.rstrip('/')

    def shell(self, command):
        result = subprocess.run(['adb', '-s', self.serial, 'exec-out', 'sh', '-c',
                                 shlex.quote(command)], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=15)
        if result.returncode:
            raise OSError(f'adb read rc={result.returncode}: {result.stderr[:512]!r}')
        return result.stdout

    def listing(self):
        pattern = shlex.quote(self.directory) + '/azahar_log.txt.frames-*-*.jsonl'
        raw = self.shell(f'for f in {pattern}; do [ -f "$f" ] && stat -c "%s %n" "$f"; done; true')
        rows = []
        for line in raw.decode('utf-8', 'strict').splitlines():
            size, path = line.split(' ', 1)
            name = path.rsplit('/', 1)[-1]
            if NAME.fullmatch(name): rows.append((name, int(size)))
        return rows

    def read(self, name, offset, count):
        assert NAME.fullmatch(name) and 0 <= count <= CHUNK
        # Android toybox dd does not need byte-at-a-time IO or GNU-only skip_bytes.
        skip, head = divmod(offset, CHUNK)
        blocks = math.ceil((head + count) / CHUNK)
        command = f'dd if={shlex.quote(self.directory + "/" + name)} bs={CHUNK} skip={skip} count={blocks} 2>/dev/null'
        raw = self.shell(command)
        return raw[head:head + count]


def atomic_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
    temporary.replace(path)


def collect_once(source, destination, max_bytes_per_file=1024 * 1024):
    """One frozen listing; bounded reads, immutable raw segments and explicit quality.

    A backlog continues on the next invocation, never silently truncates a session.
    Caller must serialize invocations for this destination (one capture owner).
    """
    assert max_bytes_per_file > 0
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    state_path = destination / 'STATE.json'
    state = json.loads(state_path.read_text()) if state_path.exists() else {'schema': 1, 'files': {}, 'runs': {}}
    assert state['schema'] == 1
    rows = sorted(source.listing(), key=lambda item: tuple(map(int, NAME.fullmatch(item[0]).groups())))
    report = {'schema': 1, 'started_unix': time.time(), 'files': [], 'events': [], 'event_counts': {}, 'caught_up': True}
    blocked_streams = set()

    def event(kind, **values):
        report['event_counts'][kind] = report['event_counts'].get(kind, 0) + 1
        if len(report['events']) < 128: report['events'].append({'kind': kind, **values})

    for name, size in rows:
        stream_id = NAME.fullmatch(name).group(1)
        if stream_id in blocked_streams:
            report['caught_up'] = False
            continue
        entry = state['files'].get(name)
        old_entry, old_runs = copy.deepcopy(entry), dict(state['runs'])
        try:
            prefix_size = entry['prefix_size'] if entry else min(size, CHUNK)
            prefix = source.read(name, 0, prefix_size) if prefix_size else b''
            prefix_hash = hashlib.sha256(prefix).hexdigest()
            replaced = entry and (size < entry['offset'] or len(prefix) != prefix_size or
                                 prefix_hash != entry['prefix_sha256'])
            if replaced:
                event('source_replaced_or_truncated', source=name, previous_offset=entry['offset'], new_size=size)
                entry = None
            if entry is None:
                prefix_size = min(size, CHUNK)
                prefix = source.read(name, 0, prefix_size) if prefix_size else b''
                entry = {'offset': 0, 'prefix_size': prefix_size,
                         'prefix_sha256': hashlib.sha256(prefix).hexdigest(), 'tail': '',
                         'discarding_oversize': False, 'segment': '', 'segment_start': 0}
                state['files'][name] = entry
            elif entry['prefix_size'] == 0 and size:
                entry['prefix_size'] = min(size, CHUNK)
                entry['prefix_sha256'] = hashlib.sha256(source.read(name, 0, entry['prefix_size'])).hexdigest()
            raw_path = destination / entry['segment'] if entry['segment'] else None
            if raw_path is None or not raw_path.exists() or raw_path.stat().st_size != entry['offset'] - entry['segment_start']:
                if raw_path is not None: event('retained_segment_size_mismatch', source=name, offset=entry['offset'])
                index = 0
                while (destination / f'{name}.segment{index:04}.raw').exists(): index += 1
                raw_path = destination / f'{name}.segment{index:04}.raw'
                raw_path.touch(exist_ok=False)
                entry['segment'], entry['segment_start'] = raw_path.name, entry['offset']
            target = min(size, entry['offset'] + max_bytes_per_file)
            span_begin, span_hash = entry['offset'], hashlib.sha256()
            with raw_path.open('ab') as output:
                while entry['offset'] < target:
                    data = source.read(name, entry['offset'], min(CHUNK, target - entry['offset']))
                    if not data:
                        event('source_read_gap', source=name, offset=entry['offset'], expected_end=target)
                        break
                    output.write(data)
                    span_hash.update(data)
                    entry['offset'] += len(data)
                    buffer = base64.b64decode(entry['tail']) + data
                    complete = buffer.split(b'\n')
                    for line in complete[:-1]:
                        if entry['discarding_oversize']:
                            entry['discarding_oversize'] = False
                            continue
                        if len(line) > MAX_LINE:
                            event('oversized_record_retained_raw', source=name)
                            continue
                        try:
                            record = json.loads(line)
                            if record.get('kind') in ('context', 'window', 'final'):
                                run = str(record['run_ns']) + ':' + str(record['session'])
                                sequence = int(record['sequence'])
                                previous = state['runs'].get(run, 0)
                                if sequence != previous + 1:
                                    event('record_sequence_gap_or_overlap', run=run, previous=previous, current=sequence)
                                state['runs'][run] = sequence
                            elif record.get('kind') == 'delivery':
                                entry['delivery'] = record
                        except (ValueError, KeyError, TypeError) as error:
                            event('invalid_complete_record_retained_raw', source=name, error=str(error)[:160])
                    tail = complete[-1]
                    if len(tail) > MAX_LINE:
                        event('oversized_partial_retained_raw', source=name)
                        entry['discarding_oversize'] = True
                        tail = b''
                    entry['tail'] = base64.b64encode(tail).decode('ascii')
                output.flush(); os.fsync(output.fileno())
            caught_up = entry['offset'] == size
            if not caught_up:
                blocked_streams.add(stream_id); report['caught_up'] = False
            report['files'].append({'source': name, 'frozen_size': size, 'retained_offset': entry['offset'],
                'segment': entry['segment'], 'segment_start': entry['segment_start'],
                'span_begin': span_begin, 'span_end': entry['offset'], 'span_sha256': span_hash.hexdigest(),
                'partial_tail_bytes': len(base64.b64decode(entry['tail'])),
                'caught_up': caught_up, 'delivery': entry.get('delivery')})
        except (OSError, subprocess.SubprocessError, ValueError) as error:
            if old_entry is None: state['files'].pop(name, None)
            else: state['files'][name] = old_entry
            state['runs'] = old_runs
            report['caught_up'] = False; blocked_streams.add(stream_id)
            event('probe_failed', source=name, error=str(error)[:300])
    present = {name for name, _ in rows}
    for name in state['files'].keys() - present:
        event('source_absent_archive_preserved', source=name)
    # CodexAstraLocal: Raw bytes are flushed before offsets commit. A crash leaving
    # extra bytes creates another segment next time instead of truncating evidence.
    atomic_json(state_path, state)
    report['finished_unix'] = time.time()
    with (destination / 'RETENTION.jsonl').open('a') as stream:
        stream.write(json.dumps(report) + '\n')
    atomic_json(destination / 'LATEST.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument('--source-dir', type=Path)
    modes.add_argument('--serial')
    parser.add_argument('--remote-dir')
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--max-bytes-per-file', type=int, default=1024 * 1024)
    args = parser.parse_args()
    if args.serial and not args.remote_dir: parser.error('--serial requires --remote-dir')
    source = LocalSource(args.source_dir) if args.source_dir else AdbSource(args.serial, args.remote_dir)
    report = collect_once(source, args.destination, args.max_bytes_per_file)
    print(json.dumps({'report': str(args.destination / 'LATEST.json'), 'caught_up': report['caught_up'],
                      'events': len(report['events'])}))


if __name__ == '__main__':
    main()
