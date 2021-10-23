# ESPboy_ZX48 LinKeFong core and improved sound rendering
ESPboy emulator of the famous ZX Spectrum 48 machine. Let's play retro ZX48 games )
   
v1.4 19.10.2021 Speed optimizations, palette corrections, PROGMEM demo game load

v1.3 13.08.2021 Moving from libzymosis to z80emu by Lin Ke-Fong to improve performance and improving sound rendering   
    
v1.2 06.01.2020 bug fixes, onscreen keyboard added, keyboard module support

v1.1 23.12.2019  z80 format v3 support, improved frameskip, screen and controls config files

v1.0 20.12.2019 initial version, with sound

by Shiru

shiru@mail.ru

https://www.patreon.com/shiru8bit

uses Z80 core by Lin Ke-Fong

IMPORTANT: the project consumes a lot of RAM, to allow enough set
- SSL Support to Basic
- IwIP to Lower memory (no features)
- Use 160 MHz for much better performance.
- Make sure all the display driver and pin comnenctions are correct by editting the User_Setup.h file in the TFT_eSPI library folder.
- Set SPI_FREQUENCY to 39000000 for best performance.
- Games should be uploaded into LittleFS as 48K .z80 snapshots (v1,v2,v3)
- You can create such snapshots using ZXSPIN emulator
- You can also put a 6912 byte screen with the exact same name to be displayed before game
- You can provide an optional controls configuration file to provide convinient way to control a game. Such file has extension of .cfg, it is a plain text file that contains a string 8 characters long. The characters may represent a key A-Z, 0-9, _ for space, $ for Enter, @ for CS, # for SS
   Warning! This file overrides manual controls selection in file manager
- The order of characters is UP,DOWN,LEFT,RIGHT,ACT,ESC,LFT,RGT 
for example: QAOPM_0$ 
I.e. a game file set may look like:
- game.z80 - snapshot
- game.scr - splash screen in native ZX Spectrum format
- game.cfg - for keys settigs 
