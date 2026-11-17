# Proton DLL Injector for Linux

A robust, automated C++ DLL injector and Bash wrapper designed for Linux gamers using Wine/Proton. It is specifically built to bypass isolated container namespaces (like Bubblewrap/Pressure-Vessel used by Heroic Games Launcher or Steam) by utilizing `nsenter`. 

It automatically clones the exact Proton environment variables, filters out internal process locks (`WINESERVERSOCKET`), and injects mods directly into the running game process without crashing.

## 🧪 Tested Environment
- **OS:** Fedora / Nobara Linux
- **Launcher:** Heroic Games Launcher
- **Compatibility Tool:** Proton-CachyOS (v11.0) / Wine
- **Tested Game:** Grand Theft Auto V (Enhanced Edition)

## 📥 Download & Setup

**1. Clone the repository**
Download the project directly to your machine using `git`:
```bash
git clone [https://github.com/YannekXOlenso/proton-dll-injector.git](https://github.com/YannekXOlenso/proton-dll-injector.git)
cd proton-dll-injector
```

**2. Install Dependencies**
You need the MinGW cross-compiler to build the Windows executable natively on Linux. On Fedora/Nobara, install it via:
```bash
sudo dnf install mingw64-gcc-c++ mingw64-winpthreads-static
```

**3. Compile the Injector**
Compile the C++ code statically. This ensures it doesn't require any missing runtime DLLs inside the Proton prefix:
```bash
x86_64-w64-mingw32-g++ -O2 -static -static-libgcc -static-libstdc++ injector.cpp -o injector.exe
```

## 📂 Directory Structure

For the script to work flawlessly, place the compiled `injector.exe`, the `direct_inject.sh` script, and your mod menu DLL inside your **main game folder** (where the game's executable is located). 

Example structure:
```text
/home/user/Games/GTAVEnhanced/
├── GTA5_Enhanced.exe
├── ModMenu.dll           <-- Your Mod Menu (.dll or .asi)
├── injector.exe          <-- Compiled C++ Injector
└── direct_inject.sh      <-- Bash Wrapper Script
```

## ⚙️ Configuration & Usage

**1. Edit the Script**
Open `direct_inject.sh` in a text editor and adjust the configuration variables at the top to match your specific game and mod:
```bash
GAME_DIR="/home/user/Games/GTAVEnhanced"
LINUX_EXE="GTA5_Enhanced.e"     # The 15-character truncated name seen in the Linux System Monitor
WINE_EXE="GTA5_Enhanced.exe"    # The full Windows executable name
MOD_DLL="ModMenu.dll"           # The exact name of your mod DLL
```

**2. Make it Executable**
Apply execution permissions to the bash script:
```bash
chmod +x direct_inject.sh
```

**3. Inject!**
1. Start your game (e.g., via Heroic) and load fully into the game world (Story Mode). Do not inject during loading screens.
2. Alt-Tab out, open a terminal in your game folder, and run:
   ```bash
   ./direct_inject.sh
   ```
3. Check your desktop notifications. The script will automatically find the running game, enter its container, and safely inject the DLL. You can now open your mod menu in-game.
