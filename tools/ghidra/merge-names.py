# Resolve a names.tsv merge conflict: union by address; prefer non-placeholder names; report real clashes.
import re,sys
p=sys.argv[1]; s=open(p).read()
rows={};header=[];state=0;a=[];b=[];seq=[]
for line in s.splitlines():
    if line.startswith('<<<<<<<'): state=1; continue
    if line.startswith('=======') and state==1: state=2; continue
    if line.startswith('>>>>>>>') and state==2: seq+=a+b; a=[];b=[]; state=0; continue
    (a if state==1 else b if state==2 else seq).append(line)
ph=lambda n: bool(re.match(r'^(crt|real|FUN|sub)_[0-9a-f]+$|_v[0-9]+$',n))
for l in seq:
    if not l.strip(): continue
    if l.startswith('#'):
        if l not in header: header.append(l)
        continue
    k=l.split('\t')[0].lower(); n=l.split('\t')[1]
    if k in rows:
        o=rows[k].split('\t')[1]
        if ph(o) and not ph(n): rows[k]=l
        elif o!=n and not ph(n) and not ph(o): print('CLASH',k,o,n)
        continue
    rows[k]=l
open(p,'w').write('\n'.join(header+[rows[k] for k in sorted(rows)])+'\n'); print(len(rows),'names')
