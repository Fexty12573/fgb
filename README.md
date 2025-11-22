# fgb
A somewhat decent GameBoy Emulator

## Features
- Emulates the original GameBoy (DMG)
- M-Cycle accurate CPU emulation
- Pixel FIFO PPU emulation (Not 100% accurate yet)
- Working APU (Not 100% accurate yet)
- MBC1, MBC2, MBC3 and MBC5 cartridge support
- CPU+Timer Debugger with breakpoints and step-by-step execution
- PPU State Viewer

## Usage
After building, run the emulator from the command line with the path to a GameBoy ROM as an argument:
```bash
fgb <path_to_rom>
```
(Or drag a ROM file onto the executable in Windows Explorer)

## Building
### Requirements
- A C11 compatible compiler (e.g. GCC, Clang, MSVC)
- CMake 3.21 or higher

### Windows
Clone the repository and checkout submodules:
```bash
git clone --recurse-submodules
cd fgb
```

a. Open the cloned repository in Visual Studio and hit Ctrl+B to build the project.
b. Alternatively, use CMake from the command line:
```bash
mkdir build
cmake --preset windows-dist
cmake --build build
```

### Linux
Clone the repository and checkout submodules:
```bash
git clone --recurse-submodules
cd fgb
```

Create a build directory and run CMake:
```bash
mkdir build && cd build
cmake ..
make
```
