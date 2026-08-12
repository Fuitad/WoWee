#!/usr/bin/env bash
# Every addon event this client fires, however it is dispatched.
#
# There are four call styles, and grepping for one of them under-reports the
# rest badly - the answer has been 6, 52, 73 and 147 depending on which was
# searched. Integration work needs to know whether an event FrameXML listens
# for actually arrives, so it should ask this rather than guess.
#
#   tools/addon_events.sh            list them
#   tools/addon_events.sh UNIT_      only those matching a pattern
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
{
  grep -rhoP 'fireEvent\("\K[A-Z_0-9]+'                      "$root/src" || true
  grep -rhoP 'fireAddonEvent\("\K[A-Z_0-9]+'                 "$root/src" || true
  grep -rhoP '\bemit\("\K[A-Z_0-9]+'                         "$root/src" || true
  grep -rhoP 'addonEventCallbackRef\(\)\("\K[A-Z_0-9]+'      "$root/src" || true
  grep -rhoP 'addonEventCallback_\("\K[A-Z_0-9]+'            "$root/src" || true
} | sort -u | { [ $# -gt 0 ] && grep -- "$1" || cat; }
