# Audio Opus Example

Plays an external .opus file via the engine audio mixer (PortAudio + libopusfile).

Usage
- From the build directory after building this repo:
  ./audio_opus_example /path/to/file.opus [--no-loop]

Controls
- Space: play/pause
- L: toggle loop
- Left/Right: pan
- Up/Down: gain +/-
- Mouse drag horizontally: pan
- R: restart from beginning
- Esc or close window: quit

Notes
- Decoding happens at load; the entire track is stored as interleaved stereo float32 in memory.
- The audio backend and device can be influenced with AME_AUDIO_HOST environment variable (e.g., pulse, alsa, jack).

AI note for examples
- Keep debug prints behind a DEBUG-only macro if you add diagnostics.
- Do not add noisy key or per-frame logs; keep output clean in Release builds.
