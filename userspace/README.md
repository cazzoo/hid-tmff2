# T500RS Userspace Force Feedback Driver

Professional userspace driver for the Thrustmaster T500RS racing wheel with full force feedback support.

## Features

### Core Functionality
- ✅ **Full Force Feedback** - All effect types supported
- ✅ **Autocenter Control** - Adjustable spring-based autocenter
- ✅ **Gain Control** - Master gain adjustment
- ✅ **Multi-Effect Mixing** - Multiple simultaneous effects
- ✅ **Professional Infrastructure** - Config, logging, error handling, stats

### Quick Start

```bash
# Build
make -f Makefile.modular

# Run
sudo ./t500rs-ffb-modular
```

### Configuration

Copy and edit the config file:
```bash
cp t500rs.conf.example t500rs.conf
# Edit t500rs.conf to customize settings
```

See `t500rs.conf.example` for all available options.

### Documentation

- **[docs/](docs/)** - Complete documentation (see [docs/INDEX.md](docs/INDEX.md))
  - **[Refactoring Summary](docs/refactoring/REFACTORING_COMPLETE.md)** - What was accomplished
  - **[Protocol Reference](docs/technical/FFB_PROTOCOL_COMPLETE.md)** - Technical details
  - **[Development Guide](docs/development/QUICK_REFERENCE.md)** - Developer reference
- **t500rs.conf.example** - Configuration file with all options documented
- **Source code** - Comprehensive Doxygen-style comments

### Performance

The driver displays statistics on exit showing:
- USB efficiency (spam prevention working)
- Effect statistics
- Force update performance
- Uptime and throughput

Typical performance: < 2% CPU, 50Hz updates, 90%+ USB spam reduction

### Troubleshooting

**Device not found**: Check `lsusb | grep 044f`
**Permission denied**: Run with sudo or add udev rule
**No FFB**: Check gain settings and game configuration

For detailed documentation, see the header files and source code comments.
