# AGENTS.md

This file provides guidelines for agentic coding agents operating in this repository.

## Project Overview

This is a DOCA PCC (Programmable Congestion Control) application that implements congestion control algorithms for NVIDIA BlueField DPUs. The project consists of:
- **Host code**: C11 application that manages PCC lifecycle
- **Device code**: DPA (Data Path Accelerator) kernels compiled with dpacc

## Build Commands

### Initial Setup
```bash
meson setup build --prefix=$DOCA_INSTALL_PATH
```

### Build
```bash
meson compile -C build
```

### Clean
```bash
meson compile -C build --clean
```

### Rebuild (full)
```bash
rm -rf build && meson setup build --prefix=$DOCA_INSTALL_PATH && meson compile -C build
```

### Install
```bash
meson install -C build
```

### Device Code Build
Device code is automatically built via `build_device_code.sh` during the Meson build process. This script uses the `dpacc` tool to compile DPA kernels.

### Build Options
```bash
# Enable TX counter sampling
meson setup build -Denable_tx_counter_sampling=true

# Set DPACC MCPU flag (default: nv-dpa-bf3)
meson setup build -Ddpacc_mcpu_flag=nv-dpa-bf3
```

## Testing

No formal test suite exists for this project. Test new changes by:
1. Building the project successfully
2. Running on a BlueField DPU with DOCA runtime

## Code Style Guidelines

### General
- **C Standard**: C11 (`c_std=c11` in meson.build)
- **Indentation**: Use tabs for indentation (match existing code)
- **Line length**: Soft limit at 100 characters
- **Copyright**: All files must include NVIDIA copyright header (see existing files)

### Naming Conventions
- **Functions**: `lowercase_with_underscores` (e.g., `pcc_init`, `simple_cc_step`)
- **Variables**: `lowercase_with_underscores` (e.g., `doca_device`, `cur_rate`)
- **Constants/Macros**: `UPPER_CASE_WITH_UNDERSCORES` (e.g., `DOCA_SUCCESS`, `MAX_ARG_SIZE`)
- **Types**: `snake_case_t` suffix for typedefs (e.g., `doca_error_t`, `pcc_role_t`)
- **Structs**: `lowercase_with_underscores` (e.g., `pcc_config`, `bytes_ts_t`)
- **Enums**: `UPPER_CASE_WITH_UNDERSCORES` for values (e.g., `DOCA_PCC_PS_ERROR`)

### Comments
- Use Doxygen-style for function documentation:
  ```
  /*
   * Function description
   *
   * @param [in]: parameter description
   * @return: return value description
   */
  ```
- Use `//` for inline comments in device code
- Use `/* ... */` for block comments in host code

### Error Handling
- Functions return `doca_error_t` (`DOCA_SUCCESS` on success, error code on failure)
- Check return values with `if (result != DOCA_SUCCESS)`
- Use `DOCA_ERROR_PROPAGATE()` to chain error codes
- Use `PRINT_ERROR()`, `PRINT_WARNING()`, `PRINT_INFO()` for logging

### Includes
- System includes first: `<stdlib.h>`, `<stdio.h>`, etc.
- Third-party includes second: `<doca_*.h>`, `<flexio_*.h>`
- Local includes last: `"pcc_core.h"`, `"utils.h"`
- Use angle brackets for system/third-party, quotes for local

### Header Guards
```c
#ifndef PCC_CORE_H_
#define PCC_CORE_H_
/* ... */
#endif /* PCC_CORE_H_ */
```

### Device-Specific Code
- Device code (in `device/`) uses DPA-specific headers (`<doca_pcc_dev.h>`)
- Use `ALWAYS_INLINE` macro for inline functions
- Use `unlikely()` macro for branch prediction hints
- `#pragma clang diagnostic` can suppress (match warnings existing patterns)

### macros
- Define macros with parentheses around arguments: `#define MAX_SIZE (1024)`
- Use do-while for multi-statement macros:
  ```c
  #define PRINT_ERROR(...) \
      do { \
          if (log_level >= LOG_LEVEL_ERROR) \
              printf(__VA_ARGS__); \
      } while (0)
  ```

## Key Files

- `meson.build`: Main build configuration
- `build_device_code.sh`: Device code compilation script
- `host/pcc.c`: Application entry point
- `host/pcc_core.h`: Core PCC interfaces and macros
- `host/pcc_core.c`: PCC core implementation
- `device/rp/simple_cc/`: Simple CC congestion control algorithm

## Dependencies

- DOCA SDK (doca-common, doca-pcc, doca-argp)
- libflexio
- dpacc tool for DPA compilation
- GCC for host code compilation
