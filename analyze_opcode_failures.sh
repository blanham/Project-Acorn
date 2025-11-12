#!/bin/bash
# Analyze individual opcode test results

echo "==================================================="
echo "  Detailed Opcode Test Results"
echo "==================================================="
echo ""

files=(tests/8086_tests/v1/*.json.gz)
total_files=${#files[@]}

echo "Analyzing $total_files opcode test files..."
echo ""

for file in "${files[@]}"; do
    basename=$(basename "$file" .json.gz)
    output=$(./json_test_runner 0 0 2>&1 | grep "^Testing $basename.json.gz" -A 1000 | grep "^Testing" -A 1 | head -2)

    # Run test for this specific file
    result=$(./json_test_runner 0 0 2>&1 <<< "tests/8086_tests/v1/$basename.json.gz")
done

# Instead, let's just run each opcode individually
for i in $(seq 0 $((total_files - 1))); do
    file_index=$i
    batch=$((file_index / 10))

    # Run single batch
    output=$(./json_test_runner $batch $batch 2>&1)

    # Extract per-file results from the output
    filename=$(ls tests/8086_tests/v1/*.json.gz | sed -n "$((i+1))p" | xargs basename .json.gz)

    # For now, just show batch summary
    if [ $((i % 10)) -eq 0 ]; then
        passed=$(echo "$output" | grep "Passed:" | awk '{print $2}')
        total=$(echo "$output" | grep "Total:" | awk '{print $2}')
        pct=$(awk "BEGIN {printf \"%.1f\", ($passed*100.0/$total)}")
        echo "Batch $batch (files $i-$((i+9))): $passed/$total ($pct%)"
    fi
done
