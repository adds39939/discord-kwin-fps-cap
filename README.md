# discord-kwin-fps-cap

Discord's Linux screen capture asks the Wayland portal for an unbounded framerate. The compositor then defaults to your display's refresh rate, so a 240 Hz monitor has kwin pushing 240 fps into a 60 fps stream. Three quarters of the capture, scale and colour-convert work is done on the GPU and thrown straight away.

This is a shim that supplies the ceiling Discord should be asking for.

Your display keeps its refresh rate. Your game keeps its framerate. The stream is unchanged at 60 fps. The only thing that goes away is the surplus.

## How it works

Discord's capture lives in `discord_voice.node` — its own Rust implementation talking to `org.freedesktop.portal.ScreenCast` directly, not Chromium's capturer. It resolves PipeWire entirely through `dlopen`/`dlsym`:

```
$ nm -D --undefined-only discord_voice.node | grep -c 'pw_'
0
$ strings discord_voice.node | grep libpipewire
libpipewire-0.3.so.0
```

So `LD_PRELOAD` alone can't interpose `pw_stream_connect` — a `dlsym(handle, ...)` lookup searches that handle's library directly and never sees a preloaded copy. The shim therefore interposes **`dlsym` itself**, returns its own wrapper when Discord asks for `pw_stream_connect`, and forwards everything else untouched.

The wrapper rewrites the `EnumFormat` params before negotiation sees them:

- **video formats only** — `SPA_TYPE_OBJECT_Format` with `mediaType == video`. Audio streams pass through.
- if `SPA_FORMAT_VIDEO_maxFramerate` is present, every fraction above the cap is clamped
- if it's **absent** — which is what Discord actually does — it's appended as a `Choice(Range)`
- params are copied into our own buffer first, so the caller's memory is never mutated

## Requirements

- PipeWire and its development headers (`libpipewire-0.3` via `pkg-config`)
- A C compiler
- A Wayland compositor using the ScreenCast portal (developed against kwin; nothing in the shim is kwin-specific)

## Build

```sh
./build.sh
```

Runs the POD-surgery unit tests and the interposition smoke test, then reports the built library.

## Use

```sh
./run-discord.sh
```

Or set it yourself:

```sh
LD_PRELOAD=/path/to/libfpscap.so discord
```

To make it the default for your desktop launcher, copy the system entry and add the preload:

```sh
cp /usr/share/applications/discord.desktop ~/.local/share/applications/
sed -i 's|^Exec=|Exec=env LD_PRELOAD='"$HOME"'/.local/lib/libfpscap.so |' \
    ~/.local/share/applications/discord.desktop
```

### Options

| Variable | Default | Meaning |
|---|---|---|
| `DISCORD_CAPTURE_FPS_CAP` | `60` | Ceiling handed to the portal |
| `FPSCAP_DEBUG` | unset | Log every param rewritten |

With `FPSCAP_DEBUG=1` you should see, once the stream starts:

```
[fpscap] active, capping capture maxFramerate at 60
[fpscap] added maxFramerate -> 60
```

`added` means Discord proposed no ceiling and one was supplied; `clamped` means it proposed a higher one that was lowered. If neither line ever appears, the capture isn't going through `pw_stream_connect` and this shim isn't the right tool.

## License

MIT — see [LICENSE](LICENSE).
