# Process performance measurements

Linux process runtime and peak resident memory, measured separately for each run with wait4.
This measures startup, the requested workload and shutdown together. It does not measure
frame-time percentiles, GPU memory, Android performance, or readiness for 3 GB devices.
Use a fixed workload and the same build, machine and settings when comparing reports.
Inherited child-process output stays in the caller's terminal; JSON contains no environment dump.

```sh
python3 Engine/Tools/performance/measure_process.py --runs 3 --timeout 60 --output /tmp/editor-process.json -- build/Engine/App/uve_editor --headless --frames 300
python3 -m unittest discover -s Engine/Tools/performance -p 'test_*.py'
```

An optional `--max-peak-mib` rejects a run whose peak process RSS exceeds a chosen budget.
There is deliberately no default device budget: available application memory is not the
same as total device RAM. Timeout, a failed child, or exceeding the budget produces exit
code 1 and a failed report. All processes in the workload's process group are stopped at
completion or timeout. Do not use this tool for workloads that deliberately daemonize.

## Frame-time percentiles

`frame_report.py` measures frame time rather than process runtime. The engine writes one CSV
row per frame when `UVE_FRAME_TRACE` names a path (bounded by `UVE_FRAME_TRACE_MAX`, default
20000 rows, written once at shutdown); the tool runs the command, collects those rows and
reports p50/p95/p99, 1% low FPS and hitches over 2x the median, split by update, late update
and render.

```sh
python3 Engine/Tools/performance/frame_report.py --runs 3 --warmup-frames 60 --min-fps 60 \
  --output /tmp/frame-time.json -- build/Engine/App/uve_runtime --headless --frames 1200
python3 Engine/Tools/performance/frame_report.py --runs 1 --output /tmp/after.json \
  --baseline /tmp/before.json --max-regression-percent 10 -- <command>
```

`--warmup-frames` drops startup frames (shader compilation, asset streaming) so the percentiles
describe steady state. Compare reports from the same build configuration, machine and workload;
the null render device measures CPU frame cost only and software GL measures neither GPU nor
device behaviour.
