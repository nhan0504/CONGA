#!/usr/bin/env bash
set -euo pipefail

# Parameters you may tweak
DUR=${DUR:-30}            # simulation duration (seconds)
FLOWSZ=${FLOWSZ:-131072}  # average flow size in bytes (128KB)
QUEUE=${QUEUE:-droptail}
ENDH=${ENDH:-tcp}
BIN=${BIN:-./htsim}       # path to your htsim binary
EXPT=${EXPT:-2}           # 2 == conga_testbed per your test.h
OUTDIR=${OUTDIR:-results}
PARALLEL_JOBS=${PARALLEL_JOBS:-1}  # set >1 if you have GNU parallel; otherwise keep 1

mkdir -p "$OUTDIR"

workloads=(uniform pareto enterprise datamining)
loads=(0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 0.9 1.0)
policies=(ecmp conga)

# If your tree doesn’t implement enterprise, it will be mapped to pareto in test_conga_testbed.cpp.
# You can still pass --flowdist=enterprise for naming consistency.

run_one () {
  local policy="$1"
  local work="$2"
  local util="$3"
  local tag="${policy}_${work}_U${util}"
  local log="${OUTDIR}/${tag}.log"

  echo "Running ${tag}..."
  "${BIN}" --expt=${EXPT}            --duration=${DUR}            --utilization=${util}            --flowsize=${FLOWSZ}            --queue=${QUEUE}            --endhost=${ENDH}            --flowdist=${work}            --policy=${policy} > "${log}"
}

export -f run_one
export BIN EXPT DUR FLOWSZ QUEUE ENDH OUTDIR

# Sequential fallback (portable)
if [ "${PARALLEL_JOBS}" -le 1 ]; then
  for policy in "${policies[@]}"; do
    for work in "${workloads[@]}"; do
      for util in "${loads[@]}"; do
        run_one "${policy}" "${work}" "${util}"
      done
    done
  done
else
  # Parallel version (requires GNU parallel)
  # Build a job list
  JOBS_FILE="$(mktemp)"
  trap 'rm -f "${JOBS_FILE}"' EXIT
  for policy in "${policies[@]}"; do
    for work in "${workloads[@]}"; do
      for util in "${loads[@]}"; do
        echo "run_one ${policy} ${work} ${util}" >> "${JOBS_FILE}"
      done
    done
  done
  parallel -j "${PARALLEL_JOBS}" < "${JOBS_FILE}"
fi

echo "All runs completed. Logs in ${OUTDIR}/"
