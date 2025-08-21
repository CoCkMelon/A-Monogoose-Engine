Title: System – Audio Update

Purpose
Compute spatialized ambient audio parameters using audio rays. Trigger jump SFX on jump events. Don't use spatial audio for music, jump SFX.

Query
- in: CAmbientAudio (read)
- in: CPhysicsBody (read from Player)
- in: CInput (read from Player)

Algorithm
- Get px, py from player body
- Build AmeAudioRayParams with listener=(px,py), source=(aa.x, aa.y)
- params: min_distance=32, max_distance=6000, occlusion_db=8, air_absorption_db_per_meter=0.01
- Call ame_audio_ray_compute(physics, &params, &gl, &gr)
- pan = (gr - gl) / max(gl+gr, eps); gain = aa.base_gain * max(gl, gr)
- atomic_store(ambient_pan, pan)
- atomic_store(ambient_gain, gain)
- If in.jump_trigger -> atomic_store(jump_sfx_request, true)

Notes
- Audio thread reads the atomics and syncs sources.

