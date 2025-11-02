
#!/usr/bin/env python3

import re, os, sys, math, glob
import pandas as pd
import matplotlib.pyplot as plt

# --- Config ---
LOG_DIR = sys.argv[1] if len(sys.argv) > 1 else "results"
OUT_DIR = sys.argv[2] if len(sys.argv) > 2 else "plots"
os.makedirs(OUT_DIR, exist_ok=True)

# Flow line pattern from your logs, e.g.:
# Flow conga-src24504104 98020668 size 10000 start 1276075 end 1276113 fct 38.2858 sent 10500 0 tput 2.08955 rtt 14.263 cwnd 12000 alpha 0
FLOW_RE = re.compile(
    r'^Flow\s+\S+\s+\d+\s+size\s+(?P<size>\d+)\s+start\s+(?P<start>\d+)\s+end\s+(?P<end>\d+)\s+fct\s+(?P<fct>[\d\.eE\+\-]+)'
)

# Extract policy/workload/utilization from filename pattern: {policy}_{workload}_U{util}.log
NAME_RE = re.compile(r'(?P<policy>ecmp|conga)_(?P<workload>uniform|pareto|enterprise|datamining)_U(?P<util>[0-9.]+)\.log$')

rows = []
for path in glob.glob(os.path.join(LOG_DIR, "*.log")):
    base = os.path.basename(path)
    m = NAME_RE.search(base)
    if not m:
        continue
    policy = m.group("policy")
    workload = m.group("workload")
    util = float(m.group("util"))

    with open(path, "r", errors="ignore") as f:
        for line in f:
            fm = FLOW_RE.search(line)
            if fm:
                size = int(fm.group("size"))
                fct_us = float(fm.group("fct"))   # appears to be microseconds in your output
                # Derive small/large bucket
                small = (size < 100*1024)
                rows.append({
                    "policy": policy,
                    "workload": workload,
                    "util": util,
                    "size_bytes": size,
                    "fct_us": fct_us,
                    "bucket": "small" if small else "large"
                })

df = pd.DataFrame(rows)
if df.empty:
    print("No flow lines parsed. Check LOG_DIR and filename pattern.")
    sys.exit(1)

# Aggregate means (you can add medians/tails similarly)
def agg_curve(dsub, label):
    g = dsub.groupby(["policy","workload","util"], as_index=False)["fct_us"].mean()
    g["curve"] = label
    return g

all_curve    = agg_curve(df, "all")
small_curve  = agg_curve(df[df["bucket"]=="small"], "small")
large_curve  = agg_curve(df[df["bucket"]=="large"], "large")

curves = {"all": all_curve, "small": small_curve, "large": large_curve}

# Save CSVs
for k, g in curves.items():
    g.sort_values(["workload","policy","util"]).to_csv(os.path.join(OUT_DIR, f"mean_fct_{k}.csv"), index=False)

# --- Plotting: 12 figures (3 per workload) ---
workloads = ["uniform", "pareto", "enterprise", "datamining"]
buckets   = ["all", "small", "large"]

for w in workloads:
    for b in buckets:
        g = curves[b]
        sub = g[g["workload"]==w].copy()
        if sub.empty:
            continue
        # pivot to x=util, line per policy
        # keep util sorted
        util_vals = sorted(sub["util"].unique())
        plt.figure()
        for policy in ["ecmp","conga"]:
            y = []
            for u in util_vals:
                yy = sub[(sub["policy"]==policy) & (sub["util"]==u)]["fct_us"].mean()
                y.append(yy if not math.isnan(yy) else float('nan'))
            plt.plot([int(u*100) for u in util_vals], y, marker='o', label=policy.upper())
        plt.xlabel("Offered load (%)")
        plt.ylabel("Average FCT (µs)")
        plt.title(f"{w} workload — {b} flows")
        plt.legend()
        plt.grid(True, linestyle='--', linewidth=0.5)
        out = os.path.join(OUT_DIR, f"{w}_{b}_fct.png")
        plt.savefig(out, bbox_inches="tight", dpi=160)
        plt.close()

print(f"Done. Plots and CSVs in: {OUT_DIR}")
