# discord-kwin-fps-cap

Discord's Linux screen capture asks the Wayland portal for an unbounded framerate. The compositor then defaults to your display's refresh rate, so a 240 Hz monitor has kwin pushing 240 fps into a 60 fps stream. Three quarters of the capture, scale and colour-convert work is done on the GPU and thrown straight away.

This is a shim that supplies the ceiling Discord should be asking for.

Your display keeps its refresh rate. Your game keeps its framerate. The stream is unchanged at 60 fps. The only thing that goes away is the surplus.

## Install

```sh
curl -L https://raw.githubusercontent.com/adds39939/discord-kwin-fps-cap/main/install.sh | bash
```

It finds your Discord launchers, asks before changing anything, and adds the shim to each one. Then fully quit Discord — including the tray icon — and start it again.

Nothing is written outside your home directory, no Discord file is modified, and no root is needed.

### Uninstall

```sh
curl -L https://raw.githubusercontent.com/adds39939/discord-kwin-fps-cap/main/uninstall.sh | bash
```

Launchers you had customised yourself are backed up before being touched and restored on the way out.

### Options

```sh
./install.sh --cap 30    # ceiling, default 60
./install.sh --yes       # no prompts
```

Piping to `bash` needs `-s --` to pass options through:

```sh
curl -L .../install.sh | bash -s -- --cap 30
```

Keep the cap at or above your stream's output framerate; below it you lose stream smoothness.

## How it works

Discord loads PipeWire at runtime with `dlsym`, so the shim interposes `dlsym` and hands back its own `pw_stream_connect`. That wrapper adds a framerate ceiling to the capture format before the compositor ever negotiates it.

Video capture only — audio streams pass through untouched.

## Verifying

With a stream running, both the compositor and Discord should report the ceiling you set:

```sh
pw-dump | grep -A2 maxFramerate | head
```

For a verbose run, `FPSCAP_DEBUG=1` logs every rewrite:

```
[fpscap] active, capping capture maxFramerate at 60
[fpscap] added maxFramerate -> 60
```

`added` means Discord proposed no ceiling and one was supplied; `clamped` means it proposed a higher one that was lowered. If neither line appears, the capture isn't going through `pw_stream_connect` and this shim isn't the right tool.

## Build from source

Needs a C compiler and the PipeWire headers (`libpipewire-0.3` via `pkg-config`).

```sh
git clone https://github.com/adds39939/discord-kwin-fps-cap
cd discord-kwin-fps-cap
./build.sh      # builds and runs the tests
./install.sh    # uses the library you just built
```

To run it once without installing anything:

```sh
LD_PRELOAD=$PWD/libfpscap.so discord
```

## License

MIT — see [LICENSE](LICENSE).
