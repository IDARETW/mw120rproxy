# Imported doors

> **POC status:** this describes the current conversion and proxy code. Rebuild and test door maps
> before relying on opening, closing, bashing, collision, or interaction behavior.

Imported brush doors support Use to open and close, melee bashing, and running
into a closed door to bash it open. Hinged doors swing away from the player.
Adjacent double-door leaves operate together. Moving leaves remain solid, and a
closing door reverses if a player blocks it.

## Super Mario 64

The updated `mp_dr_sm64` package includes both castle entrance leaves, the small
room door, the secret-room door, and the sliding start gate. Select **Super Mario
64** from Local Play's normal map menu.

Aim at a door within arm's reach and press your normal **Use/Interact** binding.
Aim at the open leaf to close it. Melee or sprint into a closed door to bash it.
The game's interaction popup shows whether Use will open or close the door,
with the current keyboard binding or controller button. Keyboard Activate and
controller Use/Reload are both supported. Reload by itself does not open doors.

The original deathrun puzzles and unlock triggers are not required for these
doors. The rest of the CoD4 gameplay script is not executed.

## Map conversion

The legacy imported-map builder recognizes named `script_brushmodel` doors with literal
`getEnt`, `linkto`, `rotateYaw`, or `moveX`/`moveY`/`moveZ` definitions in the map's
GSC source. The door's name must contain `door`. Hinged doors use their authored
linked pivot, and sliding gates retain their authored travel distance.

This legacy package format stores moving door data in `doors.bin` and records
`"doors": "brush-poses-v1"` in `manifest.json`. Keep those files with that
legacy package. Direct `build-iw3` output is fastfile-only and does not create
door sidecars; it can still be installed without a manifest.

This supports authored brush doors in Local Play. It does not turn arbitrary
decorative models into doors or run general scripted movers, elevators, keys,
or deathrun traps. The imported door animation does not add the game's native
hand interaction animations or network replication.
