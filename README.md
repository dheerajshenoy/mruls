# MRULS

MRULS is a TUI companion for SLURM task management.

# Installation

```bash
git clone https://github.com/dheerajshenoy/mruls
cd mruls
mkdir build && cd build
cmake ..
make
make install # if you can
```

# Usage

```bash
mruls --help
```

This will display the help message with available options. You can run `mruls` without any arguments to see your current SLURM tasks for the current user. You can also specify a different user with the `-u` or `--user` option.

# Requirements

- C++17
- CMake
- slurm (of course)

# How it works

- MRULS parses the `squeue` command output to fetch the list of tasks and their details.
- It then displays this information in a TUI, allowing users to navigate through their tasks and view details.

# TODO

- [ ] Search and filter tasks
- [ ] View task logs
- [ ] Support for more SLURM commands (e.g., `sacct` for historical data)
