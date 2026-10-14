#!/usr/bin/env bash
# Hands-free run on the PS4 (needs the runner app open, see compat/pcps4_runner.c).
#   tools/ps4_run.sh <core.prx> <label>   upload the module as /data/pcps4/core_<label>.prx and run it
#   tools/ps4_run.sh --builtin            rerun the code built into the app
# Waits for /data/pcps4/done to change, then prints the runner log tail.
set -uo pipefail
CONSOLE="${PCPS4_FTP:-ftp://PCPS4_CONSOLE_IP:2121}"
# The app creates /data/pcps4 without write access for the FTP server.
curl -s -Q "SITE CHMOD 777 /data/pcps4" "$CONSOLE/" -o /dev/null
if [ "${1:-}" = "--builtin" ]; then
  REMOTE=""
else
  PRX="${1:?core.prx}"; LABEL="${2:?label}"
  REMOTE="/data/pcps4/core_${LABEL}.prx"
  curl -sS -T "$PRX" "$CONSOLE$REMOTE" || exit 1
fi
before="$(curl -s "$CONSOLE/data/pcps4/done")"
printf '%s\n' "$REMOTE" > /tmp/pcps4_trigger
curl -sS -T /tmp/pcps4_trigger "$CONSOLE/data/pcps4/trigger" || exit 1
echo "triggered ${REMOTE:-<built-in>}"
for i in $(seq 1 300); do
  sleep 2
  now="$(curl -s "$CONSOLE/data/pcps4/done")"
  if [ -n "$now" ] && [ "$now" != "$before" ]; then echo "done: $now"; break; fi
done
curl -s "$CONSOLE/data/pcps4_runner_log.txt" | tail -8
