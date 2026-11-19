# Next Steps for PCPS4 Development

## Completed Tasks

### Audio Support
- ✅ Created wineps4 audio driver (based on prospero-win's wineps5)
- ✅ Created PS4 audio platform interface (pw_audio_ps4.c/h)
- ✅ Created Wine audio sink (pw_wine_audio_ps4.c/h)
- ✅ Created registry patch to set default audio driver to "ps4"
- ✅ Created build scripts for audio components

### Steam Integration
- ✅ Created launcher configuration for Steam (pcps4.txt)
- ✅ Created Steam integration documentation (README.md)
- ✅ Defined file structure for Steam support

### Controller/XInput Improvements
- ✅ Created improved xinput implementation (xinput_ps4_pad_v2.c)
- ✅ Added deadzone filtering
- ✅ Improved error handling and reconnection logic
- ✅ Better axis scaling

## Pending Tasks (High Priority)

### Audio
- ⏳ **Implement audio mixing in wineps4 driver**
  - Currently outputs silence
  - Need to mix all active streams into output buffer
  - Resample streams to 48kHz stereo 16-bit
  - Apply volume and panning

- ⏳ **Integrate audio sink into ntdll.prx**
  - Link pw_wine_audio_output into ntdll.prx
  - Or compile as separate module
  - Ensure proper symbol exports

- ⏳ **Test audio with real games**
  - Test with SuperTuxKart
  - Test with Minecraft
  - Verify no crashes or performance issues

### Steam
- ⏳ **Create Steam wrapper/launcher**
  - Handle Steam client startup
  - Set up proper environment
  - Manage Steam pipe communication

- ⏳ **Package Steam runtime in PKG**
  - Include necessary Steam DLLs
  - Configure for PS4 environment
  - Set up proper paths

- ⏳ **Test Steam client on PS4**
  - Verify login works
  - Test game installation
  - Test game launching

### Controller
- ⏳ **Test improved xinput implementation**
  - Verify deadzone works correctly
  - Test reconnection logic
  - Test with multiple games

- ⏳ **Integrate xinput_ps4_pad_v2.c**
  - Replace existing xinput_ps4_pad.c
  - Update build scripts
  - Test on real hardware

## Pending Tasks (Medium Priority)

### Build System
- ⏳ **Update pkg.gp4 manifest**
  - Add wineps4.prx to sce_module list
  - Add audio sink module
  - Update dependencies

- ⏳ **Create unified build script**
  - Build all audio components together
  - Integrate with existing build system
  - Automate PKG creation

### Documentation
- ⏳ **Update main README.md**
  - Document audio support
  - Document Steam integration
  - Update status section

- ⏳ **Update WINE_PS4_STATUS.md**
  - Document audio implementation
  - Document Steam progress
  - Update roadmap

### Network
- ⏳ **Verify network connectivity for Steam**
  - Test with GoldHEN network
  - Configure firewall rules if needed
  - Test DNS resolution

- ⏳ **Add network configuration to launcher**
  - Allow network settings customization
  - Add proxy support if needed
  - Add connection testing

## Testing Checklist

### Audio Tests
- [ ] Build wineps4 driver successfully
- [ ] Build audio sink successfully
- [ ] Verify driver loads without errors
- [ ] Test audio output with test program
- [ ] Test audio in SuperTuxKart
- [ ] Test audio in Minecraft
- [ ] Test with different audio formats

### Steam Tests
- [ ] Build Steam launcher configuration
- [ ] Test Steam client startup
- [ ] Verify network connectivity
- [ ] Test Steam login
- [ ] Test game installation
- [ ] Test game launching

### Controller Tests
- [ ] Build improved xinput implementation
- [ ] Test button mapping
- [ ] Test stick inputs with deadzone
- [ ] Test reconnection after disconnect
- [ ] Test rumble functionality

## Files That Need Integration

### Into the Main Repository
1. `wineps4/` directory - Audio driver source
2. `build-ps4-module/pw_audio_ps4.*` - Audio platform interface
3. `build-ps4-module/pw_wine_audio_ps4.*` - Audio sink
4. `build-ps4-module/xinput_ps4_pad_v2.c` - Improved xinput
5. `steam/` directory - Steam integration files
6. `wine-ps4-audio-driver.patch` - Registry patch

### Into the Build System
1. `build-ps4-module/build_wineps4.sh` - Audio driver build
2. `build-ps4-module/build_audio_sink.sh` - Audio sink build

## Dependencies

### Required for Audio
- OpenOrbis toolchain
- orbis-compat
- SCE AudioOut library (libSceAudioOut.sprx)
- Proper linker scripts

### Required for Steam
- Steam client files
- Network connectivity (GoldHEN)
- Storage space on PS4
- Proper environment variables

## Estimated Time

- **Audio implementation**: 2-4 hours (mixing logic)
- **Audio integration**: 1-2 hours (linking, testing)
- **Steam implementation**: 4-8 hours (wrapper, testing)
- **Controller testing**: 1-2 hours (validation)
- **Total**: 8-16 hours

## Risk Assessment

### Low Risk
- Adding new modules (wineps4, audio sink)
- Improving xinput implementation
- Adding Steam configuration files

### Medium Risk
- Integrating audio sink into ntdll.prx
- Network configuration for Steam
- Audio mixing implementation

### High Risk
- Steam client compatibility on PS4
- Audio driver stability
- Performance impact of audio mixing

## Recommendations

1. **Start with audio** - It's the most self-contained feature
2. **Test incrementally** - Build and test each component separately
3. **Use existing patterns** - Follow prospero-win's approach where possible
4. **Document everything** - Update README and STATUS files
5. **Backup existing code** - Before making changes to critical components

## Contact

For questions or issues with these changes, refer to:
- The PCPS4 repository: https://github.com/Skysbed/ps4-windows-games
- The prospero-win repository (for reference): https://github.com/mpereiraesaa/prospero-win
