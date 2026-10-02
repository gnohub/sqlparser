#!/usr/bin/env python3
"""Run public-API pipeline comparisons sequentially, retaining every sample.

Compile the same tools/sqlparser_pipeline_bench.c against the two static
libraries first. This runner never builds or modifies either library.
"""
import argparse
import csv
import hashlib
import json
import math
import pathlib
import statistics
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline-bin', type=pathlib.Path, required=True)
p.add_argument('--candidate-bin', type=pathlib.Path, required=True)
p.add_argument('--output-dir', type=pathlib.Path, required=True)
p.add_argument('--cpu', type=int, default=2)
p.add_argument('--dialect', choices=('mysql','postgresql'), default='mysql')
p.add_argument('--runs', type=int, default=31)
p.add_argument('--warmups', type=int, default=5)
p.add_argument('--slow-sql-runs', type=int, default=7)
p.add_argument('--slow-sql-warmups', type=int, default=2)
p.add_argument('--reuse', action='store_true', help='Reuse existing complete raw CSVs')
p.add_argument('--quick', action='store_true', help='Omit slow baseline 5000-row SQL run')
a = p.parse_args()
a.output_dir.mkdir(parents=True, exist_ok=True)
# The three failure/no-op cases are explicit correctness checks in the harness.
cases = [(n, mode) for n in (1, 10, 100, 1000, 5000) for mode in ('literal', 'replace')]
cases += [(n, mode) for n in (10, 100) for mode in ('unchanged', 'noop', 'invalid')]
cases += [(5000, 'unchanged')]
summary = []
source = pathlib.Path(__file__).resolve().parent.parent / 'tools/sqlparser_pipeline_bench.c'
source_sha = hashlib.sha256(source.read_bytes()).hexdigest()
binary_shas = {str(b.resolve()): hashlib.sha256(b.read_bytes()).hexdigest() for b in (a.baseline_bin,a.candidate_bin)}
for rows, mode in cases:
    for label, binary in (('baseline', a.baseline_bin), ('candidate', a.candidate_bin)):
        if a.quick and label == 'baseline' and rows == 5000 and mode == 'replace':
            continue
        slow = label == 'baseline' and rows == 5000 and mode == 'replace'
        runs = a.slow_sql_runs if slow else a.runs
        warmups = a.slow_sql_warmups if slow else a.warmups
        path = a.output_dir / f'{label}-{a.dialect}-{rows}-{mode}.csv'
        meta_path = path.with_suffix('.meta.json')
        metadata = dict(source_sha256=source_sha, binary_sha256=binary_shas[str(binary.resolve())],
                        rows=rows, mode=mode, dialect=a.dialect, runs=runs, warmups=warmups, cpu=a.cpu)
        records = []
        if a.reuse and path.exists() and meta_path.exists() and json.loads(meta_path.read_text()) == metadata:
            with path.open() as f:
                records = list(csv.DictReader(f))
        if len(records) != runs:
            print(f'{label}: rows={rows}, mode={mode}, runs={runs}, warmups={warmups}', flush=True)
            with path.open('w') as f:
                subprocess.run(['taskset', '-c', str(a.cpu), str(binary.resolve()), str(rows),
                                str(runs), str(warmups), mode, a.dialect], stdout=f, check=True)
            with path.open() as f:
                records = list(csv.DictReader(f))
        meta_path.write_text(json.dumps(metadata,indent=2)+'\n')
        if len(records) != runs:
            raise RuntimeError(f'Incomplete measurements: {path}')
        for stage in [key for key in records[0] if key.endswith('_ms')]:
            values = sorted(float(record[stage]) for record in records)
            summary.append(dict(build=label, dialect=a.dialect, rows=rows, mode=mode, stage=stage,
                                runs=runs, warmups=warmups, median_ms=statistics.median(values),
                                p95_ms=values[math.ceil(.95*len(values))-1],
                                min_ms=values[0], max_ms=values[-1]))
with (a.output_dir / 'summary.csv').open('w') as f:
    writer=csv.DictWriter(f, fieldnames=list(summary[0]))
    writer.writeheader(); writer.writerows(summary)
print(a.output_dir / 'summary.csv')
