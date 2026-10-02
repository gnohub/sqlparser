import csv,io,pathlib,subprocess,argparse
p=argparse.ArgumentParser()
p.add_argument("--baseline-bin",type=pathlib.Path,required=True)
p.add_argument("--candidate-bin",type=pathlib.Path,required=True)
p.add_argument("--output-dir",type=pathlib.Path,required=True)
p.add_argument("--cpu",type=int,default=2)
a=p.parse_args()
root=a.output_dir;root.mkdir(parents=True,exist_ok=True)
rows=[]
workloads=['select-filter','select-join','insert-values','update-where','delete-where','create-view','transaction']
cases=[(m,w,n) for m in ['sqlparser-parse','sqlparser-view-json','sqlparser-deparse'] for w in workloads for n in ([256,8192] if w=='transaction' else [128,8192])]
cases += [(m,w,8192) for m,w in [('sqlparser-update-assignment-literal','update-where'),('sqlparser-update-assignment-sql','update-where'),('sqlparser-update-rewrite-deparse','update-where'),('sqlparser-insert-cell-literal','insert-values'),('sqlparser-insert-cell-sql','insert-values'),('sqlparser-insert-rewrite-deparse','insert-values')]]
for mode,workload,length in cases:
 for label,binary in [('baseline',a.baseline_bin.resolve()),('candidate',a.candidate_bin.resolve())]:
  cmd=['taskset','-c',str(a.cpu),str(binary),'--mode',mode,'--workload',workload,'--length-bytes',str(length),'--iterations','101','--warmup','10','--csv-header']
  out=subprocess.run(cmd,text=True,capture_output=True)
  (root/f'related-{label}-{mode}-{workload}-{length}.csv').write_text(out.stdout)
  if out.returncode: raise RuntimeError((cmd,out.stdout,out.stderr))
  for r in csv.DictReader(io.StringIO(out.stdout)): rows.append({'build':label,'dialect':'mysql',**r})
with (root/'related-summary.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
print('related API benchmarks complete',len(rows))
