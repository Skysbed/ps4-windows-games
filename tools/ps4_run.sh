#!/usr/bin/env bash
# Hands-free run on the PS4 (needs the runner app open, see compat/pcps4_runner.c).
#   tools/ps4_run.sh <core.prx> <label>   upload the module as /data/pcps4/core_<label>.prx and run it
#   tools/ps4_run.sh --builtin            rerun the code built into the app
# Waits for /data/pcps4/done to change, then prints the runner log tail.
set -euo pipefail
PS4="${PS4:-ftp://PCPS4_CONSOLE_IP:2121}"
if [ "${1:-}" = "--builtin" ]; then
  REMOTE=""
else
  PRX="${1:?core.prx}"; LABEL="${2:?label}"
  REMOTE="/data/pcps4/core_${LABEL}.prx"
  curl -s --ftp-create-dirs -T "$PRX" "$PS4$REMOTE"
fi
before="$(curl -s "$PS4/data/pcps4/done" || true)"
printf '%s\n' "$REMOTE" > /tmp/pcps4_trigger
curl -s -T /tmp/pcps4_trigger "$PS4/data/pcps4/trigger"
for i in $(seq 1 240); do
  sleep 2
  now="$(curl -s "$PS4/data/pcps4/done" || true)"
  if [ -n "$now" ] && [ "$now" != "$before" ]; then echo "done: $now"; break; fi
done
curl -s "$PS4/data/pcps4_runner_log.txt" | tail -6
