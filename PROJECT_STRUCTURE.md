# Project Structure

## Core Files
- `boards.py` - Board-specific configurations and platform definitions
- `soc_linux.py` - Main SoC generator with heterogeneous core support
- `make.py` - Build automation script with target selection


## Hardware Description
- `VexRiscv_IOCore.v` - I/O optimized RISC-V core
- `VexRiscv_RTCore.v` - Real-time optimized RISC-V core

## Software Components
- `driver/` - Linux kernel drivers for heterogeneous communication
- `test_code/` - Test programs and benchmarks


