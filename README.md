# Snipes

This is a modern port of the classic 1982 text-mode game Snipes. The code has been reverse-engineered from the original DOS executable, and has 100% identical game logic.

For more information, see the [vogons.org forum thread](https://www.vogons.org/viewtopic.php?f=7&t=49073).

### Building

#### Dependencies

SDL builds require the SDL2 and SDL2_ttf libraries.

On startup, the SDL build will attempt to load a custom font, which can be obtained separately from [here](http://kingbird.myphotos.cc/ee22d44076adb8a34d8e20df4be3730a/SnipesConsole.ttf).

#### With Visual Studio

1. Copy `config-sample.h` to `config.h` (and edit as/if desired)
2. Open and build the Visual Studio solution file (`Sniper.sln`).
   The Visual Studio project file has configurations targeting SDL and Windows console graphics.

#### With GNU Make

1. (Optional) Copy `config-sample.h` to `config.h`, and edit as desired
2. Run `make` to compile an SDL build.

For Arch Linux, you can use the [snipes-git](https://aur.archlinux.org/packages/snipes-git/) AUR package.

### Replay recording

This version automatically records replay files of played games. By default, replay files are saved to the current directory, and have a `.SnipesGame` file extension.
If you uncomment the `#define REPLAY_FOLDER` line in config.h and enter a valid path, the `.SnipesGame` replays will be saved to that directory rather than the current directory.

To play back a replay file, pass it as the first argument to the game program, e.g.:

```
$ ./snipes "2016-07-08 09.10.11.SnipesGame"
```

### License

As this is a reverse-engineered port, copyright is retained by the original authors of Snipes.

This reverse-engineered source code is released with the original authors' permission.
