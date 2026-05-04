# agi-up

Sierra AGI picture viewer and enhancer.

This program only works on PIC files. You can use a program like [AGI Studio](https://sciprogramming.com/scitools.php?id=7)
to extract PIC files from an AGI game.

## Building

Requires 
- C23 compiler (or at least one that allows bool)
- cmake
- GLFW3

```sh
brew install glfw
```

Configure and build with CMake:

```sh
cmake -B build
cmake --build build
```

## Usage

```
./build/agi_up [-mN] [-b] <pic-file>
```

| Option | Description |
|--------|-------------|
| `-mN`  | Open on monitor N (default: 1). E.g. `-m2` opens on the second monitor. |
| `-b`   | Write the rendered image to `<pic-file>.bmp` at the original 160×168 resolution. |

Examples:

```sh
./build/agi_up kq1.pic
./build/agi_up -m2 sq2.pic
./build/agi_up -b kq2.pic
```

### Keyboard controls

| Key | Action                                              |
|-----|-----------------------------------------------------|
| `→` / `←` | Step forward / backward through drawing commands    |
| `H` | Toggle between original resolution and 6× upscale   |
| `E` | Toggle enhance mode (hybrid high-resolution render) |
| `,` | Enhance: spread all coloured pixels one step        |
| `.` | Enhance: spread line pixels only                    |
| `/` | Enhance: spread fill pixels only                    |
| `Esc` | Quit                                                |

For best results switch to enhance mode and press `.` twice to spread the line pixels. Then hold down `/` to spread the 
fill pixels. Finish by using `,` to fill in any gaps. 