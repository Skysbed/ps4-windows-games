# Steam Integration for PCPS4

## Overview

This directory contains files for integrating Steam into the PCPS4 Wine runtime on PS4.

## Requirements

For Steam to work on PS4 via PCPS4, you need:

1. **Steam Client Files**: The Windows Steam client files (steam.exe, steam.dll, etc.)
2. **Network Access**: GoldHEN provides network access
3. **Storage**: Enough space in `/data/pcps4/steam/` for Steam files

## Setup Instructions

### 1. Install Steam Files

Copy the Steam client files to `/data/pcps4/steam/` on your PS4:
- `steam.exe`
- `steam.dll`
- All other required Steam runtime files
- `steamapps/` directory (contains installed games)

### 2. Configuration

The `pcps4.txt` file in this directory configures Steam for the PCPS4 launcher.

### 3. Game Installation

To install Steam games:
1. Launch Steam from the PCPS4 launcher
2. Sign in to your Steam account (may require offline mode)
3. Download and install games to `/data/pcps4/steam/steamapps/`

### 4. Running Steam Games

Steam games can be launched:
- Directly from Steam after installation
- Or by creating individual game configurations in the PCPS4 launcher

## Network Configuration

Steam requires internet access to:
- Sign in to your account
- Download games
- Verify game files
- Access online features

GoldHEN provides network access, but some Steam features may not work properly
on PS4 due to:
- Missing Windows system components
- Different architecture
- Steam DRM requirements

## Offline Mode

For better compatibility, use Steam in offline mode:
1. Sign in to Steam on a PC
2. Enable offline mode
3. Copy the Steam files to your PS4
4. Run Steam in offline mode on PS4

## Limitations

Current limitations:
- Steam overlay may not work
- Some DRM-protected games may not launch
- Controller input may need configuration
- Audio may not work (WIP)

## Files in this Directory

- `pcps4.txt`: PCPS4 launcher configuration for Steam
- `steam_wrapper.sh`: Optional wrapper script for Steam (not yet implemented)

## Future Enhancements

Planned improvements:
- Automatic Steam client download
- Better controller support
- Audio integration
- Steam overlay support
- Game-specific configuration
