# Installing PCPS4 on a jailbroken PS4

PCPS4 is an early-stage compatibility runtime. The current build initializes
Wine's Unix side on the console (module load, prefix creation, in-process
wineserver startup) and stops during NLS table loading. Windows programs do not
run in this build. The diagnostic output below documents that failure point.

## Requirements

- A jailbroken PS4 running [GoldHEN](https://github.com/GoldHEN/GoldHEN), with
  the **FTP** and **klog** plugins enabled.
- A PC on the same network, to install packages over FTP and collect klog
  output.
- About 100 MB of storage space for the package.

## Installation

1. Download the current `.pkg` from the
   [Releases](../../releases) page.
2. Copy the package over FTP to `/data/pkg/` on the console.
3. Open **GoldHEN → Package Installer** and install the package.
4. If an older build of the same title is installed, delete the installed
   title first: all builds share one CONTENT_ID, and the installer rejects a
   duplicate.

## Running and collecting logs

Set the console address in the environment before using the tools in this
repository; none of them hardcode an address:

```bash
export PCPS4_CONSOLE_IP=192.168.1.n   # the PS4's address on your LAN
```

Launch the installed title ("PcPs4 Wine FirstLight") while klog capture is
running:

| Log | Source | Contents |
|---|---|---|
| `klog.txt` | `python3 build-ps4-module/klog_capture.py klog.txt 120`, started before launch | Kernel and plugin output, including the diagnostic lines below |
| `/data/pcps4_wine_boot.txt` | copied over FTP after launch | Bootstrap stages: `wine_start_load`, `stage`, `environment`, `run` |

## Expected diagnostic output

The current build fails in `load_intl_file()`, which opens the NLS tables from
the read-only `/app0` PFS. klog contains the path, the return value of a raw
`open()`, and `errno`:

```
pcps4: nls try [/app0/sce_module/nls/l_intl.nls] rawopen=.. errno=..
```

A negative `rawopen` means the console refused the open (PFS access problem);
a non-negative value with a later failure points to wineserver's `open_fd`
handling. Distinguishing the two cases determines the fix.

## Known limitations

- No Windows programs execute in this build.
- No graphics output yet; the RADV/GNM winsys is being wired (see
  [WINE_PS4_STATUS.md](WINE_PS4_STATUS.md)).
- Validated only on firmware 13.02-era homebrew setups.

## Reporting issues

Open an [issue](../../issues) with the klog capture attached. Remove or redact
network addresses before posting.
