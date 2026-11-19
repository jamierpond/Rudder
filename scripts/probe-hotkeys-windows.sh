#!/usr/bin/env bash
# Asks a Windows box which of the given chords (letter keys only) are free to
# register as global hotkeys, in the logged-in user's desktop session: ssh runs
# in a session of its own, which never sees the shell's hotkeys, so the probe
# runs as a one-off scheduled task in the interactive session and its output
# is read back. The host's ssh shell must be git-bash, with the user logged in.
set -euo pipefail

host="${1:?usage: probe-hotkeys-windows.sh <host> <chord>...}"
shift

remoteDirectory="tmp/rudder-hotkey-probe"
taskName="RudderHotKeyProbe"
waitSeconds=20

ssh "$host" "mkdir -p $remoteDirectory && rm -f $remoteDirectory/result.txt"
scp -q "$(dirname "$0")/probe-hotkeys-windows.ps1" "$host:$remoteDirectory/probe.ps1"
printf '%s\n' "$@" | ssh "$host" "cat > $remoteDirectory/chords.txt"

ssh "$host" "set -e
directory=\$(cygpath -w ~/$remoteDirectory)
command=\"powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \$directory\\probe.ps1\"
schtasks //create //f //tn $taskName //sc once //st 00:00 //it //tr \"\$command\" > /dev/null 2>&1
schtasks //run //tn $taskName > /dev/null
for attempt in \$(seq $waitSeconds); do
    [ -s ~/$remoteDirectory/result.txt ] && break
    sleep 1
done
schtasks //delete //f //tn $taskName > /dev/null
cat ~/$remoteDirectory/result.txt"
