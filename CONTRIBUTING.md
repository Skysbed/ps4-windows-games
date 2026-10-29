# Contributing

The project is in an early, experimental stage. Pull requests are welcome.

## Where to start

1. Read [WINE_PS4_STATUS.md](WINE_PS4_STATUS.md) — the log of every milestone,
   fix and blocker, written to save you the archaeology. The final section
   describes the current blocker.
2. The issue tracker tracks active work items as they are identified.

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
FTP and klog plugins for on-console testing.

Never commit a console address — scripts in this repository read the address
from `PCPS4_CONSOLE_IP` at runtime (see [README](README.md)).
