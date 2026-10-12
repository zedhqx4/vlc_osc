# Native OSC control for VLC 3.0.x

This branch targets the VLC 3.0.x source line and adds an OSC-over-UDP interface as a built-in VLC control plugin.

## Build integration

- `modules/control/osc.c` implements the interface.
- `modules/control/Makefile.am` registers `libosc_plugin.la` in `control_LTLIBRARIES` and links the socket and math libraries.

## Enable it

After building VLC from this branch, start it with:

```sh
vlc --extraintf=osc --osc-port=8000
```

The default UDP port is 8000.

## OSC messages

| Address | OSC type tag | Action |
|---|---|---|
| `/vlc/play` | `,` | Play |
| `/vlc/pause` | `,` | Toggle pause |
| `/vlc/stop` | `,` | Stop |
| `/vlc/next` | `,` | Next playlist item |
| `/vlc/previous` | `,` | Previous playlist item |
| `/vlc/volume` | `,i` | Set volume from 0 to 256 |
| `/vlc/seek` | `,f` | Seek to normalized position from 0.0 to 1.0 |

## Validation status

The source and Automake registration have been added, but this branch has **not yet been compiled or runtime-tested** in this environment. Treat it as an integration candidate until a VLC 3.0.x build confirms compiler and linker compatibility.

The receiver does not authenticate OSC senders. Keep UDP port 8000 on a trusted network or restrict it with the host firewall.
