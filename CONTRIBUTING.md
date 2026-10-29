# Contributing

Early project — every careful hand helps. PRs welcome.

## Where to start

1. Read [WINE_PS4_STATUS.md](WINE_PS4_STATUS.md) — it is the living log of every
   milestone, fix and blocker, written to save you the archaeology.
2. `docs` issues list the known next steps (currently: the NLS-file open from
   read-only `/app0` PFS in wineserver).

## Ground rules

- **Test on hardware when you claim hardware.** A fix without a klog trace or
  boot log is a hypothesis; note which one it is.
- **Small diffs win.** This port has many invariants in delicate places (16 KiB
  segment alignment, no-TLS PRX build, the in-process server locking). A
  drive-by refactor of working code is worse than a slow, careful fix.
- **Reproduce from the recipe.** `WINE_PS4_STATUS.md` documents the exact
  module build commands; if you needed a step it doesn't have, add it to the
  doc in the same PR.

## Hardware notes

You need a jailbroken PS4 with [GoldHEN](https://github.com/GoldHEN/GoldHEN)'s
FTP and klog plugins. Never commit your console's IP — scripts here read it
from `PCPS4_CONSOLE_IP` (see [README](README.md)); keep it that way.
