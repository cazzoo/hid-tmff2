#!/bin/bash
# Build, reload, and verify TMFF2 driver module
# Usage: ./build-reload.sh [--build-only|-b] [--help|-h] [insmod params]

set -Eeuo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color
usage() {
    cat <<EOF
Usage: ./build-reload.sh [--build-only|-b] [--help|-h] [insmod params]

Options:
  -b, --build-only   Build the module only; do not reload
  -h, --help         Show this help and exit

Any remaining arguments are passed to insmod as module parameters.
EOF
}


echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}TMFF2 Driver Build, Reload, and Verify Script${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Parse options
BUILD_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        -b|--build-only)
            BUILD_ONLY=1; shift ;;
        -h|--help)
            usage; exit 0 ;;
        --)
            shift; break ;;
        -* )
            echo -e "${RED}Unknown option: $1${NC}"; usage; exit 2 ;;
        * )
            # First non-option: treat the rest as module params
            break ;;
    esac
done
# Resolve repo root and local .ko path
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$(cd "$SCRIPT_DIR"/.. && pwd)"
LOCAL_KO="$REPO_ROOT/hid-tmff-new.ko"

# Pre-flight: required tools
require_cmd() { command -v "$1" >/dev/null 2>&1; }
MISSING=()
for cmd in make modinfo; do
    if ! require_cmd "$cmd"; then MISSING+=("$cmd"); fi
done
if [ ${#MISSING[@]} -gt 0 ]; then
    echo -e "${RED}Missing required tools: ${MISSING[*]}${NC}"; exit 2
fi

# Determine parallel jobs
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 4)}



# Build
echo -e "${YELLOW}Building driver...${NC}"
if make -C "$REPO_ROOT" -j"$JOBS"; then
    echo -e "${GREEN}  ✓ Build succeeded${NC}"
else
    echo -e "${RED}  ✗ Build failed — aborting${NC}"
    exit 1
fi

if [ ! -f "$LOCAL_KO" ]; then
    echo -e "${RED}  ✗ Module not found after build: $LOCAL_KO${NC}"
    exit 1
fi


# Build-only mode: stop after building/validating artifact
if [ "$BUILD_ONLY" -eq 1 ]; then
    echo -e "${YELLOW}Build-only mode: module built at ${LOCAL_KO}${NC}"
    echo "To reload modules: sudo $0 [insmod params]"
    exit 0
fi

# Ensure we have root for module operations
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}ERROR: Module reload requires root. Re-run with sudo or use --build-only${NC}"
    exit 1
fi
# Pre-flight for reload operations
MISSING=()
for cmd in lsmod modprobe insmod; do
    if ! require_cmd "$cmd"; then MISSING+=("$cmd"); fi
done
if [ ${#MISSING[@]} -gt 0 ]; then
    echo -e "${RED}Missing required tools for reload: ${MISSING[*]}${NC}"; exit 2
fi



# Unload modules
echo -e "${YELLOW}Unloading modules...${NC}"
# Try to unload in reverse dependency order
if lsmod | grep -q "hid_tmff_new"; then
    echo "  - Removing hid_tmff_new..."
    modprobe -r hid_tmff_new || {
        echo -e "${RED}  WARNING: Failed to remove hid_tmff_new (may be in use)${NC}"
        echo "  Trying to force removal..."
        rmmod -f hid_tmff_new 2>/dev/null || true
    }
else
    echo "  - hid_tmff_new not loaded"
fi


echo -e "${GREEN}  ✓ Modules unloaded${NC}"
echo ""

# Wait for module unload to settle
for i in {1..10}; do
    if ! lsmod | grep -q "^hid_tmff_new"; then
        break
    fi
    sleep 0.2

done
echo ""

# Load main driver from local build directory
echo -e "${YELLOW}Loading main driver from local build...${NC}"
if [ -f "$LOCAL_KO" ]; then
    echo "  - Loading $LOCAL_KO with params: $*"
    insmod "$LOCAL_KO" "$@" && echo -e "${GREEN}  ✓ hid_tmff_new loaded from local build${NC}" || {
        echo -e "${RED}  ✗ Failed to load hid_tmff_new${NC}"
        exit 1
    }
else
    echo -e "${RED}  ✗ Module not found: $LOCAL_KO${NC}"
    echo "  Run 'make' first to build the module"
    exit 1
fi
echo ""

# Verify loaded module matches local build
echo -e "${YELLOW}Verifying loaded module matches local build...${NC}"
LOCAL_SRCVER=$(modinfo -F srcversion "$LOCAL_KO" 2>/dev/null || echo "unknown")
LOADED_SRCVER=$(cat /sys/module/hid_tmff_new/srcversion 2>/dev/null || echo "none")
INSTALLED_PATH=$(modinfo -n hid_tmff_new 2>/dev/null || echo "unknown")

printf "  - Local .ko srcversion:   %s\n" "$LOCAL_SRCVER"
printf "  - Loaded module srcversion:%s\n" "$LOADED_SRCVER"
printf "  - Installed candidate:     %s\n" "$INSTALLED_PATH"

if [ "$LOCAL_SRCVER" != "$LOADED_SRCVER" ]; then
    echo -e "${RED}  ✗ Mismatch: loaded module is not the local build${NC}"
    exit 1
else
    echo -e "${GREEN}  ✓ Loaded module matches local build${NC}"
fi

echo ""

# Show driver logs (TMFF2/hid_tmff_new)
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}TMFF2 Driver Status:${NC}"
echo -e "${BLUE}========================================${NC}"
if dmesg | grep -Ei "tmff2|hid_tmff_new" | tail -n 10; then
    :
fi

# Check for errors
if dmesg | tail -50 | grep -qi "error\|fail\|bug\|oops"; then
    echo -e "${RED}⚠ WARNING: Errors detected in kernel log!${NC}"
    dmesg | tail -50 | grep -i "error\|fail\|bug\|oops" | tail -10
    echo ""
fi

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Build, reload, and verification complete!${NC}"
echo -e "${GREEN}========================================${NC}"

