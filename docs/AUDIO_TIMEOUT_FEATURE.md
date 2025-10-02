# Audio Initialization Timeout Feature

## Problem Statement

The audio system (PortAudio with ALSA/PulseAudio/JACK backends) could freeze indefinitely during initialization on certain systems. This caused the entire application to hang during startup, making the engine unusable on systems with problematic audio configurations.

## Solution

Implemented a safe audio initialization function with timeout support that:

1. **Prevents application freezes** - Uses SDL threading to run audio initialization in a separate thread
2. **Graceful fallback** - Returns false after timeout, allowing the application to continue without audio
3. **User feedback** - Provides clear logging about what went wrong and how to fix it
4. **Safe state management** - Ensures mixer mutex is initialized even if timeout occurs

## API Changes

### New Function: `ame_audio_init_safe`

```c
bool ame_audio_init_safe(int sample_rate_hz, int timeout_ms);
```

**Parameters:**
- `sample_rate_hz`: Preferred sample rate (e.g., 48000). If 0, defaults to 48000.
- `timeout_ms`: Timeout in milliseconds (e.g., 3000 for 3 seconds)

**Returns:**
- `true` on successful initialization
- `false` on timeout or failure

**Example usage:**
```c
bool audio_available = ame_audio_init_safe(48000, 3000); // 3 second timeout
if (audio_available) {
    SDL_Log("Audio initialized successfully");
} else {
    SDL_Log("Continuing without audio");
}
```

### Modified Function: `ame_audio_init`

The existing `ame_audio_init()` function remains unchanged for backward compatibility, but now cooperates with `ame_audio_init_safe()` to avoid double-initializing mutexes.

## Implementation Details

### Timeout Mechanism

1. Pre-initialize `g_mixer` mutex before spawning thread
2. Spawn SDL thread to call `ame_audio_init()`
3. Poll for completion every 10ms
4. On timeout:
   - Detach thread (cannot safely cancel PortAudio blocking calls)
   - Return false with helpful error message
   - Leave mutex in valid state for sync functions

### Safety Guards

Added checks in audio sync functions to prevent crashes when audio system is not running:

```c
void ame_audio_sync_sources_refs(const struct AmeAudioSourceRef *refs, size_t count) {
    if (!atomic_load(&g_mixer.running)) {
        return; // Audio not initialized, do nothing
    }
    // ... rest of function
}
```

## Testing

### Standalone Test

A simple test program (`/tmp/test_audio_timeout.c`) demonstrates the timeout working correctly:

```
Testing audio init with 3 second timeout...
[ame_audio] Audio initialization timed out after 3009 ms
Audio initialization failed or timed out (expected behavior)
Test completed successfully - no crash
```

### Integration Test

The `unitylike_audio_example` now uses safe initialization and continues running even when audio times out.

## User Guidance

When timeout occurs, users see:

```
[ame_audio] Audio initialization timed out after 3000 ms
[ame_audio] This is likely due to audio backend issues (ALSA/PulseAudio/JACK probe hang)
[ame_audio] The application will continue without audio.
[ame_audio] To force a specific audio backend, set AME_AUDIO_HOST (e.g., pulse, alsa, jack)
```

Users can force a specific backend by setting the environment variable:
```bash
export AME_AUDIO_HOST=pulse  # or alsa, jack, etc.
./my_app
```

## Known Limitations

1. **Thread detachment**: If initialization times out, the thread continues running in the background until PortAudio completes or fails. This is unavoidable as PortAudio provides no cancellation mechanism for blocking initialization calls.

2. **Headless environments**: In truly headless environments (no audio devices at all), initialization will still take the full timeout duration. Consider using shorter timeouts (e.g., 1000ms) or skipping audio initialization entirely in such environments.

## Recommendations

### For Application Developers

1. **Always use `ame_audio_init_safe()` instead of `ame_audio_init()`**
   ```c
   bool audio_ok = ame_audio_init_safe(48000, 3000);
   ```

2. **Check return value and handle gracefully**
   ```c
   if (!audio_ok) {
       // Disable audio-dependent features
       // Show UI notification
       // Continue with visual-only mode
   }
   ```

3. **Allow users to disable audio** via command-line flag:
   ```c
   if (!args.disable_audio) {
       audio_available = ame_audio_init_safe(48000, 3000);
   }
   ```

### For Production Deployments

1. **Test on target hardware** - Audio backend behavior varies widely
2. **Provide audio diagnostics** - Log which backend was selected
3. **Document audio requirements** - List tested backends and configurations
4. **Consider separate audio process** - For mission-critical applications, run audio in a separate process that can be killed on timeout

## Files Modified

- `include/ame/audio.h` - Added `ame_audio_init_safe()` declaration
- `src/audio.c` - Implemented safe initialization with timeout and safety guards
- `examples/unitylike_audio_example/main.cpp` - Updated to use safe initialization

## Dependencies

- SDL3 threading API (`SDL_CreateThread`, `SDL_WaitThread`, `SDL_DetachThread`)
- C11 atomics (`_Atomic`, `atomic_store`, `atomic_load`)

## Future Improvements

1. **Async audio initialization** - Return immediately and notify via callback when ready
2. **Audio hotplugging** - Retry initialization if audio device becomes available later
3. **Per-backend timeouts** - Some backends are slower than others
4. **Audio initialization progress** - Report which phase is taking time (ALSA probe, device open, stream start)
