#!/usr/bin/env bash
# Runs `just test` on a Windows box over ssh, against this checkout's work: a
# checkout of its own there (never the box's own working copy), reset to
# origin/main, with every local change against origin/main applied on top,
# untracked files included. The host's ssh shell must be git-bash.
set -euo pipefail

host="${1:-jamie@tamby-windows}"
remoteDirectory="projects/Rudder-remote-test"
repositoryUrl="$(git remote get-url origin)"

temporaryIndex="$(mktemp)"
patch="$(mktemp)"
trap 'rm -f "$temporaryIndex" "$patch"' EXIT

# A throwaway index, so the real one (what is staged) is left alone.
GIT_INDEX_FILE="$temporaryIndex" git read-tree HEAD
GIT_INDEX_FILE="$temporaryIndex" git add -A
GIT_INDEX_FILE="$temporaryIndex" git diff --cached --binary origin/main > "$patch"

ssh "$host" "set -e
if [ ! -d $remoteDirectory ]; then
    git clone --config core.autocrlf=input $repositoryUrl $remoteDirectory
fi
cd $remoteDirectory
git fetch --quiet origin
git reset --quiet --hard origin/main
git clean --quiet -fd
git apply --whitespace=nowarn -" < "$patch"

ssh "$host" "cd $remoteDirectory && just test"
