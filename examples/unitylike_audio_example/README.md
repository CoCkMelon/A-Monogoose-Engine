# Unity-like Spatial Audio Example

This example demonstrates the Unity-like audio facade with spatial audio support, including:

- **AudioSource component** with spatial audio calculations
- **AudioListener component** for audio mixing
- **Physics-based occlusion** using raycasting and acoustic materials
- **Touch-friendly controls** with onscreen buttons
- **Asyncinput integration** for keyboard input
- **Multiple audio sources** with different types (oscillators, sound effects)

## Features

### Audio System
- **Spatial Audio**: Audio sources use distance-based attenuation and directional panning
- **Occlusion**: Walls block sound with different acoustic materials (concrete, wood)
- **Multiple Source Types**: 
  - Sigmoid oscillators for footsteps
  - Saw cut transients for jump sounds
  - Saw work continuous buzzes for ambient sources
- **Unity-like API**: `AudioSource.Play()`, `Stop()`, `volume()`, `spatialAudio(true)`, etc.

### Controls
- **Keyboard**: WASD/Arrow keys for movement, Space for jump, Q/Esc to quit
- **Touch**: Onscreen buttons for movement and actions
- **Hybrid**: Both input methods work simultaneously

### Physics Integration
- Player moves with Rigidbody2D physics
- Walls are static colliders with acoustic materials
- Audio raycasting checks for occlusion between listener and sources

## Building

From the project root:
```bash
mkdir build && cd build
cmake ..
make unitylike_audio_example
./examples/unitylike_audio_example
```

## Usage

- Move the green player square around with WASD/arrows or touch controls
- Notice how audio sources (colored squares) change volume and panning based on distance
- Walk behind walls to hear occlusion effects
- Audio sources pulse visually when playing
- Touch the jump button or press space to make jump sounds

## Implementation Notes

### Audio Architecture
- `AudioSyncSystem` runs in the ECS update loop to sync audio components with the mixer
- Spatial calculations use `ame_audio_ray_compute()` for physics-based attenuation
- The main audio listener follows the camera position
- Each `AudioSource` can be configured with spatial audio parameters

### Touch Controls
- Onscreen buttons are rendered as transparent overlays
- Handles both mouse and touch finger events
- Buttons dynamically resize with window changes
- Visual feedback shows pressed state

This example demonstrates a complete Unity-like audio system suitable for games requiring spatial audio with occlusion.