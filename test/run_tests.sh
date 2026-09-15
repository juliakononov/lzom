#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE="$SCRIPT_DIR/../lzom_module.ko"

[ ! -f "$MODULE" ] && MODULE="./lzom_module.ko"

TEST_FILES="$SCRIPT_DIR/test_files"
DEVICE="/dev/lzom0"

PASSED=0
FAILED=0

[ "$EUID" -ne 0 ] && { echo "Need root"; exit 1; }

[ ! -f "$MODULE" ] && { echo "Module not found: $MODULE"; exit 1; }

echo "=== Init ==="
rmmod lzom_module 2>/dev/null || true
rmmod brd 2>/dev/null || true

EXISTING_RAMS=$(ls /dev/ram* 2>/dev/null || true)

modprobe brd rd_nr=1 rd_size=512000

sleep 1
NEW_RAMS=$(ls /dev/ram* 2>/dev/null)
BRD_DEVICE=""

for dev in $NEW_RAMS; do
    if ! echo "$EXISTING_RAMS" | grep -q "$dev"; then
        BRD_DEVICE="$dev"
        break
    fi
done

[ -z "$BRD_DEVICE" ] && BRD_DEVICE="/dev/ram0"

[ ! -b "$BRD_DEVICE" ] && { echo "BRD device not found"; exit 1; }
echo "BRD device: $BRD_DEVICE"

insmod $MODULE
echo -n "$BRD_DEVICE" > /sys/module/lzom_module/parameters/path
sleep 1

[ ! -b "$DEVICE" ] && { echo "Device not found"; exit 1; }
echo "OK"

test_file() {
    local file=$1
    local bs=$2
    local name=$(basename "$file")
    local size=$(stat -c%s "$file")

    echo -n "Testing $name (bs=$bs, size=$size, whole file)... "

    local aligned_size=$(( (size + bs - 1) / bs * bs ))
    local count=$(( aligned_size / bs ))
    local padded=/tmp/lzom_test_padded.tmp
    local out=/tmp/lzom_test_out.tmp

    cp "$file" "$padded"
    truncate -s "$aligned_size" "$padded"

    dd if="$padded" of="$DEVICE" bs=$bs count=$count oflag=direct 2>/dev/null \
        || { echo "FAIL (write)"; FAILED=$((FAILED+1)); rm -f "$padded"; return; }
    dd if="$DEVICE" of="$out" bs=$bs count=$count iflag=direct 2>/dev/null \
        || { echo "FAIL (read)"; FAILED=$((FAILED+1)); rm -f "$padded" "$out"; return; }

    if cmp -s <(head -c "$size" "$out") "$file"; then
        echo "OK"
        PASSED=$((PASSED+1))
    else
        echo "FAIL (mismatch)"
        FAILED=$((FAILED+1))
    fi

    rm -f "$padded" "$out"
}

echo ""
echo "=== Tests ==="

BLOCK_SIZES=(4096 8192)

for file in "$TEST_FILES"/*; do
    [ -f "$file" ] || continue

    for bs in "${BLOCK_SIZES[@]}"; do
        test_file "$file" "$bs"
    done
done

echo ""
echo "=== Results ==="
echo "Passed: $PASSED"
echo "Failed: $FAILED"

echo ""
read -p "Cleanup? [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    rmmod lzom_module
    rmmod brd
    echo "Done"
fi

[ $FAILED -eq 0 ]
