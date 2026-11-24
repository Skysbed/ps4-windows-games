# On-console tests

Small Windows programs that run on the PS4 through PCPS4 and write `result.txt` beside their tile, ending with a `DONE` line.

| Test | What it checks |
|---|---|
| `archtest` | Thread suspension, CPU faults and exception codes |
| `fsprobe` | The `FS` selector across system calls, faults and pauses |
| `systest` | File locks, clocks, TCP, UDP, socket options and open files |
| `gsring` | Geometry shaders and GS ring growth |
| `heaptest` | Reported Vulkan memory, budgets and usage |
| `drawbench` | CPU cost of Vulkan draws |
| `d3dbench` | CPU cost of Direct3D 11 calls through DXVK |
| `memplace` | CPU and GPU speed on Garlic and Onion |
| `memstress` | No data loss when memory is reused across Garlic and Onion |
| `pooltest` | A large GPU pool filled with verified data |
| `fastmem` | `ps4_fastmem.c` against reference loops, on the build host |
| `fchdir` | A reused descriptor cannot move the working directory, on the build host |

Build with `x86_64-w64-mingw32-gcc`, adding Mesa's `include` directory for the Vulkan headers. Vulkan tests must create a window before loading `winevulkan.dll`.

Each test needs a tile at `/data/pcps4/games/<name>/pcps4.txt`. The launcher reads tiles when it starts.

Set `PS4_HOST` to your console's address, then run:

```bash
python3 tests/tools/run_ps4_test.py archtest 60
```

`tests/tools/module_ab.py` compares two builds of a module with alternating runs.
