#!/usr/bin/env python3
"""Compare ordinary MySQL SELECT/UPDATE/DELETE full pipelines with 0/1/few patches."""
import argparse,csv,hashlib,json,math,pathlib,statistics,subprocess
p=argparse.ArgumentParser()
p.add_argument('--baseline-bin',type=pathlib.Path,required=True)
p.add_argument('--candidate-bin',type=pathlib.Path,required=True)
p.add_argument('--output-dir',type=pathlib.Path,required=True)
p.add_argument('--cpu',type=int,default=2)
p.add_argument('--runs',type=int,default=201)
p.add_argument('--warmups',type=int,default=20)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
summary=[]
for workload,few in [('select',2),('select_join',2),('update',3),('delete',2)]:
 for count in [0,1,few]:
  for label,binary in [('baseline',a.baseline_bin),('candidate',a.candidate_bin)]:
   path=a.output_dir/f'{label}-{workload}-{count}.csv'
   with path.open('w') as f:subprocess.run(['taskset','-c',str(a.cpu),str(binary.resolve()),workload,str(count),str(a.runs),str(a.warmups)],stdout=f,check=True)
   rows=list(csv.DictReader(path.open()))
   for stage in [k for k in rows[0] if k.endswith('_ms')]:
    v=sorted(float(r[stage]) for r in rows)
    summary.append(dict(build=label,workload=workload,patches=count,stage=stage,runs=len(v),median_ms=statistics.median(v),p95_ms=v[math.ceil(.95*len(v))-1],max_ms=max(v)))
with (a.output_dir/'summary.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(summary[0]));w.writeheader();w.writerows(summary)
meta={'dialect':'mysql','cpu':a.cpu,'runs':a.runs,'warmups':a.warmups,'binaries':{str(b.resolve()):hashlib.sha256(b.read_bytes()).hexdigest() for b in [a.baseline_bin,a.candidate_bin]}}
(a.output_dir/'provenance.json').write_text(json.dumps(meta,indent=2)+'\n')
print('Common MySQL pipeline comparison complete: 12 paired workloads')
