# Project Acorn Development Plan

## Vision

Create a complete, accurate emulator for the IBM PC 5150 (8086-based) that can run original IBM PC software and provide a learning platform for understanding early PC architecture.

## Development Phases

### Phase 1: Core CPU Implementation

**Status:** ✅ Complete (100%)

**Goals:**
- Complete Intel 8086 instruction set implementation
- Accurate flag handling for all operations
- Memory addressing modes
- Interrupt handling

**Tasks:**
- [x] Review CPU state, encapsulate where necessary, other code improvements
- [x] Add comprehensive CPU state debugging output
- [x] Create CPU test framework using https://github.com/singleStepTests/8086
- [x] Complete ModR/M byte decoding for all addressing modes
- [x] Implement arithmetic instructions (ADD, SUB, CMP, INC, DEC)
- [x] Implement logical instructions (AND, OR, XOR, TEST)
- [x] Implement shift/rotate instructions (SHL, SHR, SAL, SAR, ROL, ROR, RCL, RCR)
- [x] Implement stack operations (PUSH, POP, PUSHF, POPF)
- [x] Implement control flow instructions (CALL, RET, INT, IRET, LOOP, JMP, JCXZ)
- [x] Implement remaining MOV variants (MOV with ModR/M, segment registers, direct memory)
- [x] Implement multiplication and division (MUL, IMUL, DIV, IDIV)
- [x] Implement remaining arithmetic (ADC, SBB, NEG, AAA, DAA, AAS, DAS, AAM, AAD)
- [x] Implement string operations (MOVS, CMPS, SCAS, LODS, STOS)
- [x] Implement remaining instructions (XCHG, LEA, LDS, LES, NOP, HLT, CBW, CWD, NOT)
- [x] Implement Grp3 and Grp4/5 instructions (INC/DEC/CALL/JMP with ModR/M)
- [x] Basic segment register handling (segment override prefixes deferred to Phase 2)

**Completed Instructions:** All 8086 base instruction set opcodes (0x00-0xFF) are now implemented.

**Time Taken:** Implementation completed 2025-11-12

### Phase 2: Memory and I/O Subsystems

**Status:** Not Started

**Goals:**
- Accurate memory management
- I/O port handling
- DMA controller emulation
- Interrupt controller (8259 PIC)

**Tasks:**
- [ ] Implement segmented memory model correctly
- [ ] Add memory-mapped I/O regions
- [ ] Implement 8259 PIC (Programmable Interrupt Controller)
- [ ] Implement 8237 DMA controller
- [ ] Add port I/O infrastructure
- [ ] Implement timer (8253/8254 PIT)
- [ ] Add memory read/write tracking for debugging


### Phase 3: Video Output

**Status:** Not Started (SDL2 window creation complete)

**Goals:**
- MDA (Monochrome Display Adapter) support
- CGA (Color Graphics Adapter) support
- Text mode rendering
- Basic graphics mode support

**Tasks:**
- [ ] Implement MDA text mode (80x25)
- [ ] Implement CGA text mode (80x25, 40x25)
- [ ] Implement CGA graphics modes (320x200, 640x200)
- [ ] Add character ROM loading
- [ ] Implement video memory mapping
- [ ] Add SDL2 rendering pipeline for text mode
- [ ] Add SDL2 rendering pipeline for graphics modes
- [ ] Implement cursor blinking
- [ ] Add color palette emulation for CGA


### Phase 4: Peripheral Devices

**Status:** Not Started

**Goals:**
- Keyboard input (8042 keyboard controller)
- Speaker output
- Serial port (8250 UART)
- Parallel port

**Tasks:**
- [ ] Implement keyboard controller and scancode handling
- [ ] Add SDL2 keyboard event mapping to PC scancodes
- [ ] Implement PC speaker emulation with SDL2 audio
- [ ] Add serial port (8250 UART) emulation
- [ ] Add parallel port emulation
- [ ] Implement keyboard buffer and interrupts

### Phase 5: Storage

**Status:** Not Started

**Goals:**
- Floppy disk controller
- Disk image support
- BIOS interrupt handlers for disk I/O

**Tasks:**
- [ ] Implement floppy disk controller (NEC µPD765)
- [ ] Add support for .IMG disk images
- [ ] Implement disk geometry handling (tracks, sectors, heads)
- [ ] Add disk read/write operations
- [ ] Implement BIOS INT 13h disk services
- [ ] Add support for bootable disk images
- [ ] Implement disk change detection

### Phase 6: System Integration and Testing

**Status:** Not Started

**Goals:**
- Complete system integration
- Boot real IBM PC software
- Performance optimization
- Comprehensive testing

**Tasks:**
- [ ] Ensure all BIOS interrupts are properly handled
- [ ] Test with original IBM PC BIOS ROMs
- [ ] Test with MS-DOS boot disks
- [ ] Test with various PC software (games, applications)
- [ ] Profile and optimize performance bottlenecks
- [ ] Add save state functionality
- [ ] Implement debugger interface
- [ ] Add execution tracing and logging options
- [ ] Create comprehensive test suite
- [ ] Write user documentation

### Phase 7: Advanced Features (Future)

**Status:** Planning

**Goals:**
- Enhanced compatibility
- Modern conveniences
- Advanced debugging

**Possible Features:**
- EGA/VGA support (requires 80286/386 support)
- Hard disk support
- Mouse support
- Network card emulation
- Turbo mode (run faster than original hardware)
- Integrated debugger with disassembly
- Memory viewer
- Performance profiling tools
- Automated regression testing
- CI/CD integration

## Current Architecture

### Code Organization

```
5150emu.c       - Main emulator loop, BIOS loading, SDL initialization
intel8086.c     - CPU state management, instruction dispatch
intel8086.h     - CPU structure and register definitions
opcode.h        - Individual instruction implementations
```

### Memory Layout

```
0x00000 - 0x003FF : Interrupt Vector Table (IVT)
0x00400 - 0x004FF : BIOS Data Area (BDA)
0x00500 - 0x9FFFF : Conventional Memory (RAM)
0xA0000 - 0xBFFFF : Video Memory
0xC0000 - 0xEFFFF : Expansion ROM area
0xF0000 - 0xFFFFF : BIOS ROM
```

### CPU State Structure

The `X86Cpu` structure maintains:
- General purpose registers (AX, BX, CX, DX)
- Segment registers (CS, DS, SS, ES)
- Pointer/Index registers (SP, BP, SI, DI)
- Instruction pointer (IP)
- Flags register
- RAM pointer (1MB address space)

## Building and Testing Strategy

### Compiler Warnings

All code must compile without warnings using:
- `-Wall` - Enable all common warnings
- `-Wextra` - Enable extra warnings
- `-Werror` - Treat warnings as errors

This ensures code quality and catches potential bugs early.

### Testing Approach

1. **Unit Tests**: Test individual instructions with known inputs/outputs
2. **Integration Tests**: Test instruction sequences and BIOS routines
3. **System Tests**: Boot real software and verify correct execution
4. **Regression Tests**: Prevent previously-fixed bugs from returning

### Code Review Checklist

- [ ] All functions have clear, descriptive names
- [ ] Complex logic is commented
- [ ] No magic numbers (use #define constants)
- [ ] Error conditions are properly handled
- [ ] Memory allocations are paired with frees
- [ ] Code follows project style guidelines
- [ ] New features include tests
- [ ] Documentation is updated

## Contribution Guidelines

### Getting Started

1. Read this development plan
2. Check the current phase and available tasks
3. Claim a task by opening an issue
4. Create a feature branch
5. Implement and test your changes
6. Submit a pull request

### Code Style

- Use tabs for indentation
- K&R brace style
- Descriptive variable names (no single letters except loop counters)
- Comment complex algorithms
- Keep functions focused and reasonably sized (<100 lines)

### Commit Messages

Use clear, descriptive commit messages:
```
Add SHL/SHR instruction implementation

Implements the shift left and shift right instructions including
proper flag handling (CF, ZF, SF, PF). Fixes #42.
```

## Resources

### Essential Documentation

- [Intel 8086 Family User's Manual (PDF)](https://edge.edx.org/c4x/BITSPilani/EEE231/asset/8086_family_Users_Manual_1_.pdf)
- [IBM 5150 Technical Reference Manual](http://www.minuszerodegrees.net/manuals/IBM_5150_Technical_Reference_6025005_APR84.pdf)
- [x86 Opcode and Instruction Reference](https://www.felixcloutier.com/x86/)

### Helpful Resources

- [OSDev Wiki - x86](https://wiki.osdev.org/X86)
- [BOCHS Emulator Source](https://github.com/bochs-emu/Bochs) - Reference implementation
- [QEMU Source](https://github.com/qemu/qemu) - Another reference implementation
- [emu8086 Documentation](http://www.emu8086.com/)

### Test Software

- MS-DOS boot disks (various versions)
- IBM PC Diagnostics
- Classic PC games (Prince of Persia, Commander Keen, etc.)
- Benchmark utilities (Landmark, CheckIt)

## Long-term Goals

1. **Accuracy**: Bit-perfect emulation of the original hardware
2. **Performance**: Run at original speed or faster on modern hardware
3. **Usability**: Easy to build, configure, and use
4. **Educational**: Well-documented code suitable for learning
5. **Compatibility**: Run a wide variety of IBM PC software

## Version Milestones

- **v0.1.0** (Current): Basic structure, SDL2 integration, Meson build
- **v0.2.0**: Complete instruction set implementation
- **v0.3.0**: Full memory management and I/O infrastructure
- **v0.4.0**: Working video output (text mode)
- **v0.5.0**: Keyboard and speaker support
- **v0.6.0**: Floppy disk support
- **v0.7.0**: Boot MS-DOS successfully
- **v1.0.0**: Run major PC software, comprehensive test suite

## Contact and Discussion

For questions, suggestions, or discussions about the project architecture and development plan, please open an issue on the project repository.

---

*Last Updated: 2025-11-12*
