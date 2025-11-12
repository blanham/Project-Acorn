# Project Acorn Test Suite

This directory contains the CPU test framework for Project Acorn.

## Test Repository

The tests are based on the comprehensive Intel 8086 test suite from:
https://github.com/SingleStepTests/8086

This repository contains approximately 2,000 test cases per opcode, with randomized register and memory states to ensure thorough testing of CPU behavior.

## Test Structure

### 8086_tests/
Contains the cloned test repository with JSON test files organized by opcode:
- `v1/` - JSON format test files (gzipped)
- `v1_binary/` - Binary MOO format test files

Each test file (e.g., `B0.json.gz`) contains tests for a specific opcode (0xB0 = MOV AL, imm8).

### Test Format

Each test case includes:
- **name**: Disassembly of the instruction
- **bytes**: Raw instruction bytes
- **initial**: Initial CPU state (registers, memory, instruction queue)
- **final**: Expected final CPU state (only changed values)
- **cycles**: Detailed bus cycle information (for advanced testing)

## Running Tests

Build and run the test suite:
```bash
make test
```

This will compile the test runner and execute all implemented tests.

## Test Coverage

Currently implemented tests:
- [x] MOV AL, imm8 (0xB0)

## Adding New Tests

To add tests for a new instruction:

1. Implement the instruction in `opcode.h`
2. Add a test function in `test_runner.c`:
   ```c
   void test_<instruction_name>(TestResults *results)
   {
       // Set up CPU state
       // Execute instruction
       // Verify results
   }
   ```
3. Call the test function from `main()`
4. Run `make test` to verify

## Future Enhancements

- [ ] JSON parser for automated test loading from 8086_tests repository
- [ ] Cycle-accurate bus testing
- [ ] Memory comparison utilities
- [ ] Automated test generation from JSON files
- [ ] Performance benchmarking
- [ ] Coverage reporting
