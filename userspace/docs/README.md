# T500RS Driver Documentation

Welcome to the T500RS userspace force feedback driver documentation!

## Start Here

- **New Users**: Read the [Main README](../README.md) in the parent directory
- **Developers**: See [INDEX.md](INDEX.md) for complete documentation index
- **Quick Reference**: See [development/QUICK_REFERENCE.md](development/QUICK_REFERENCE.md)

## Documentation Organization

This directory contains all technical documentation, organized by topic:

### 📁 [refactoring/](refactoring/)
Documentation of the refactoring process that transformed the driver from a monolithic file to a professional modular architecture.

**Key Documents**:
- `REFACTORING_COMPLETE.md` - Summary of what was accomplished
- `REFACTORING_UNIFIED.md` - Complete refactoring plan

### 📁 [technical/](technical/)
Deep technical documentation about the force feedback protocol and implementation.

**Key Documents**:
- `FFB_PROTOCOL_COMPLETE.md` - Complete protocol reference
- `FFB_EFFECTS_GUIDE.md` - Effects implementation guide
- `ENVELOPE_IMPLEMENTATION.md` - Envelope processing details

### 📁 [development/](development/)
Guides for developers working on or extending the driver.

**Key Documents**:
- `QUICK_REFERENCE.md` - Developer quick reference
- `LOGGING_GUIDE.md` - Debugging with the logging system
- `TESTING_QUICKSTART.md` - Testing guide

### 📁 [fixes/](fixes/)
Documentation of specific bugs and their solutions.

**Key Documents**:
- `MODE_SWITCH_FIX.md` - USB mode switch solution
- `USB_ERROR_FIX.md` - USB error handling improvements

### 📁 [archive/](archive/)
Historical documentation kept for reference.

### 📁 [analysis/](analysis/)
USB capture analysis and protocol reverse engineering.

### 📁 [planning/](planning/)
Future improvement plans and ideas.

## Quick Links

### For Users
- [Main README](../README.md) - User guide and quick start
- [Configuration](../t500rs.conf.example) - Configuration template

### For Developers
- [Complete Index](INDEX.md) - Full documentation index
- [Protocol Reference](technical/FFB_PROTOCOL_COMPLETE.md) - Protocol details
- [Architecture](refactoring/REFACTORING_COMPLETE.md) - Driver architecture

### For Troubleshooting
- [Main README Troubleshooting](../README.md#troubleshooting) - Common issues
- [Mode Switch Fix](fixes/MODE_SWITCH_FIX.md) - Device initialization
- [USB Errors](fixes/USB_ERROR_FIX.md) - USB communication

## Documentation Standards

All documentation follows these standards:

- **Markdown format** - Easy to read and version control
- **Clear structure** - Organized by topic
- **Up to date** - Reflects current implementation
- **Cross-referenced** - Links between related documents

## Need Help?

1. Check the [Main README](../README.md) first
2. Browse the [Documentation Index](INDEX.md)
3. Search for specific topics in the appropriate folder
4. Check the [archive/](archive/) for historical context

---

**See [INDEX.md](INDEX.md) for the complete documentation index.**

