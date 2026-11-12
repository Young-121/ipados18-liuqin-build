#!/system/bin/sh
# Read the PONGLOG1 persistent XNU console ring from the hfs/userdata
# partition.  Run as root on the tablet (Android or recovery).  Read-only:
# the partition is never written by this script.
#
#   sh ponglog-android.sh [OUT.txt]
#
# Default OUT is /sdcard/ponglog.txt, falling back to /data/local/tmp.
# DEV=/dev/block/sdX overrides block-device discovery.
set -eu

out=${1:-/sdcard/ponglog.txt}
if ! ( : > "$out" ) 2>/dev/null; then
    out=/data/local/tmp/ponglog.txt
fi

dev=${DEV:-}
if [ -z "$dev" ]; then
    for p in /dev/block/by-name/hfs /dev/block/bootdevice/by-name/hfs \
             /dev/block/by-name/userdata /dev/block/bootdevice/by-name/userdata; do
        if [ -e "$p" ]; then
            t=$(readlink "$p" 2>/dev/null || true)
            dev=${t:-$p}
            break
        fi
    done
fi
[ -n "$dev" ] || { echo "ponglog: hfs/userdata block device not found" >&2; exit 1; }
[ -r "$dev" ] || { echo "ponglog: $dev is not readable (run as root)" >&2; exit 1; }

# Size in bytes.  blockdev is the normal path on a block device; wc -c
# also lets the same script parse a raw image file.
size=
if command -v blockdev >/dev/null 2>&1; then
    size=$(blockdev --getsize64 "$dev" 2>/dev/null || true)
fi
if [ -z "$size" ] && [ -f "$dev" ]; then
    size=$(wc -c < "$dev" | tr -d ' \n')
fi
if [ -z "$size" ]; then
    base=$(basename "$(readlink "$dev" 2>/dev/null || echo "$dev")")
    sectors=$(cat "/sys/class/block/$base/size" 2>/dev/null | tr -d ' \n' || true)
    [ -n "$sectors" ] && size=$((sectors * 512))
fi
[ -n "$size" ] || { echo "ponglog: cannot get the size of $dev" >&2; exit 1; }
[ "$size" -ge $((4 * 1024 * 1024)) ] || {
    echo "ponglog: $dev is smaller than the 4 MiB log area" >&2; exit 1; }

log_lba=$(( (size - 4 * 1024 * 1024) / 4096 ))
tmp=${TMPDIR:-/data/local/tmp}/ponglog.$$.bin
cleanup() { rm -f "$tmp" "$out.tmp"; }
trap cleanup EXIT INT TERM

dd if="$dev" of="$tmp" bs=4096 skip="$log_lba" count=1024 2>/dev/null
[ "$(wc -c < "$tmp")" -eq $((4 * 1024 * 1024)) ] || {
    echo "ponglog: short read from $dev at LBA $log_lba" >&2; exit 1; }

magic=$(dd if="$tmp" bs=1 count=8 2>/dev/null)
[ "$magic" = "PONGLOG1" ] || {
    echo "ponglog: no PONGLOG1 header at LBA $log_lba (log never flushed?)" >&2
    exit 1; }

u32() { od -A n -t u4 -j "$1" -N 4 "$tmp" | tr -d ' \n'; }
head=$(u32 8)
length=$(u32 12)
flush=$(u32 24)
max=$((4 * 1024 * 1024 - 4096))
[ -n "$length" ] || { echo "ponglog: missing length" >&2; exit 1; }
[ "$length" -le "$max" ] || {
    echo "ponglog: implausible length $length (max $max)" >&2; exit 1; }

# pongLogFlush already writes the ring rotated so body[0] is the oldest
# byte; only the block-aligned length is needed to copy it out.
blocks=$((length / 4096))
dd if="$tmp" of="$out.tmp" bs=4096 skip=1 count="$blocks" 2>/dev/null
tr -d '\000' < "$out.tmp" > "$out"
rm -f "$out.tmp"

echo "ponglog: dev=$dev size=$size lba=$log_lba head=$head length=$length flush=$flush" >&2
echo "ponglog: wrote $out" >&2
