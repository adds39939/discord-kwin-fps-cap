# discord-kwin-fps-cap

Discord's Linux screen capture asks the Wayland portal for a stream with **no framerate bound**. The compositor fills that vacuum with a ceiling derived from your display's refresh rate — so on a 240 Hz monitor, kwin hands Discord 240 frames a second to produce a 60 fps stream. Three quarters of the capture, scale and colour-convert work is done on the GPU's shaders and then thrown away.

This is an `LD_PRELOAD` shim that supplies the ceiling Discord should be asking for.

```
before:  kwin ──240 fps of 3840x2160──▶ Discord ──60 fps──▶ viewers
after:   kwin ── 60 fps of 3840x2160──▶ Discord ──60 fps──▶ viewers
```

Your display keeps its refresh rate. Your game keeps its framerate. The stream is unchanged at 60 fps. The only thing that goes away is the surplus.

## Measured effect

One machine — RTX 5090, driver 610.57.04, KDE Plasma / kwin 6.7.4, PipeWire 1.6.8, Discord 1.0.153, streaming a fullscreen 4K game from a 3840x2160@240 display.

| | Before | After |
|---|---|---|
| Negotiated `maxFramerate` | 240 | **60** |
| PipeWire node errors (`pw-top` ERR) | **6156**, climbing | **4**, flat |
| Node BUSY per cycle | 7.5 ms | 6.7 ms |
| Encoder latency | 13.7–14.4 ms | 12.4 ms |
| Encoder output | 59 fps | 60 fps |

The error counter is the clearest signal: Discord was being handed buffers faster than it could consume them and erroring on the overflow. With the cap in place the pipeline keeps up.

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

## Verifying

With a stream running:

```sh
pw-dump > /tmp/pw.json
python3 -c '
import json
for o in json.load(open("/tmp/pw.json")):
    i = o.get("info") or {}; p = i.get("props") or {}
    if "Video" not in str(p.get("media.class", "")): continue
    for f in (i.get("params", {}) or {}).get("Format", []) or []:
        print(p.get("node.name"), f.get("size"), "max", f.get("maxFramerate"))'
```

Both the compositor and Discord nodes should report `max {'num': 60, 'denom': 1}`.

## Caveats

- `LD_PRELOAD` reaches every Discord child process. The shim overrides only `dlsym` and forwards everything it doesn't care about, but that is the blast radius.
- Interposing `dlsym` means resolving the real one via `dlvsym(RTLD_NEXT, ...)`. The shim tries `GLIBC_2.34`, then `2.2.5`, then `2.0`.
- Rewritten params are deliberately not freed. `pw_stream_connect` runs a handful of times per session, and leaking a few hundred bytes beats a use-after-free if the callee retains the pointer.
- Capping capture below your stream's output framerate will cost stream smoothness. `60` suits a 60 fps stream.
- Nothing is written to disk and no Discord file is modified. To disable, launch Discord without the preload.

## Why not fix it elsewhere?

Every other avenue was checked first and none is exposed:

- no `KWIN_*` environment variable for screencast framerate
- no `kwinrc` or `xdg-desktop-portal-kde` config key
- no `/VirtualOutputs` DBus interface in KWin 6.7
- `zkde_screencast_unstable_v1::stream_virtual_output` takes name/width/height/scale — **no refresh parameter**
- Discord's `captureVideoFrameRate` transport option exists but doesn't reach the capture negotiation

Lowering the display's refresh rate works, and so does patching kwin to clamp the ceiling it offers — but the first costs you the refresh rate you paid for, and the second means maintaining a compositor fork. This shim targets only the process with the bug.

## License

MIT — see [LICENSE](LICENSE).
