#!/bin/bash
# Analyze which opcodes are failing

echo "Analyzing test failures by opcode..."
echo ""

for batch in {0..32}; do
    output=$(./json_test_runner $batch $batch 2>&1)

    total=$(echo "$output" | grep "Total:" | awk '{print $2}')
    passed=$(echo "$output" | grep "Passed:" | awk '{print $2}')
    failed=$(echo "$output" | grep "Failed:" | awk '{print $2}')

    if [ -n "$total" ] && [ "$total" -gt 0 ]; then
        pass_pct=$(awk "BEGIN {printf \"%.1f\", ($passed*100.0/$total)}")

        # Get the files processed in this batch
        files=$(ls tests/8086_tests/v1/*.json.gz | sort | sed -n "$((batch*10+1)),$((batch*10+10))p" | xargs -n1 basename | sed 's/.json.gz//')

        if [ "$pass_pct" != "100.0" ]; then
            echo "Batch $batch: $pass_pct% pass rate ($passed/$total)"
            echo "  Opcodes: $files"
            echo ""
        fi
    fi
done
