#!/bin/bash
# Run all JSON tests in batches

TOTAL_BATCHES=32  # 323 files / 10 files per batch = 33 batches (0-32)

echo "================================================="
echo "  Running ALL 8086 SingleStep Tests"
echo "  Total files: 323"
echo "  Batch size: 10 files"
echo "  Total batches: 33 (0-32)"
echo "================================================="
echo ""

TOTAL=0
PASSED=0
FAILED=0

for batch in $(seq 0 $TOTAL_BATCHES); do
    echo "Running batch $batch..."

    # Run test and capture output
    output=$(./json_test_runner $batch $batch 2>&1)

    # Extract results using grep and awk
    batch_total=$(echo "$output" | grep "Total:" | awk '{print $2}')
    batch_passed=$(echo "$output" | grep "Passed:" | awk '{print $2}')
    batch_failed=$(echo "$output" | grep "Failed:" | awk '{print $2}')

    if [ -n "$batch_total" ]; then
        TOTAL=$((TOTAL + batch_total))
        PASSED=$((PASSED + batch_passed))
        FAILED=$((FAILED + batch_failed))
        echo "  Batch $batch: $batch_passed/$batch_total passed"
    else
        echo "  Batch $batch: ERROR - no output"
    fi
done

echo ""
echo "================================================="
echo "  FINAL RESULTS - ALL TESTS"
echo "================================================="
echo "  Total:   $TOTAL"
echo "  Passed:  $PASSED ($(awk "BEGIN {printf \"%.1f\", ($PASSED*100.0/$TOTAL)}")%)"
echo "  Failed:  $FAILED"
echo "================================================="
