# VLC 3.0.x — native OSC interface prototype

This branch introduces a first source-level prototype for an OSC-over-UDP control interface.

## Files added

- `modules/control/osc.c`: OSC packet parser and VLC control calls.
- `patches/modules-control-Makefile.am.patch`: proposed build-system registration for the VLC 3.0.x source tree.

## OSC messages (UDP port 8000 by default)

| Address | OSC type tag | Action |
|---|---|---|
| `/vlc/play` | `,` | Play |
| `/vlc/pause` | `,` | Toggle pause |
| `/vlc/stop` | `,` | Stop |
| `/vlc/next` | `,` | Next playlist item |
| `/vlc/previous` | `,` | Previous playlist item |
| `/vlc/volume` | `,i` | Set volume (0–256) |
| `/vlc/seek` | `,f` | Seek to normalized position (0.0–1.0) |

## Current status

**Not yet compiled or tested against VLC 3.0.24.** The source/API compatibility and the exact Makefile integration still require validation against the full VLC 3.0.x source tree. Do not treat this branch as a ready-to-install plugin.

The UDP receiver has no sender authentication; restrict access to a trusted network or firewall the port.
