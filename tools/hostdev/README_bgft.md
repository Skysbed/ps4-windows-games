# Controller-free PKG install (bgft + HTTP)

Installs a .pkg onto the PS4 with no controller, by driving the console's
background-file-transfer (bgft) daemon to download the pkg over HTTP from this Mac.

## Steps
1. Serve the pkg with a RANGE-capable server (the PS4 downloader needs HTTP 206;
   Python's http.server does NOT, so use this one):
       cd <dir with the .pkg> && python3 range_http_server.py   # 0.0.0.0:8099
2. Install (over ps4debug, port 744):
       python3 ps4debug_install_bgft.py <http_url> <content_id> <title_id> <pkg_size>
   It uninstalls the existing title, registers a bgft download task pointed at the
   URL, starts it, and polls. Then launch with ps4debug_launch.py — no controller.

## Hard-won details (fw 13.02, GoldHEN)
- The install daemon runs in its own sandbox; direct sceAppInstUtilAppInstallPkg
  from /data hits EXDEV. bgft (download+install) stages correctly and GoldHEN
  accepts the fake signature.
- bgft register/start/progress are the NON-"Int" `sceBgftServiceDownload*` exports:
  RegisterTask=0x220, StartTask=0x260, GetProgress=0x2a0 (libSceBgft).
- OrbisBgftDownloadParam is 104 bytes; **option MUST be 0** — the
  DISABLE_CDN_QUERY_PARAM flag (0x10000) is rejected on this fw (0x80990004).
- A title already installed → register returns 0x80990015
  (SAME_APPLICATION_ALREADY_INSTALLED); no option bit overrides it, so uninstall
  (sceAppInstUtilAppUnInstall, libSceAppInstUtil+0x3160) first.
- The downloader range-fetches with CDN-style query params; the server must honor
  Range and reply 206.
