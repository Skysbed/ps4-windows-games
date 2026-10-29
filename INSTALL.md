# Trying PCPS4 on your PS4 (experimental)

**Read this first:** PCPS4 is an early research runtime. The current build boots
Wine (ntdll loads, the prefix is created, the in-process wineserver starts) and
then stops at the locale-file module — **no Windows program runs yet.** If you
install it, you are a tester: the logs you collect are the contribution.

## What you need

- A **jailbroken PS4** running [GoldHEN](https://github.com/GoldHEN/GoldHEN)
  with the **FTP** and **klog** plugins enabled (both ship with GoldHEN).
- A PC on the same network as the console (for logs and FTP).
- A few hundred MB free in the console's package storage.

## Install

1. Download the current `.pkg` from this repository's
   [Releases](../../releases) (or pull it over FTP to `/data/pkg/`).
2. On the PS4: open **GoldHEN → Package Installer** and install the package
   ("PcPs4 Wine FirstLight").
3. If a previous test build is installed, **delete that tile first** — same
   CONTENT_ID, or the installer refuses.

## Collect logs (the useful part)

Set your console address in the environment on the PC — the tools never
hardcode it:

```bash
export PCPS4_CONSOLE_IP=192.168.1.n   # REPLACE with your PS4's LAN address
python3 build-ps4-module/klog_capture.py klog.txt 120
```

Then launch the app on the PS4. Two logs result:

| File | How to get it | Contents |
|---|---|---|
| `klog.txt` | the command above, while and after launch | kernel-level + `pcps4:` trace lines, incl. the current blocker diagnostic |
| `pcps4_wine_boot.txt` (copy it via FTP from `/data/`) | any FTP client | bootstrap stages: `wine_start_load`, `stage`, `environment`, `run` |

## What the current blocker looks like

In `klog.txt`, expect (`WINE_PS4_STATUS.md` explains the whole chain):

```
pcps4: nls try [/app0/sce_module/nls/l_intl.nls] rawopen=.. errno=..
```

That single line decides the fix (raw open fails ⇒ PFS access issue; raw open
succeeds ⇒ server `open_fd` handling); wiggle it, and [open an
issue](../../issues) with the klog text attached (strip any addresses you'd
rather not share).

## Known limitations

- No Windows programs execute yet; no graphics pipeline on-screen (RADV/GNM
  winsys is being wired, see status file).
- Only fw 13.02-era homebrew setups have been exercised.
- Expect crashes; the app is designed to die loudly and legibly.

## Safety and etiquette

- All experiments are your own; treat non-app consoles as yours to manage.
- Please **redact IPs/addresses** from logs before posting them.
