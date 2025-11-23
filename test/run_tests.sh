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
    
    echo -n "Testing $name (bs=$bs)... "
    
    local size=$(stat -c%s "$file")
    [ $size -lt $bs ] && { echo "SKIP (too small)"; return; }
    
    # Write/Read
    dd if="$file" of="$DEVICE" bs=$bs count=1 oflag=direct 2>/dev/null || { echo "FAIL (write)"; FAILED=$((FAILED+1)); return; }
    dd if="$DEVICE" of=/tmp/out.tmp bs=$bs count=1 iflag=direct 2>/dev/null || { echo "FAIL (read)"; FAILED=$((FAILED+1)); return; }
    
    # Verify
    dd if="$file" of=/tmp/orig.tmp bs=$bs count=1 2>/dev/null
    
    if diff -q /tmp/orig.tmp /tmp/out.tmp >/dev/null 2>&1; then
        echo "OK"
        PASSED=$((PASSED+1))
    else
        echo "FAIL (mismatch)"
        FAILED=$((FAILED+1))
    fi
    
    rm -f /tmp/out.tmp /tmp/orig.tmp
}

echo ""
echo "=== Tests ==="

BLOCK_SIZES=(4096 8192)

for file in "$TEST_FILES"/*; do
    [ -f "$file" ] || continue
    
    for bs in "${BLOCK_SIZES[@]}"; do
        test_file "$file" $bs
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