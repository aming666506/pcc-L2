# LongCC DOCA PCC prototype

A DOCA Programmable Congestion Control application for NVIDIA BlueField DPUs. The active implementation combines forward delay variation, RTT-specific rate parameters, and a NACK fallback. Adaptive Probing Control (APC) is not implemented yet; every event requests the next RTT probe.

## Overview

The current device algorithm:

- Computes `(recv_i - recv_(i-1)) - (send_i - send_(i-1))` from consecutive RTT probes, then applies an EWMA with weight 1/8.
- Classifies the smoothed forward delay variation using RTT-specific high and low thresholds. In the default mode, a trend must persist for two samples before the FWS state changes.
- Holds the sending rate during a pending FWS transition. In Growing, the rate is halved at most once per smoothed RTT. Stable uses half AI; Relieving uses AI, then 2x AI after eight continuing relief observations.
- Applies a separate 10% decrease on every RoCE NACK. CNP and other event types leave the rate unchanged.
- Rejects zero or discontinuous timestamp intervals above one second before using them for rate control.

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
meson setup build-longcc --prefix=/opt/mellanox/doca
```

### Build

```bash
meson compile -C build-longcc
```

### Clean

```bash
meson compile -C build-longcc --clean
```

### Reconfigure and rebuild

```bash
meson setup --reconfigure build-longcc
meson compile -C build-longcc
```

### Install

```bash
meson install -C build-longcc
```

## Build Options

```bash
# Enable TX counter sampling
meson configure build-longcc -Denable_tx_counter_sampling=true

# Set DPACC MCPU flag (default: nv-dpa-bf3)
meson configure build-longcc -Ddpacc_mcpu_flag=nv-dpa-bf3
```

## Usage

### Basic Usage

```bash
./build-longcc/simple_cc_host -d <RDMA device>
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
./build-longcc/simple_cc_host -d mlx5_0

# Run with custom threads
./build-longcc/simple_cc_host -d mlx5_0 -t "176 177 178"
```

### Signal Handlers

- `SIGINT` (Ctrl+C): Graceful shutdown
- `SIGUSR1`: Toggle debug mode / dump debug info

## Algorithm parameters and ablations

RTT-specific thresholds and rate values are compile-time constants in `device/rp/simple_cc/algo/simple_cc_algo_params.h`. DOCA rate values use 20-bit fixed point: `1 << 20` is the configured 100 Gbps ceiling; `1 << 17` is the 12.5 Gbps minimum. The OWD thresholds are in nanoseconds.

| RTT class | High | Low | AI | MD | Minimum rate |
|-----------|-----:|----:|---:|---:|-------------:|
| 1 ms | 300 | -100 | 800 | 0.5 | 131072 |
| 5 ms | 200 | -90 | 100 | 0.5 | 131072 |
| 10 ms | 220 | -100 | 50 | 0.5 | 131072 |
| 20 ms | 80 | -50 | 50 | 0.5 | 131072 |
| 30 ms | 80 | -50 | 50 | 0.5 | 131072 |
| 50 ms | 80 | -30 | 50 | 0.5 | 131072 |

The only writable DOCA algorithm parameter is ID 0, the ablation mode. This replaces the former MD/AI/MIN_RATE parameters, which were exposed to the Host but ignored by the RTT-specific controller.

| Mode | FWS persistence | OWD decrease cooldown |
|------|-----------------|-----------------------|
| 0 (default) | On | On |
| 1 (legacy) | Off | Off |
| 2 (FWS only) | On | Off |
| 3 (TRA only) | Off | On |

The initial mode can be selected at device compilation with `LONGCC_DEFAULT_MODE=0`, `1`, `2`, or `3`. Use a separate Meson build directory for each mode because environment-variable changes alone do not trigger a rebuild:

```bash
LONGCC_DEFAULT_MODE=2 meson setup build-longcc-fws --prefix=/opt/mellanox/doca
meson compile -C build-longcc-fws
```

The DOCA algorithm parameter callback also accepts mode changes at runtime if your management tooling supports it.

## Probe Packet Formats

The application supports multiple probe packet formats:

| Format | Description |
|--------|-------------|
| `PCC_DEV_PROBE_PACKET_CCMAD` | Congestion Notification MAD (default) |
| `PCC_DEV_PROBE_PACKET_IFA1` | In-band Flow Analyzer v1 |
| `PCC_DEV_PROBE_PACKET_IFA2` | In-band Flow Analyzer v2 |

Default is CCMAD. Use `probe_packet_format` in `pcc_config_t` to change the packet format. This is not an adaptive probe scheduling control.

## PCC Roles

- **RP (Reaction Point)**: Adjusts sending rate based on congestion signals (default)
- **NP (Notification Point)**: Generates congestion notifications

## Testing with qperf

### Bidirectional Test (RC)

```bash
# Receiver (ns1)
sudo ip netns exec ns1 /root/pcc-LongCC/build-longcc/simple_cc_host -d mlx5_0

# Sender (ns2)
sudo ip netns exec ns2 /root/pcc-LongCC/build-longcc/simple_cc_host -d mlx5_1

# Run bandwidth test
sudo ip netns exec ns2 qperf -cm1 -t 30 192.168.123.1 rc_bw
```

### Unidirectional Test (UD)

```bash
# Receiver
sudo ip netns exec ns1 /root/pcc-LongCC/build-longcc/simple_cc_host -d mlx5_0

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

The first 119 OWD events print RTT, delay variation, state, mode and rate information to the device trace buffer. NACK prints are limited to the first 20 events and every 100th event afterward. This diagnostic output is not a complete per-flow time series.

The algorithm counters currently track processed events and NACKs.

## Architecture

### Host Side (pcc.c)

The host application:
1. Parses command-line arguments
2. Initializes DOCA PCC context
3. Starts PCC on the specified device
4. Handles signals for shutdown and debug

### Device Side (device/)

DPA kernel code that runs on the BlueField DPU processes RTT probe responses and NACKs, updates the FWS state and sending rate, and maintains per-flow state in the 48-byte DOCA algorithm context.

### Key Data Structures

The per-flow context stores the current rate, previous probe timestamps, smoothed RTT and OWD gradient, FWS state and persistence counters, relief history, and the timestamp of the last OWD-triggered decrease.

## Performance Tuning

### Rate Parameters

Adjust `simple_cc_algo_params.h` and rebuild to change the RTT-specific thresholds, AI, MD, minimum rate, or 100 Gbps ceiling. Retune and rerun the experiments after changing these values; the current values are empirical, not derived from a stability proof.

### Thread Affinity

For optimal performance, configure PCC threads to run on specific CPU cores:

```bash
./build-longcc/simple_cc_host -d mlx5_0 -t "176 177 178 179"
```

## Troubleshooting

### Common Issues

1. **Device not found**: Ensure DOCA is installed and device supports PCC
2. **Build failures**: Verify the DOCA SDK is installed under `/opt/mellanox/doca`
3. **Permission denied**: May need root privileges for RDMA device access

### Check Device PCC Support

```bash
doca_devinfo -l | grep -i pcc
```

### Verbose Build

```bash
meson setup --reconfigure build-longcc
meson compile -C build-longcc -v
```

## License

This code is licensed under the BSD-3-Clause license. See LICENSE file for details.

## References

- [DOCA PCC Programming Guide](https://docs.nvidia.com/doca/sdk/programmable-conestion-control-guide/index.html)
- [NVIDIA BlueField DPU Documentation](https://docs.nvidia.com/doca/)
- [PCC Architecture](https://github.com/eth-pcc/arch)
