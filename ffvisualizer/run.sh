#!/usr/bin/env bash
# Launcher for the FFB Visualizer.
#
# Proxy mode needs elevated privileges (RW on /dev/input/eventN and /dev/uinput).
# We set up the Python venv as the INVOKING user (not root) so files aren't
# root-owned, then re-exec python with sudo only if not already root.
set -e
cd "$(dirname "$0")"

VPY="$PWD/.venv/bin/python"
VPIP="$PWD/.venv/bin/pip"

# Self-heal: a previous `sudo ./run.sh` may have root-owned .venv / __pycache__,
# which would make the user's pip rebuild fail with EACCES. Fix ownership first.
if [ -n "${SUDO_UID:-}" ] && [ -e ".venv" ]; then
  chown -R "$SUDO_UID:${SUDO_gid:-$SUDO_UID}" .venv __pycache__ 2>/dev/null || true
fi

# --- dependency setup (as the invoking user, using venv binaries) -----------
# If called via sudo, drop privileges for venv/pip so files aren't root-owned.
SETUP_UID="${SUDO_UID:-$(id -u)}"
as_user() { sudo -u "#$SETUP_UID" "$@"; }

if [ ! -x "$VPY" ]; then
  as_user python3 -m venv .venv
fi
# Always use the venv's pip by absolute path (activation does NOT propagate
# across sudo -u, which previously fell back to the system pip and tripped
# Arch's PEP 668 externally-managed-environment block).
as_user "$VPIP" install -q -r requirements.txt

# --- run -------------------------------------------------------------------
# Proxy mode requires root; observe mode does not (only needs read access).
PY="$VPY"
if [ "$(id -u)" -ne 0 ]; then
  case "${1:-}" in
    --observe) : ;;                       # no elevation needed
    *) PY="sudo -E $VPY" ;;               # elevate for uinput/input RW
  esac
fi

exec $PY main.py "$@"
