#!/usr/bin/env python3
import os, re, glob, sys
import numpy as np
import matplotlib.pyplot as plt

if len(sys.argv) < 3:
    print("Usage: python3 parse_and_plot.py <LOG_DIR> <PLOT_DIR>")
    sys.exit(1)
LOG_DIR, PLOT_DIR = sys.argv[1], sys.argv[2]
os.makedirs(PLOT_DIR, exist_ok=True)

FLOW_RE = re.compile(
    r'Flow\s+\S+\s+\d+\s+size\s+(?P<size>\d+).*?fct\s+(?P<fct>[0-9\.eE\+\-]+)',
    re.IGNORECASE
)

def parse_logs():
    data = {}  # (policy, workload, util) -> list[(size,fct)]
    for fn in glob.glob(os.path.join(LOG_DIR, "*.log")):
        base = os.path.basename(fn).lower()
        policy = "conga" if "conga" in base else "ecmp"
        workload = next((w for w in ["uniform","pareto","enterprise","datamining"] if w in base), "unknown")
        m = re.search(r'u([0-9.]+)', base)
        if m:
            # strip trailing dots or underscores just in case
            val = m.group(1).rstrip(".")
            try:
                util = float(val)
            except ValueError:
                util = 0.0
        else:
            util = 0.0
        key = (policy, workload, util)
        data.setdefault(key, [])
        with open(fn) as f:
            for line in f:
                m = FLOW_RE.search(line)
                if m:
                    size = int(m.group("size"))
                    fct = float(m.group("fct"))
                    data[key].append((size, fct))
    print(f"Parsed {sum(len(v) for v in data.values())} flows from {LOG_DIR}")
    return data

def mean_fct(flows, sel):
    if not flows: return 0
    if sel=="all":   vals=[fct for _,fct in flows]
    elif sel=="small": vals=[fct for s,fct in flows if s<100*1024]
    else: vals=[fct for s,fct in flows if s>=100*1024]
    return np.mean(vals) if vals else 0

def plot_workload(data, workload):
    loads = sorted({u for (_,w,u) in data.keys() if w==workload})
    if not loads: return
    metrics = ["all","small","large"]
    for metric in metrics:
        ecmp = [mean_fct(data.get(("ecmp",workload,u),[]),metric) for u in loads]
        conga= [mean_fct(data.get(("conga",workload,u),[]),metric) for u in loads]
        plt.figure(figsize=(6,4))
        plt.plot(np.array(loads)*100, ecmp, "o-", label="ECMP")
        plt.plot(np.array(loads)*100, conga,"s-", label="CONGA")
        plt.xlabel("Offered Load (%)")
        plt.ylabel(f"Average FCT ({metric})")
        plt.title(f"{workload.capitalize()} — {metric}")
        plt.legend()
        plt.grid(True, ls="--", alpha=0.5)
        fname = f"{workload}_{metric}.png"
        plt.tight_layout()
        plt.savefig(os.path.join(PLOT_DIR, fname))
        plt.close()
        print(f"Saved {fname}")

data = parse_logs()
for w in ["uniform","pareto","enterprise","datamining"]:
    plot_workload(data, w)
print(f"Plots saved in {PLOT_DIR}")
