import re,os,glob,collections,json
root=os.path.expanduser("~/openba-review/src/")
pat=re.compile(r"^(/\S+?):(\d+):(\d+): (warning|error): (.*) \[([^\]]+)\]$")
seen={}
for fn in sorted(glob.glob("out/chunk*.txt")):
    for line in open(fn,errors="replace"):
        m=pat.match(line.rstrip())
        if not m: continue
        f,l,c,sev,msg,chk=m.groups()
        f=os.path.normpath(f)
        key=(f,l,c,chk)
        if key not in seen: seen[key]=msg
def mod(f):
    if not f.startswith(root): return "other"
    r=f[len(root):].split("/")
    return r[0]+"/"+(r[1] if len(r)>2 else "(root)")
rows=[dict(file=k[0],line=int(k[1]),col=int(k[2]),check=k[3],msg=v,mod=mod(k[0])) for k,v in seen.items()]
json.dump(rows,open("hits.json","w"))
tot=collections.Counter(r["check"] for r in rows)
print(len(rows))
for k,v in tot.most_common(): print(v,k)
