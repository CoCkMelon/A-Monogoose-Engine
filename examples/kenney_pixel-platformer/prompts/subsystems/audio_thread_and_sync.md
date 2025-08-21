Title: Subsystem – Audio Thread and Source Synchronization

Purpose
Initialize audio, run a dedicated audio thread that reacts quickly to gameplay events via atomics, and keep the device feed in sync by submitting references.

Initialization
- ame_audio_init(sample_rate=48000)
- Load sources (Opus files):
  - music: loop=true, gain=0.3, pan=0
  - ambient: loop=true, gain=base_gain, pan=0
  - jump_sfx: loop=false, gain~0.6, pan=0, playing=false
- Expose pointers via CAudioRefs on the World entity; expose CAmbientAudio(x,y,base_gain)

Audio Thread Loop (~1 ms cadence)
- While !should_quit:
  - Apply atomics to sources:
    - ambient.pan = ambient_pan_atomic; ambient.gain = ambient_gain_atomic
  - If jump_sfx_request is true (exchange false):
    - jump_sfx.u.pcm.cursor = 0; jump_sfx.playing = true; jump_sfx.pan = 0
  - Build refs array with {src*, stable_id}
  - ame_audio_sync_sources_refs(refs, count)
  - SDL_DelayNS(1e6) // ~1ms

Interaction with Systems
- SysAudioUpdate computes ambient pan/gain using ame_audio_ray_compute and sets jump_sfx_request when CInput.jump_trigger is true.

Shutdown
- Signal should_quit, join audio thread
- ame_audio_shutdown()

