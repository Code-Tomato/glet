# Glet - GPU Scheduler

This repository is a fork of the [Gpu-let Prototype](https://github.com/casys-kaist/glet) by the CASYS research group at KAIST, cleaned up to focus on the scheduler component only.

Original authors:
- CASYS, KAIST
- Contact: sbchoi@casys.kaist.ac.kr

## Building the Scheduler

1. Install dependencies:
```bash
./install_dependencies.sh
```

2. Build the scheduler:
```bash
./scripts/build.sh
```

The binary will be created at `bin/standalone_scheduler`.

## Running the Scheduler

To run the scheduler, use the `run_scheduler.sh` script:

```bash
./scripts/run_scheduler.sh -t ../resource/SUS-GPUS_INPUT.CSV
```

For more options and usage information:

```bash
./scripts/run_scheduler.sh --help
```

## Dependencies

- CMake 3.12+
- Boost libraries (boost_program_options, boost_system)
- Google Logging library (glog)
