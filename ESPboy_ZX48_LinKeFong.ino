//v1.4 19.10.2021 Speed optimizations, palette corrections, PROGMEM demo game load
//v1.3 13.08.2021 Moving from libzymosis to z80emu by Lin Ke-Fong to improve performance and improving sound rendering
//v1.2 06.01.2020 bug fixes, onscreen keyboard added, keyboard module support
//v1.1 23.12.2019 z80 format v3 support, improved frameskip, screen and controls config files
//v1.0 20.12.2019 initial version, with sound
//by Shiru
//shiru@mail.ru
//https://www.patreon.com/shiru8bit

#pragma GCC optimize ("-O3")
#pragma GCC push_options

#include "ESPboyInit.h"

#include <LittleFS.h>
#include <sigma_delta.h>
#include "game.h"

/*
   IMPORTANT: the project consumes a lot of RAM, to allow enough
    - Use SDK 2.7 for compilation
    - Set SSL Support to Basic
    - Set IwIP to Lower memory (no features)
    
   Set 160 MHz for better performance.

   Make sure all the display driver and pin comnenctions are correct by
   editting the User_Setup.h file in the TFT_eSPI library folder.

   Set SPI_FREQUENCY to 39000000 for best performance.

   Games should be uploaded into ESP8266 Flash (LittleFS) as 48K .z80 snapshots (v1,v2,v3)

   You can also put a 6912 byte screen with the exact same name to be displayed before game

   You can provide an optional controls configuration file to provide convinient way to
   control a game. Such file has extension of .cfg, it is a plain text file that contains
   a string 8 characters long. The characters may represent a key A-Z, 0-9, _ for space,
   $ for Enter, @ for CS, # for SS
   Warning! This file overrides manual controls selection in file manager

   The order of characters is UP,DOWN,LEFT,RIGHT,ACT,ESC,LFT,RGT
   for example: QAOPM_0$

   I.e. a game file set may look like:

   game.z80 - snapshot
   game.scr - splash screen in native ZX Spectrum format
   game.cfg - for keys settigs 
*/

ESPboyInit myESPboy;

#include "z80emu.h"

#include "glcdfont.c"
#include "gfx/keyboard.h"
#include "rom/rom.h"

Adafruit_MCP23017 mcpKeyboard;

uint8_t pad_state;
uint8_t pad_state_prev;
uint8_t pad_state_t;
uint8_t keybModuleExist;

#define PAD_LEFT        0x01
#define PAD_UP          0x02
#define PAD_DOWN        0x04
#define PAD_RIGHT       0x08
#define PAD_ACT         0x10
#define PAD_ESC         0x20
#define PAD_LFT         0x40
#define PAD_RGT         0x80
#define PAD_ANY         0xff

uint16_t line_buffer[128] __attribute__ ((aligned(32)));

uint8_t line_change[32 + 1] __attribute__ ((aligned(32))); //bit mask to updating each line, the extra bit is border update flag

uint8_t memory[49152] __attribute__ ((aligned(32)));

uint8_t port_fe;  //keyboard, tape, sound, border
uint8_t port_1f;  //kempston joystick

char filename[32];

Z80_STATE cpu;

bool frame_int;
bool frame_done;
int frame_ticks;

#define ZX_CLOCK_FREQ   3500000
#define ZX_FRAME_RATE   50

#define SAMPLE_RATE     48000   //more is better, but emulations gets slower

#define MAX_FRAMESKIP   4

enum {
  K_CS = 0,
  K_Z,
  K_X,
  K_C,
  K_V,

  K_A,
  K_S,
  K_D,
  K_F,
  K_G,

  K_Q,
  K_W,
  K_E,
  K_R,
  K_T,

  K_1,
  K_2,
  K_3,
  K_4,
  K_5,

  K_0,
  K_9,
  K_8,
  K_7,
  K_6,

  K_P,
  K_O,
  K_I,
  K_U,
  K_Y,

  K_ENTER,
  K_L,
  K_K,
  K_J,
  K_H,

  K_SPACE,
  K_SS,
  K_M,
  K_N,
  K_B,

  K_DEL,
  K_LED,
  K_NULL = 255,
};


constexpr uint8_t keybCurrent[7][5] PROGMEM = {
  {K_Q, K_E, K_R, K_U, K_O},
  {K_W, K_S, K_G, K_H, K_L},
  {255, K_D, K_T, K_Y, K_I},
  {K_A, K_P, K_SS, K_ENTER, K_DEL},
  {K_SPACE, K_Z, K_C, K_N, K_M},
  {K_CS, K_X, K_V, K_B, K_6},
  {K_LED, K_SS, K_F, K_J, K_K}
};

constexpr uint8_t keybCurrent2[7][5] PROGMEM = {
  {K_Q, K_2, K_3, K_U, K_O},
  {K_1, K_4, K_G, K_H, K_L},
  {K_NULL, K_5, K_T, K_Y, K_I},
  {K_A, K_P, K_SS, K_ENTER, K_DEL},
  {K_SPACE, K_7, K_9, K_N, K_M},
  {K_CS, K_8, K_V, K_B, K_6},
  {K_0, K_SS, K_6, K_J, K_K}
};

constexpr uint8_t keybOnscrMatrix[2][20] PROGMEM = {
  {K_1, K_2, K_3, K_4, K_5, K_6, K_7, K_8, K_9, K_0, K_Q, K_W, K_E, K_R, K_T, K_Y, K_U, K_I, K_O, K_P},
  {K_A, K_S, K_D, K_F, K_G, K_H, K_J, K_K, K_L, K_ENTER, K_CS, K_Z, K_X, K_C, K_V, K_B, K_N, K_M, K_SS, K_SPACE},
};

constexpr uint8_t keybOnscr[2][21] PROGMEM = {
  "1234567890QWERTYUIOP",
  "ASDFGHJKLecZXCVBNMs_",
};


uint8_t key_matrix[41]; //41 is NULL


uint8_t control_type;

uint8_t control_pad_l;
uint8_t control_pad_r;
uint8_t control_pad_u;
uint8_t control_pad_d;
uint8_t control_pad_act;
uint8_t control_pad_esc;
uint8_t control_pad_lft;
uint8_t control_pad_rgt;


enum {
  CONTROL_PAD_KEYBOARD,
  CONTROL_PAD_KEMPSTON
};

volatile uint8_t sound_dac;



extern "C" {
  void mem_wr(uint16_t adr, uint8_t val);
  uint8_t mem_rd(uint16_t adr);
  void port_out(uint16_t port, uint8_t value);
  uint8_t port_in(uint16_t port);
};

void mem_wr(uint16_t addr, uint8_t value)
{
  if (addr >= 0x4000)
  {
    memory[addr - 0x4000] = value;

    if (addr < 0x5800)
    {
      addr -= 0x4000;
      uint16_t line = ((addr / 256) & 7) + ((addr / 32) & 7) * 8 + addr / 2048 * 64;
      line_change[line / 8] |= (1 << (line & 7));
    }
    else if (addr < 0x5b00)
    {
      line_change[(addr - 0x5800) / 32] = 255;
    }
  }
}



uint8_t mem_rd(uint16_t addr)
{
  if (addr < 0x4000)
  {
    return pgm_read_byte(&rom[addr]);
  }
  else
  {
    return memory[addr - 0x4000];
  }
}



uint8_t port_in(uint16_t port)
{
  uint8_t val;
  uint16_t off;

  val = 0xff;

  if (!(port & 0x01)) //port #fe
  {
    off = 0;

    if (!(port & 0x0100)) off = 5 * 0;
    if (!(port & 0x0200)) off = 5 * 1;
    if (!(port & 0x0400)) off = 5 * 2;
    if (!(port & 0x0800)) off = 5 * 3;
    if (!(port & 0x1000)) off = 5 * 4;
    if (!(port & 0x2000)) off = 5 * 5;
    if (!(port & 0x4000)) off = 5 * 6;
    if (!(port & 0x8000)) off = 5 * 7;

    if (key_matrix[off + 0]) val &= ~0x01;
    if (key_matrix[off + 1]) val &= ~0x02;
    if (key_matrix[off + 2]) val &= ~0x04;
    if (key_matrix[off + 3]) val &= ~0x08;
    if (key_matrix[off + 4]) val &= ~0x10;
  }
  else
  {
    if ((port & 0xff) == 0x1f) val = port_1f;
  }

  return val;
}



void port_out(uint16_t port, uint8_t value)
{
  if (!(port & 0x01))
  {
    if ((port_fe & 7) != (value & 7)) line_change[32] = 1; //update border

    port_fe = value;
  }
}



void zx_init()
{
  memset(memory, 0, sizeof(memory));
  memset((void *)key_matrix, 0, sizeof(key_matrix));
  memset(line_change, 0xff, sizeof(line_change));

  port_fe = 0;
  port_1f = 0;

  memset(&cpu, 0, sizeof(cpu));

  Z80Reset(&cpu);
}



void z80_unrle(uint8_t* mem, int sz)
{
  int ptr = 0;

  while (ptr < sz - 4)
  {
    if (mem[ptr] == 0xed && mem[ptr + 1] == 0xed)
    {
      int len = mem[ptr + 2];
      uint8_t val = mem[ptr + 3];

      memmove(&mem[ptr + len], &mem[ptr + 4], sz - (ptr + len));  //not memcpy, because of the overlapping

      for (int i = 0; i < len; ++i) mem[ptr++] = val;
    }
    else
    {
      ++ptr;
    }
  }
}






 
uint8_t zx_load_z80(const char* filename){
  int sz;
  uint32_t addr;
  uint8_t header[30];
  fs::File f;

  if(filename[0]){
    f = LittleFS.open(filename, "r");
    if (!f) return 0;
    sz = f.size();
    f.readBytes((char*)header, sizeof(header));
  }
  else {
    sz=sizeof(gameItself);
    memcpy_P(&header[0], gameItself, sizeof(header));
    }
  
  sz -= sizeof(header);

  Z80Reset(&cpu);

  cpu.registers.byte[Z80_A] = header[0];
  cpu.registers.byte[Z80_F] = header[1];
  cpu.registers.byte[Z80_C] = header[2];
  cpu.registers.byte[Z80_B] = header[3];
  cpu.registers.byte[Z80_L] = header[4];
  cpu.registers.byte[Z80_H] = header[5];
  cpu.pc = header[6] + header[7] * 256;
  cpu.registers.word[Z80_SP] = header[8] + header[9] * 256;

  cpu.i = header[10];
  cpu.r = header[11];

  if (header[12] == 255) header[12] = 1;

  uint8_t rle = header[12] & 0x20;
  port_fe = (header[12] >> 1) & 7;

  cpu.registers.byte[Z80_E] = header[13];
  cpu.registers.byte[Z80_D] = header[14];

  cpu.alternates[Z80_BC] = header[15] + header[16] * 256;
  cpu.alternates[Z80_DE] = header[17] + header[18] * 256;
  cpu.alternates[Z80_HL] = header[19] + header[20] * 256;
  cpu.alternates[Z80_AF] = header[22] + header[21] * 256; //???

  cpu.registers.byte[Z80_IXL] = header[23];
  cpu.registers.byte[Z80_IXH] = header[24];
  cpu.registers.byte[Z80_IYL] = header[25];
  cpu.registers.byte[Z80_IYH] = header[26];

  if (!(header[27] & 1)) //di
  {
    cpu.iff1 = 0;
  }
  else
  {
    cpu.iff1 = 1;
  }

  cpu.iff2 = header[28];
  cpu.im = (header[29] & 3);

  if (cpu.pc) //v1 format
    {
      if(filename[0])
        f.readBytes((char*)memory, sz);
      else 
        memcpy_P(memory, &gameItself[sizeof(header)], sz);
        
        if (rle) z80_unrle(memory, 16384 * 3);
    }
  else  //v2 or v3 format, features an extra header
  {
    //read actual PC from the extra header, skip rest of the extra header
    if(filename[0])
        f.readBytes((char*)header, 4);
      else 
        memcpy_P((unsigned char *)&header, &gameItself[sizeof(header)], 4);
    
    sz -= 4;

    int len = header[0] + header[1] * 256 + 2 - 4;
    cpu.pc = header[2] + header[3] * 256;


     if(filename[0])
        f.seek(len, fs::SeekCur);
      else
        addr = len+sizeof(header)+4;
    
    sz -= len;

    //unpack 16K pages

    while (sz > 0)
    {
      if(filename[0])
          f.readBytes((char*)header, 3);
        else{
          memcpy_P((unsigned char *)&header, &gameItself[addr], 3);
          addr += 3;
        }

      
      sz -= 3;

      len = header[0] + header[1] * 256;

      int ptr = 0;

      switch (header[2])
      {
        case 4: ptr = 0x8000; break;
        case 5: ptr = 0xc000; break;
        case 8: ptr = 0x4000; break;
      }

      if (ptr)
      {
        ptr -= 0x4000;

          if(filename[0])
            f.readBytes((char*)&memory[ptr], len);
          else{
            memcpy_P((unsigned char *)&memory[ptr], &gameItself[addr], len);
            addr+=len;}
        
        sz -= len;

        if (len < 0xffff) z80_unrle(&memory[ptr], 16384);
      }
      else
      {
        
          if(filename[0])
            f.seek(len, fs::SeekCur);
          else
            addr+=len;
        
        sz -= len;
      }
    }
  }

  if (filename[0] != 0) f.close();

  return 1;
}



uint8_t zx_load_scr(const char* filename){
 if  (filename[0]){
    fs::File f = LittleFS.open(filename, "r");
    if (!f) return 0;
    f.readBytes((char*)memory, 6912);
    f.close();
    memset(line_change, 0xff, sizeof(line_change));
    return 1;
  }
 else{
  if (gameScreenExist) {
    memcpy_P(memory, gameScreen, 6912);
    memset(line_change, 0xff, sizeof(line_change));
    return 1;
    }
  else return 0;
  }
};



#define RGB565Q(r,g,b)    ( ((((r)>>5)&0x1f)<<11) | ((((g)>>4)&0x3f)<<5) | (((b)>>5)&0x1f) )



void IRAM_ATTR zx_render_frame()
{
  static uint16_t i, j, ch, ln, px, row, aptr, optr, attr, pptr1, pptr2, bright;
  static uint_fast16_t ink, pap;
  static uint_fast16_t col = 0;
  static uint8_t line1, line2;
  
    static const uint_fast16_t palette[16] __attribute__ ((aligned(32))) = {
      RGB565Q(0, 0, 0),
      RGB565Q(0, 29, 200),
      RGB565Q(216, 36, 15),
      RGB565Q(213, 48, 201),
      RGB565Q(0, 199, 33),
      RGB565Q(0, 201, 203),
      RGB565Q(206, 202, 39),
      RGB565Q(203, 203, 203),
      RGB565Q(0, 0, 0),
      RGB565Q(0, 39, 251),
      RGB565Q(255, 48, 22),
      RGB565Q(255, 63, 252),
      RGB565Q(0, 249, 44),
      RGB565Q(0, 252, 254),
      RGB565Q(255, 253, 51),
      RGB565Q(255, 255, 255),
    };

  if (line_change[32])
  {
    line_change[32] = 0;

    col = palette[port_fe & 7] << 2;
    for (i = 0; i < 128; ++i) line_buffer[i] = col;
    for (i = 0; i < 16; ++i)
    {
      myESPboy.tft.pushImage(0, i, 128, 1, line_buffer);
      myESPboy.tft.pushImage(0, 112 + i, 128, 1, line_buffer);
    }
  }

  row = 16;
  myESPboy.tft.setAddrWindow(0, row, 128, 96);

  for (ln = 0; ln < 192; ln += 2)
  {
    if (!(line_change[ln / 8] & (3 << (ln & 7))))
    {
      ++row;
      myESPboy.tft.setAddrWindow(0, row, 128, 96);
      continue;
    }

    line_change[ln / 8] &= ~(3 << (ln & 7));

    pptr1 = (ln & 7) * 256 + ((ln / 8) & 7) * 32 + (ln / 64) * 2048;
    pptr2 = pptr1 + 256;
    aptr = 6144 + ln / 8 * 32;
    optr = 0;

    for (ch = 0; ch < 32; ++ch)
    {
      attr = memory[aptr++];
      bright = (attr & 0x40) ? 8 : 0;
      ink = palette[(attr & 7) + bright];
      pap = palette[((attr >> 3) & 7) + bright];

      line1 = memory[pptr1++];
      line2 = memory[pptr2++];

      for (px = 0; px < 8; px += 2)
      {
        col = (line1 & 0x80) ? ink : pap;
        col += (line1 & 0x40) ? ink : pap;
        col += (line2 & 0x80) ? ink : pap;
        col += (line2 & 0x40) ? ink : pap;

        line_buffer[optr++] = col;

        line1 <<= 2;
        line2 <<= 2;
      }
    }

    //myESPboy.tft.pushImage(0, row++, 128, 1, line_buffer);
    row++;
    myESPboy.tft.pushPixels(line_buffer, 128); 
  }
}


int IRAM_ATTR check_key()
{
  pad_state_prev = pad_state;
  pad_state = ~myESPboy.mcp.readGPIOAB() & 255;
  pad_state_t = pad_state ^ pad_state_prev & pad_state;
  return pad_state;
}



//0 no timeout, otherwise timeout in ms

void wait_any_key(int timeout)
{
  timeout /= 100;

  while (1)
  {
    check_key();

    if (pad_state_t&PAD_ANY) break;

    if (timeout)
    {
      --timeout;

      if (timeout <= 0) break;
    }

    delay(100);
  }
}


//render part of a 8-bit uncompressed BMP file
//no clipping
//uses line buffer to draw it much faster than through writePixel

void drawBMP8Part(int16_t x, int16_t y, const uint8_t bitmap[], int16_t dx, int16_t dy, int16_t w, int16_t h)
{
  uint32_t bw = pgm_read_dword(&bitmap[0x12]);
  uint32_t bh = pgm_read_dword(&bitmap[0x16]);
  uint32_t wa = (bw + 3) & ~3;

  if (w >= h)
  {
    for (uint16_t i = 0; i < h; ++i)
    {
      uint32_t off = 54 + 256 * 4 + (bh - 1 - (i + dy)) * wa + dx;

      for (uint16_t j = 0; j < w; ++j)
      {
        uint16_t col = pgm_read_byte(&bitmap[off++]);
        uint32_t rgb = pgm_read_dword(&bitmap[54 + col * 4]);
        uint16_t c16 = ((rgb & 0xf8) >> 3) | ((rgb & 0xfc00) >> 5) | ((rgb & 0xf80000) >> 8);
        line_buffer[j] = c16;
      }

      myESPboy.tft.pushImage(x, y + i, w, 1, line_buffer);
    }
  }
  else
  {
    for (uint16_t i = 0; i < w; ++i)
    {
      uint32_t off = 54 + 256 * 4 + (bh - 1 - dy) * wa + i + dx;

      for (uint16_t j = 0; j < h; ++j)
      {
        uint16_t col = pgm_read_byte(&bitmap[off]);
        uint32_t rgb = pgm_read_dword(&bitmap[54 + col * 4]);
        uint16_t c16 = ((rgb & 0xf8) >> 3) | ((rgb & 0xfc00) >> 5) | ((rgb & 0xf80000) >> 8);
        line_buffer[j] = c16;
        off -= wa;
      }

      myESPboy.tft.pushImage(x + i, y, 1, h, line_buffer);
    }
  }
}



void drawCharFast(uint16_t x, uint16_t y, uint8_t c, uint16_t color, uint16_t bg)
{
  for (uint16_t i = 0; i < 5; ++i)
  {
    uint16_t line = pgm_read_byte(&font[c * 5 + i]);

    for (uint16_t j = 0; j < 8; ++j)
    {
      uint16_t c16 = (line & 1) ? color : bg;
      line_buffer[j * 5 + i] = c16;
      line >>= 1;
    }
  }

  myESPboy.tft.pushImage(x, y, 5, 8, line_buffer);
}



void printFast(int x, int y, char* str, int16_t color)
{
  while (1)
  {
    char c = *str++;

    if (!c) break;

    drawCharFast(x, y, c, color, 0);
    x += 6;
  }
}



#define CONTROL_TYPES   5

const char* const layout_name[] = {
  "KEMP",
  "QAOP",
  "ZXse",
  "SINC",
  "CURS"
};

const int8_t layout_scheme[] = {
  -1, 0, 0, 0, 0, 0, 0, 0,
  K_O, K_P, K_Q, K_A, K_SPACE, K_M, K_0, K_1,
  K_Z, K_X, K_Q, K_A, K_SPACE, K_ENTER, K_0, K_1,
  K_6, K_7, K_9, K_8, K_0, K_ENTER, K_SPACE, K_1,
  K_5, K_8, K_8, K_7, K_0, K_ENTER, K_SPACE, K_1
};



#define FILE_HEIGHT    14
#define FILE_FILTER   "z80"

int16_t file_cursor;

uint8_t file_browser_ext(const char* name)
{
  while (1) if (*name++ == '.') break;

  return (strcasecmp(name, FILE_FILTER) == 0) ? 1 : 0;
}



void file_browser(const char* path, const __FlashStringHelper* header, char* fname, uint16_t fname_len)
{
  int16_t i, j, sy, pos, off, frame, file_count, control_type;
  uint8_t change, filter;
  fs::Dir dir;
  fs::File entry;
  char name[19 + 1];
  const char* str;

  memset(fname, 0, fname_len);
  memset(name, 0, sizeof(name));

  myESPboy.tft.fillScreen(TFT_BLACK);

  dir = LittleFS.openDir(path);

  file_count = 0;
  control_type = 0;

  while (dir.next())
  {
    entry = dir.openFile("r");

    filter = file_browser_ext(entry.name());

    entry.close();

    if (filter) ++file_count;
  }

  if (!file_count)
  {
    filename[0] = 0;
    //printFast(24, 60, (char*)"No files found", TFT_RED);
    //while (1) delay(1000);
  }
  else
  {
  printFast(4, 4, (char*)header, TFT_GREEN);
  myESPboy.tft.fillRect(0, 12, 128, 1, TFT_WHITE);

  change = 1;
  frame = 0;

  while (1)
  {
    if (change)
    {
      printFast(100, 4, (char*)layout_name[control_type], TFT_WHITE);

      pos = file_cursor - FILE_HEIGHT / 2;

      if (pos > file_count - FILE_HEIGHT) pos = file_count - FILE_HEIGHT;
      if (pos < 0) pos = 0;

      dir = LittleFS.openDir(path);
      i = pos;
      while (dir.next())
      {
        entry = dir.openFile("r");

        filter = file_browser_ext(entry.name());

        entry.close();

        if (!filter) continue;

        --i;
        if (i < 0) break;
      }

      sy = 14;
      i = 0;

      while (1)
      {
        entry = dir.openFile("r");

        filter = file_browser_ext(entry.name());

        if (filter)
        {
          str = entry.name();

          for (j = 0; j < sizeof(name) - 1; ++j)
          {
            if (*str != 0 && *str != '.') name[j] = *str++; else name[j] = ' ';
          }

          printFast(8, sy, name, TFT_WHITE);

          drawCharFast(2, sy, ' ', TFT_WHITE, TFT_BLACK);

          if (pos == file_cursor)
          {
            strncpy(fname, entry.name(), fname_len);

            if (!(frame & 128)) drawCharFast(2, sy, 0xda, TFT_WHITE, TFT_BLACK);
          }
        }

        entry.close();

        if (!dir.next()) break;

        if (filter)
        {
          sy += 8;
          ++pos;
          ++i;
          if (i >= FILE_HEIGHT) break;
        }
      }

      change = 0;
    }

    //check_key();

    if (myESPboy.getKeys() & PAD_UP)
    {
      --file_cursor;

      if (file_cursor < 0) file_cursor = file_count - 1;

      change = 1;
      frame = 0;
      delay(100); 
    }

    if (myESPboy.getKeys() & PAD_DOWN)
    {
      ++file_cursor;

      if (file_cursor >= file_count) file_cursor = 0;

      change = 1;
      frame = 0;
      delay(100); 
    }

    if (myESPboy.getKeys() & PAD_ACT)
    {
      ++control_type;
      if (control_type >= CONTROL_TYPES) control_type = 0;
      change = 1;
      delay(200); 
    }

    if (myESPboy.getKeys() & PAD_ESC) {
      delay(100); 
      break;
    }

    if ((myESPboy.getKeys() & PAD_LFT) || (myESPboy.getKeys() & PAD_RGT)) {
      fname[0] = 0;
      delay(500);
      break;
    }

    delay(1);
    ++frame;

    if (!(frame & 127)) change = 1;
  }

  off = control_type * 8;

  if (layout_scheme[off + 0] >= 0)
  {
    control_type = CONTROL_PAD_KEYBOARD;
    control_pad_l = layout_scheme[off + 0];
    control_pad_r = layout_scheme[off + 1];
    control_pad_u = layout_scheme[off + 2];
    control_pad_d = layout_scheme[off + 3];
    control_pad_act = layout_scheme[off + 4];
    control_pad_esc = layout_scheme[off + 5];
    control_pad_lft = layout_scheme[off + 6];
    control_pad_rgt = layout_scheme[off + 7];
  }
  else
  {
    control_type = CONTROL_PAD_KEMPSTON;
  }
  }
  myESPboy.tft.fillScreen(TFT_BLACK);
}



void IRAM_ATTR timer_ISR()
{
  sigmaDeltaWrite(0, sound_dac);

  sound_dac = 0;

  //emulate some ticks that is roughly enough for just one sound sample

  int ticks = 0;

  if (frame_int)
  {
    frame_int = false;
    ticks = Z80Interrupt(&cpu, 0xff);
  }

  int sacc = 0;
  int scnt = 0;

  while (ticks < (ZX_CLOCK_FREQ / SAMPLE_RATE))
  {
    int n = Z80Emulate(&cpu, 4);

    if (port_fe & 0x10) sacc += n;
    scnt += n;

    ticks += n;
  }

  sound_dac = sacc * 100 / scnt;

  frame_ticks += ticks;

  if (frame_ticks >= (ZX_CLOCK_FREQ / ZX_FRAME_RATE))
  {
    frame_done = true;
    frame_int = true;
    frame_ticks -= (ZX_CLOCK_FREQ / ZX_FRAME_RATE);
  }
}



void setup() {

  //Serial.begin(115200);

  //Init ESPboy

  myESPboy.begin(((String)F("ZX48 LinKeFong core")).c_str());

  pad_state = 0;
  pad_state_prev = 0;
  pad_state_t = 0;

  //keybModule init
  Wire.begin();
  Wire.beginTransmission(0x27); //check for MCP23017Keyboard at address 0x27
  if (!Wire.endTransmission()) {
    keybModuleExist = 1;
    mcpKeyboard.begin(7);
    for (uint8_t i = 0; i < 7; i++) {
      mcpKeyboard.pinMode(i, OUTPUT);
      mcpKeyboard.digitalWrite(i, HIGH);
    }
    for (uint8_t i = 0; i < 5; i++) {
      mcpKeyboard.pinMode(i + 8, INPUT);
      mcpKeyboard.pullUp(i + 8, HIGH);
    }
    mcpKeyboard.pinMode(7, OUTPUT);
    mcpKeyboard.digitalWrite(7, HIGH); //backlit on
  }
  else keybModuleExist = 0;

  //filesystem init
  LittleFS.begin();

  //Serial.println(ESP.getFreeHeap());
}



void change_ext(char* fname, const char* ext)
{
  while (1)
  {
    if (!*fname) break;
    if (*fname++ == '.')
    {
      fname[0] = ext[0];
      fname[1] = ext[1];
      fname[2] = ext[2];
      break;
    }
  }
}



uint8_t zx_layout_code(char c)
{
  if (c >= 'a' && c <= 'z') c -= 32;
  switch (c)
  {
    case 'A': return K_A;
    case 'B': return K_B;
    case 'C': return K_C;
    case 'D': return K_D;
    case 'E': return K_E;
    case 'F': return K_F;
    case 'G': return K_G;
    case 'H': return K_H;
    case 'I': return K_I;
    case 'J': return K_J;
    case 'K': return K_K;
    case 'L': return K_L;
    case 'M': return K_M;
    case 'N': return K_N;
    case 'O': return K_O;
    case 'P': return K_P;
    case 'Q': return K_Q;
    case 'R': return K_R;
    case 'S': return K_S;
    case 'T': return K_T;
    case 'U': return K_U;
    case 'V': return K_V;
    case 'W': return K_W;
    case 'X': return K_X;
    case 'Y': return K_Y;
    case 'Z': return K_Z;
    case '0': return K_0;
    case '1': return K_1;
    case '2': return K_2;
    case '3': return K_3;
    case '4': return K_4;
    case '5': return K_5;
    case '6': return K_6;
    case '7': return K_7;
    case '8': return K_8;
    case '9': return K_9;
    case '_': return K_SPACE;
    case '$': return K_ENTER;
    case '@': return K_CS;
    case '#': return K_SS;
  }

  return 0;
}




void zx_load_layout(char* filename)
{
  char cfg[8];

  if(filename[0] != 0) {
    fs::File f = LittleFS.open(filename, "r");
    if (!f) return;
    f.readBytes(cfg, 8);
    f.close();
  }
  else{
    memcpy_P(&cfg[0], gameControl, 8);
  }

  control_type = CONTROL_PAD_KEYBOARD;
  control_pad_u = zx_layout_code(cfg[0]);
  control_pad_d = zx_layout_code(cfg[1]);
  control_pad_l = zx_layout_code(cfg[2]);
  control_pad_r = zx_layout_code(cfg[3]);
  control_pad_act = zx_layout_code(cfg[4]);
  control_pad_esc = zx_layout_code(cfg[5]);
  control_pad_lft = zx_layout_code(cfg[6]);
  control_pad_rgt = zx_layout_code(cfg[7]);
}


void keybModule() {
  static uint8_t keysReaded[7];
  static uint8_t row, col;
  static uint8_t keykeyboardpressed;
  static uint8_t symkeyboardpressed;
  symkeyboardpressed = 0;
  for (row = 0; row < 7; row++) {
    mcpKeyboard.digitalWrite(row, LOW);
    keysReaded [row] = ((mcpKeyboard.readGPIOAB() >> 8) & 31);
    mcpKeyboard.digitalWrite(row, HIGH);
  }
  if (!(keysReaded[2] & 1)) symkeyboardpressed = 1; // if "sym" key is pressed
  for (row = 0; row < 7; row++)
    for (col = 0; col < 5; col++)
      if (!((keysReaded[row] >> col) & 1))
      {
        if (!symkeyboardpressed) keykeyboardpressed = pgm_read_byte(&keybCurrent[row][col]);
        else keykeyboardpressed = pgm_read_byte(&keybCurrent2[row][col]);
        if (keykeyboardpressed < 40) key_matrix[keykeyboardpressed] |= 1;
        else {
          if (keykeyboardpressed == K_DEL) {
            key_matrix[K_0] |= 1;
            key_matrix[K_CS] |= 1;
          }
          if (keykeyboardpressed == K_LED) {
            mcpKeyboard.digitalWrite(7, !mcpKeyboard.digitalRead(7));
            delay(100);
          }
        }
      }
}


void redrawOnscreen(uint8_t slX, uint8_t slY, uint8_t shf) {
  myESPboy.tft.fillRect(0, 128 - 16, 128, 16, TFT_BLACK);
  for (uint8_t i = 0; i < 20; i++) drawCharFast(i * 6 + 4, 128 - 16, pgm_read_byte(&keybOnscr[0][i]), TFT_YELLOW, TFT_BLACK);
  for (uint8_t i = 0; i < 20; i++) drawCharFast(i * 6 + 4, 128 - 8, pgm_read_byte(&keybOnscr[1][i]), TFT_YELLOW, TFT_BLACK);
  drawCharFast(slX * 6 + 4, 128 - 16 + slY * 8, pgm_read_byte(&keybOnscr[slY][slX]), TFT_RED, TFT_BLACK);
  if (shf & 1) drawCharFast(10 * 6 + 4, 128 - 16 + 8, pgm_read_byte(&keybOnscr[1][10]), TFT_RED, TFT_BLACK);
  if (shf & 2) drawCharFast(18 * 6 + 4, 128 - 16 + 8, pgm_read_byte(&keybOnscr[1][18]), TFT_RED, TFT_BLACK);
}


void keybOnscreen() {
  uint8_t selX = 0, selY = 0, shifts = 0;
  redrawOnscreen(selX, selY, shifts);
  while (1) {
    check_key();
    delay(100);
    if ((pad_state & PAD_RIGHT) && selX < 19) selX++;
    if ((pad_state & PAD_LEFT) && selX > 0) selX--;
    if ((pad_state & PAD_DOWN) && selY < 1) selY++;
    if ((pad_state & PAD_UP) && selY > 0) selY--;
    if (((pad_state & PAD_ACT) || (pad_state & PAD_ESC)) && !(selX == 10 && selY == 1) && !(selX == 18 && selY == 1)) break;
    if ((pad_state & PAD_ACT) && (selX == 10) && (selY == 1)) {
      shifts |= (shifts ^ 1);
      delay (300);
    }
    if ((pad_state & PAD_ACT) && (selX == 18) && (selY == 1)) {
      shifts |= (shifts ^ 2);
      delay (300);
    }
    if ((pad_state & PAD_ACT) && (selX == 10) && (selY == 1) && (shifts & 2)) break;
    if ((pad_state & PAD_ACT) && (selX == 18) && (selY == 1) && (shifts & 1)) break;
    if (pad_state) redrawOnscreen(selX, selY, shifts);
  }

  if (pad_state & PAD_ACT) key_matrix[pgm_read_byte(&keybOnscrMatrix[selY][selX])] |= 1;
  if (pad_state & PAD_ACT && (shifts & 1)) key_matrix[K_CS] |= 1;
  if (pad_state & PAD_ACT && (shifts & 2)) key_matrix[K_SS] |= 1;
  delay(300);
  check_key();
  myESPboy.tft.fillRect(0, 128 - 16, 128, 16, TFT_BLACK);
  memset(line_change, 0xff, sizeof(line_change));
}



void loop()
{

  file_cursor = 0;

  control_type = CONTROL_PAD_KEYBOARD;
  control_pad_l = K_Z;
  control_pad_r = K_X;
  control_pad_u = K_Q;
  control_pad_d = K_A;
  control_pad_act = K_SPACE;
  control_pad_esc = K_ENTER;
  control_pad_lft = K_NULL;
  control_pad_rgt = K_NULL;

  file_browser("/", F("Load .Z80:"), filename, sizeof(filename));

  zx_init();

  //if (strlen(snapname) != 0)
  //{
    change_ext(filename, "cfg");
    zx_load_layout(filename);

    change_ext(filename, "scr");
    if (zx_load_scr(filename))
    {
      zx_render_frame();
      wait_any_key(3 * 1000);
    }

    change_ext(filename, "z80");
    zx_load_z80(filename);
  //}

  LittleFS.end();

  memset(line_change, 0xff, sizeof(line_change));

  noInterrupts();

  sound_dac = 0;

  sigmaDeltaSetup(0, F_CPU / 256);
  sigmaDeltaAttachPin(SOUNDPIN);
  sigmaDeltaEnable();

  timer1_attachInterrupt(timer_ISR);
  timer1_enable(TIM_DIV1, TIM_EDGE, TIM_LOOP);
  timer1_write(80000000 / SAMPLE_RATE);

  frame_int = true;
  frame_done = false;
  frame_ticks = 0;

  interrupts();

  //main loop

  while (1)
  {
   delay(0);

    noInterrupts();
    memset((void *)key_matrix, 0, sizeof(key_matrix));
    
    check_key();
    
    //check onscreen keyboard
    if ((pad_state & PAD_LFT) && (pad_state & PAD_RGT)) {interrupts(); keybOnscreen(); noInterrupts();}

    //check keyboard module
    if (keybModuleExist) {interrupts(); keybModule(); noInterrupts();}

    switch (control_type)
    {
      case CONTROL_PAD_KEYBOARD:
        key_matrix[control_pad_l] |= (pad_state & PAD_LEFT) ? 1 : 0;
        key_matrix[control_pad_r] |= (pad_state & PAD_RIGHT) ? 1 : 0;
        key_matrix[control_pad_u] |= (pad_state & PAD_UP) ? 1 : 0;
        key_matrix[control_pad_d] |= (pad_state & PAD_DOWN) ? 1 : 0;
        key_matrix[control_pad_act] |= (pad_state & PAD_ACT) ? 1 : 0;
        key_matrix[control_pad_esc] |= (pad_state & PAD_ESC) ? 1 : 0;
        key_matrix[control_pad_lft] |= (pad_state & PAD_LFT) ? 1 : 0;
        key_matrix[control_pad_rgt] |= (pad_state & PAD_RGT) ? 1 : 0;
        break;

      case CONTROL_PAD_KEMPSTON:
        port_1f = 0;
        if (pad_state & PAD_LEFT) port_1f |= 0x02;
        if (pad_state & PAD_RIGHT) port_1f |= 0x01;
        if (pad_state & PAD_UP) port_1f |= 0x08;
        if (pad_state & PAD_DOWN) port_1f |= 0x04;
        if (pad_state & PAD_ACT) port_1f |= 0x10;
        key_matrix[K_SPACE] = (pad_state & PAD_ESC) ? 1 : 0;
        key_matrix[K_0] = (pad_state & PAD_LFT) ? 1 : 0;
        key_matrix[K_1] = (pad_state & PAD_RGT) ? 1 : 0;
        break;
    }

    interrupts();
    while (!frame_done) delay(0);

    frame_done = false;

    zx_render_frame();
  }
}
