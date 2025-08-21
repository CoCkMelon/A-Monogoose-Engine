Title: Subsystem – Input Backend (asyncinput)

Purpose
Provide a non-blocking input layer that updates atomics consumed by systems and the main loop. Handles quit signals and keyboard mapping to movement/jump.

Init
- Call ni_init(0); if non-zero, fail early.
- Register callback with ni_register_callback(on_input, NULL, 0). Store errors, and on failure, call ni_shutdown and fail early.

Callback on_input(ev)
- If ev->type == NI_EV_KEY:
  - bool down = (ev->value != 0)
  - Map keys:
    - Left: NI_KEY_LEFT or NI_KEY_A -> left_down = down
    - Right: NI_KEY_RIGHT or NI_KEY_D -> right_down = down
    - Jump: NI_KEY_SPACE or NI_KEY_W or NI_KEY_UP -> jump_down = down
    - Quit: NI_KEY_ESC or NI_KEY_Q when pressed -> should_quit = true
  - Derive move_dir = (right_down?1:0) - (left_down?1:0)
  - Store all to atomics using atomic_store/atomic_exchange where appropriate

Usage
- SysInputGather reads left/right/jump atomics to fill CInput.
- Main loop watches should_quit to exit early.

Shutdown
- Call ni_shutdown() during SDL_AppQuit after setting should_quit and joining threads.

