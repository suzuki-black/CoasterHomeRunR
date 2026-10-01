"""fillbench_stats.py — fillbench_log.tcl の spans.txt を集計する

塗り 1 回の時間を「呼び出し・行・空でない行・ブロック」の 1 次式で当てはめ(最小二乗)、
窓ごとに 1 フレームあたりの呼び出し・行・ブロック・奇数の端の数と塗りの時間を出す。
    python3 tools/fillbench_stats.py spans.txt
"""
import sys
def solve(A,b):  # 正規方程式をガウスの消去法で解く
    n=len(A[0]); M=[[sum(A[k][i]*A[k][j] for k in range(len(A))) for j in range(n)]+[sum(A[k][i]*b[k] for k in range(len(A)))] for i in range(n)]
    for i in range(n):
        p=max(range(i,n),key=lambda r:abs(M[r][i])); M[i],M[p]=M[p],M[i]
        for r in range(n):
            if r!=i:
                f=M[r][i]/M[i][i]; M[r]=[x-f*y for x,y in zip(M[r],M[i])]
    return [M[i][n]/M[i][i] for i in range(n)]
X=[];Y=[];frames={};win=None;fi=-1
for l in open(sys.argv[1]):
    t=l.split()
    if t[0]=='W': win=t[1]; continue
    if t[0]=='F': fi+=1; continue
    r0,n,col=int(t[1]),int(t[2]),int(t[3]); v=list(map(int,t[4:4+2*n])); us=float(t[4+2*n])
    rows=blocks=odd=0;ws=[]
    for i in range(n):
        a,b=v[2*i],v[2*i+1]
        if a==b: continue
        lo,hi=min(a,b),max(a,b); rows+=1; blocks+=hi-lo; odd+=(lo&1)+(hi&1); ws.append(hi-lo)
    X.append([1,n,rows,blocks]);Y.append(us)
    d=frames.setdefault((win,fi),dict(calls=0,lines=0,rows=0,blocks=0,odd=0,t=0,ws=[]))
    d['calls']+=1;d['lines']+=n;d['rows']+=rows;d['blocks']+=blocks;d['odd']+=odd;d['t']+=us;d['ws']+=ws
c=solve(X,Y)
print("per call %.2fus, per line %.2fus, per nonempty row %.2fus, per block %.3fus"%tuple(c))
print("R800 cycles: per block %.1f, per row %.1f, per call %.1f"%(c[3]*7.16,c[2]*7.16,c[0]*7.16))
pred=[sum(a*b for a,b in zip(x,c)) for x in X]; m=sum(Y)/len(Y)
print("fit r2 %.4f, max abs err %.1fus"%(1-sum((y-p)**2 for y,p in zip(Y,pred))/sum((y-m)**2 for y in Y), max(abs(y-p) for y,p in zip(Y,pred))))
for w in sorted(set(k[0] for k in frames)):
    fs=[d for (ww,_),d in frames.items() if ww==w]
    k=len(fs); a=lambda key: sum(f[key] for f in fs)/k
    ws=[x for f in fs for x in f['ws']]
    print("win",w,"frames",k,"calls %.0f lines %.0f rows %.0f blocks %.0f odd-ends %.0f fill %.2fms meanw %.1f w<=2: %.0f%%"%(a('calls'),a('lines'),a('rows'),a('blocks'),a('odd'),a('t')/1000,sum(ws)/len(ws),100*sum(1 for x in ws if x<=2)/len(ws)))
