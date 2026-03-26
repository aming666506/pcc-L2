# DOCA PCC Simple Congestion Control

A Programmable Congestion Control (PCC) application implementing a minimal AIMD (Additive Increase, Multiplicative Decrease) congestion control algorithm for NVIDIA BlueField DPUs.

## Overview

This project demonstrates DOCA PCC functionality with a simple congestion control algorithm that:

- **Additive Increase**: Rate increases by a fixed amount on each ACK/probe
- **Multiplicative Decrease**: Rate halves on CNP (Congestion Notification Packet) or NACK
- **Rate Limits**: Enforces configurable minimum and maximum rate bounds

## Project Structure

```
pcc-demo/
├── host/                    # Host-side application code
│   ├── pcc.c               # Application entry point
│   ├── pcc_core.c          # Core PCC implementation
│   └── pcc_core.h          # PCC interfaces and structures
├── device/                  # DPA (Data Path Accelerator) kernel code
│   ├── dpa_app_attributes.yaml  # DPA application attributes
│   └── rp/simple_cc/       # Simple CC algorithm implementation
│       ├── simple_cc_dev_main.c   # Device entry point
│       └── algo/
│           ├── simple_cc.c         # Main CC algorithm
│           ├── simple_cc.h         # Algorithm interface
│           ├── simple_cc_ctxt.h    # Flow context structure
│           └── simple_cc_algo_params.h  # Algorithm parameters
├── common/                  # Shared utilities
│   └── device/
│       └── utils.h         # Common device utilities
├── meson.build             # Build configuration
├── meson_options.txt       # Build options
└── build_device_code.sh    # Device code compilation script
```

## Requirements

- NVIDIA BlueField DPU with DOCA SDK installed
- DOCA SDK 2.0+ (doca-common, doca-pcc, doca-argp)
- libflexio
- dpacc tool for DPA compilation
- GCC for host code compilation
- Meson build system

## Build Instructions

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

### Full Rebuild

```bash
rm -rf build && meson setup build --prefix=$DOCA_INSTALL_PATH && meson compile -C build
```

### Install

```bash
meson install -C build
```

## Build Options

```bash
# Enable TX counter sampling
meson setup build -Denable_tx_counter_sampling=true

# Set DPACC MCPU flag (default: nv-dpa-bf3)
meson setup build -Ddpacc_mcpu_flag=nv-dpa-bf3
```

## Usage

### Basic Usage

```bash
./build/simple_cc_host -d <RDMA device>
```

### Command-Line Options

| Short | Long | Description | Required |
|-------|------|-------------|----------|
| `-d` | `--device` | RDMA device name (e.g., mlx5_0) | Yes |
| `-t` | `--threads` | PCC threads list (space-separated) | No |
| `-w` | `--wait-time` | Wait duration in seconds (-1 for infinity) | No |

### Example

```bash
# Run with default settings
./build/simple_cc_host -d mlx5_0

# Run with custom threads
./build/simple_cc_host -d mlx5_0 -t "176 177 178"
```

### Signal Handlers

- `SIGINT` (Ctrl+C): Graceful shutdown
- `SIGUSR1`: Toggle debug mode / dump debug info

## Algorithm Parameters

The Simple CC algorithm uses the following parameters defined in `device/rp/simple_cc/algo/simple_cc_algo_params.h`:

| Parameter | Description | Default Value |
|-----------|-------------|---------------|
| `SIMPLE_CC_MIN_RATE` | Minimum rate floor | 2^10 (1024, ~64 Mbps) |
| `SIMPLE_CC_AI_FXP20` | Additive increase rate | 2^12 (4096, ~256 Mbps/step) |
| `SIMPLE_CC_MD_FXP16` | Multiplicative decrease factor | 0x8000 (0.5x on congestion) |
| `SIMPLE_CC_RATE_MAX` | Maximum rate ceiling | 2^14 (16384, ~1.56 Gbps) |

### Rate Calculation

Rates are represented as fixed-point values where:
- Each unit = 2^20 / 2^20 = 1 Gbps (approximately)
- `2^10 = 1024` units = ~1 Gbps actual bandwidth

### Algorithm Behavior

1. **New Flow**: Initializes rate to `MIN_RATE`
2. **On ACK/Probe**: Apply additive increase (`rate += AI`)
3. **On CNP/NACK**: Apply multiplicative decrease (`rate *= MD`)
4. **Rate Clamping**: Always enforce `[MIN_RATE, MAX_RATE]` bounds

## Probe Packet Formats

The application supports multiple probe packet formats:

| Format | Description |
|--------|-------------|
| `PCC_DEV_PROBE_PACKET_CCMAD` | Congestion Notification MAD (default) |
| `PCC_DEV_PROBE_PACKET_IFA1` | In-band Flow Analyzer v1 |
| `PCC_DEV_PROBE_PACKET_IFA2` | In-band Flow Analyzer v2 |

Default is CCMAD. Use `probe_packet_format` in `pcc_config_t` to change.

## PCC Roles

- **RP (Reaction Point)**: Adjusts sending rate based on congestion signals (default)
- **NP (Notification Point)**: Generates congestion notifications

## Testing with qperf

### Bidirectional Test (RC)

```bash
# Receiver (ns1)
sudo ip netns exec ns1 ./build/simple_cc_host -d mlx5_0 -f /tmp/dump1.txt

# Sender (ns2)
sudo ip netns exec ns2 ./build/simple_cc_host -d mlx5_1 -f /tmp/dump2.txt

# Run bandwidth test
sudo ip netns exec ns2 qperf -cm1 -t 30 192.168.123.1 rc_bw
```

### Unidirectional Test (UD)

```bash
# Receiver
sudo ip netns exec ns1 ./build/simple_cc_host -d mlx0

# Sender
sudo ip netns exec ns2 qperf -cm1 -t 30 192.168.123.1 ud_bw
```

## Debugging

### Device Logs

Device-side printf output is captured via the DOCA PCC trace buffer.

### Enable Debug Mode

Send `SIGUSR1` to enable debug mode:

```bash
kill -USR1 <pid>
```

Send `SIGUSR1` again to dump debug information.

### Log Output

The algorithm prints rate changes to the device trace buffer:

```
CC: rate 1024 -> 1536 (MD/AI), limits [1024, 16384]
CC: CNP received, apply MD
CC: rate 1536 -> 768 (MD/AI), limits [1024, 16384]
CC: New flow started, init_rate=1024, final_rate=1024, max=16384
```

## Architecture

### Host Side (pcc.c)

The host application:
1. Parses command-line arguments
2. Initializes DOCA PCC context
3. Starts PCC on the specified device
4. Handles signals for shutdown and debug

### Device Side (device/)

DPA kernel code that runs on the BlueField DPU:
- Processes congestion events (CNP, NACK, probes)
- Calculates new rates using AIMD algorithm
- Maintains per-flow state

### Key Data Structures

```c
// Flow context (per-connection state)
typedef struct {
    uint32_t cur_rate;      // Current sending rate
    struct {
        uint32_t was_cnp:1; // Was CNP received?
        uint32_t was_nack:1; // Was NACK received?
    } flags;
} simple_cc_ctxt_t;
```

## Performance Tuning

### Rate Parameters

Adjust `simple_cc_algo_params.h` for different congestion control behaviors:

- **Higher MIN_RATE**: Faster startup, less RTT gain
- **Higher AI**: Faster convergence, potential overshoot
- **Lower MD (closer to 1)**: Gentler decrease on congestion
- **Higher MAX_RATE**: Allows more throughput on uncongested paths

### Thread Affinity

For optimal performance, configure PCC threads to run on specific CPU cores:

```bash
./build/simple_cc_host -d mlx5_0 -t "176 177 178 179"
```

## Troubleshooting

### Common Issues

1. **Device not found**: Ensure DOCA is installed and device supports PCC
2. **Build failures**: Verify DOCA_INSTALL_PATH is set correctly
3. **Permission denied**: May need root privileges for RDMA device access

### Check Device PCC Support

```bash
doca_devinfo -l | grep -i pcc
```

### Verbose Build

```bash
meson setup build --prefix=$DOCA_INSTALL_PATH -Dlog_level=debug
meson compile -C build -v
```

## License

This code is licensed under the BSD-3-Clause license. See LICENSE file for details.

## References

- [DOCA PCC Programming Guide](https://docs.nvidia.com/doca/sdk/programmable-conestion-control-guide/index.html)
- [NVIDIA BlueField DPU Documentation](https://docs.nvidia.com/doca/)
- [PCC Architecture](https://github.com/eth-pcc/arch)
