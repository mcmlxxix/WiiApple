/*
AppleWin : An Apple //e emulator for Windows

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski, Nick Westgate

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Emulation of video modes
 *
 * Author: Various
 */

/* Adaptation for SDL and POSIX (l) by beom beotiger, Nov-Dec 2007 */

#include "stdafx.h"
#include "wwrapper.h"
//#include "stretch.c"	// for SDL_SoftStretch thanx to Sam Lantinga and Tomasz Cejner

//#include "..\resource\resource.h"

/* reference: technote tn-iigs-063 "Master Color Values"

          Color  Color Register LR HR  DHR Master Color R,G,B
          Name       Value      #  #   #      Value
          ----------------------------------------------------
          Black       0         0  0,4 0      $0000    (0,0,0) -> (00,00,00) Windows
(Magenta) Deep Red    1         1      1      $0D03    (D,0,3) -> (D0,00,30) Custom
          Dark Blue   2         2      8      $0009    (0,0,9) -> (00,00,80) Windows
 (Violet) Purple      3         3  2   9      $0D2D    (D,2,D) -> (FF,00,FF) Windows
          Dark Green  4         4      4      $0072    (0,7,2) -> (00,80,00) Windows
 (Gray 1) Dark Gray   5         5      5      $0555    (5,5,5) -> (80,80,80) Windows
   (Blue) Medium Blue 6         6  6   C      $022F    (2,2,F) -> (00,00,FF) Windows
   (Cyan) Light Blue  7         7      D      $06AF    (6,A,F) -> (60,A0,FF) Custom
          Brown       8         8      2      $0850    (8,5,0) -> (80,50,00) Custom
          Orange      9         9  5   3      $0F60    (F,6,0) -> (FF,80,00) Custom (modified to match better with the other Hi-Res Colors)
 (Gray 2) Light Gray  A         A      A      $0AAA    (A,A,A) -> (C0,C0,C0) Windows
          Pink        B         B      B      $0F98    (F,9,8) -> (FF,90,80) Custom
  (Green) Light Green C         C  1   6      $01D0    (1,D,0) -> (00,FF,00) Windows
          Yellow      D         D      7      $0FF0    (F,F,0) -> (FF,FF,00) Windows
   (Aqua) Aquamarine  E         E      E      $04F9    (4,F,9) -> (40,FF,90) Custom
          White       F         F  3,7 F      $0FFF    (F,F,F) -> (FF,FF,FF) Windows

   LR: Lo-Res   HR: Hi-Res   DHR: Double Hi-Res */

#define RGB(r,g,b)          ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))
//#define RGB(r,g,b) SDL_MapRGB(g_hSourceBitmap->format, r, g, b)
#define GetRValue(rgb)      ((BYTE)(rgb))
#define GetGValue(rgb)      ((BYTE)(((WORD)(rgb)) >> 8))
#define GetBValue(rgb)      ((BYTE)((rgb)>>16))


#define FLASH_80_COL 1

#define HALF_SHIFT_DITHER 0


// STANDARD /*WINDOWS*/ LINUX COLORS
#define  CREAM            0xF6
#define  MEDIUM_GRAY      0xF7
#define  DARK_GRAY        0xF8
#define  RED              0xF9
#define  GREEN            0xFA
#define  YELLOW           0xFB
#define  BLUE             0xFC
#define  MAGENTA          0xFD
#define  CYAN             0xFE
#define  WHITE            0xFF


enum Color_Palette_Index_e
{
	// Really need to have Quater Green and Quarter Blue for Hi-Res
	  BLACK
	, DARK_RED
	, DARK_GREEN       // Half Green
	, DARK_YELLOW
	, DARK_BLUE        // Half Blue
	, DARK_MAGENTA
	, DARK_CYAN
	, LIGHT_GRAY
	, MONEY_GREEN
	, SKY_BLUE

// OUR CUSTOM COLORS
	, DEEP_RED
	, LIGHT_BLUE
	, BROWN
	, ORANGE
	, PINK
	, AQUA

// CUSTOM HGR COLORS (don't change order) - For tv emulation g_nAppMode
	, HGR_BLACK
	, HGR_WHITE
	, HGR_BLUE
	, HGR_RED
	, HGR_GREEN
	, HGR_MAGENTA
	, HGR_GREY1
	, HGR_GREY2
	, HGR_YELLOW
	, HGR_AQUA
	, HGR_PURPLE
	, HGR_PINK

// USER CUSTOMIZABLE COLOR
	, MONOCHROME_CUSTOM

// Pre-set "Monochromes"
	, MONOCHROME_AMBER
	, MONOCHROME_GREEN
	, MONOCHROME_WHITE

	, NUM_COLOR_PALETTE
};

const int SRCOFFS_40COL   = 0;
const int SRCOFFS_IIPLUS  = (SRCOFFS_40COL  +  256);
const int SRCOFFS_80COL   = (SRCOFFS_IIPLUS +  256);
const int SRCOFFS_LORES   = (SRCOFFS_80COL  +  128);
const int SRCOFFS_HIRES   = (SRCOFFS_LORES  +   16);
const int SRCOFFS_DHIRES  = (SRCOFFS_HIRES  +  512);
const int SRCOFFS_TOTAL   = (SRCOFFS_DHIRES + 2560);

enum VideoFlag_e
{
	VF_80COL  = 0x00000001,
	VF_DHIRES = 0x00000002,
	VF_HIRES  = 0x00000004,
	VF_MASK2  = 0x00000008,
	VF_MIXED  = 0x00000010,
	VF_PAGE2  = 0x00000020,
	VF_TEXT   = 0x00000040
};

#define  SW_80COL         (vidmode & VF_80COL)
#define  SW_DHIRES        (vidmode & VF_DHIRES)
#define  SW_HIRES         (vidmode & VF_HIRES)
#define  SW_MASK2         (vidmode & VF_MASK2)
#define  SW_MIXED         (vidmode & VF_MIXED)
#define  SW_PAGE2         (vidmode & VF_PAGE2)
#define  SW_TEXT          (vidmode & VF_TEXT)

#define  SETSOURCEPIXEL(x,y,c)  g_aSourceStartofLine[(y)][(x)] = (c)

#define  SETFRAMECOLOR(i,r1,g1,b1)  framebufferinfo[i].r = r1; \
                                 framebufferinfo[i].g = g1; \
                                 framebufferinfo[i].b  = b1;

#define  HGR_MATRIX_YOFFSET 2	// For tv emulation g_nAppMode

// video scanner constants
int const kHBurstClock      =    53; // clock when Color Burst starts
int const kHBurstClocks     =     4; // clocks per Color Burst duration
int const kHClock0State     =  0x18; // H[543210] = 011000
int const kHClocks          =    65; // clocks per horizontal scan (including HBL)
int const kHPEClock         =    40; // clock when HPE (horizontal preset enable) goes low
int const kHPresetClock     =    41; // clock when H state presets
int const kHSyncClock       =    49; // clock when HSync starts
int const kHSyncClocks      =     4; // clocks per HSync duration
int const kNTSCScanLines    =   262; // total scan lines including VBL (NTSC)
int const kNTSCVSyncLine    =   224; // line when VSync starts (NTSC)
int const kPALScanLines     =   312; // total scan lines including VBL (PAL)
int const kPALVSyncLine     =   264; // line when VSync starts (PAL)
int const kVLine0State      = 0x100; // V[543210CBA] = 100000000
int const kVPresetLine      =   256; // line when V state presets
int const kVSyncLines       =     4; // lines per VSync duration

typedef bool (*UpdateFunc_t)(int,int,int,int,int);

static BYTE          celldirty[40][32];
static COLORREF      customcolors[NUM_COLOR_PALETTE];	// MONOCHROME is last custom color

	SDL_Surface  *g_hDeviceBitmap;
//static HBITMAP       g_hDeviceBitmap;
//static HDC           g_hDeviceDC;
static LPBYTE        framebufferbits;
       SDL_Color  framebufferinfo[256];

const int MAX_FRAME_Y = 384; // 192 scan lines * 2x zoom = 384
static LPBYTE        frameoffsettable[384];
static LPBYTE        g_pHiresBank1;
static LPBYTE        g_pHiresBank0;
       SDL_Surface  *g_hLogoBitmap = NULL;
       SDL_Surface  *charset40 = NULL;		// Apple charset40 bitmap

//static HPALETTE      g_hPalette;

	SDL_Surface  *g_hSourceBitmap = NULL;
//static HBITMAP       g_hSourceBitmap;
static LPBYTE        g_pSourcePixels;
       SDL_Color     g_pSourceHeader[256];
const int MAX_SOURCE_Y = 512;
static LPBYTE        g_aSourceStartofLine[ MAX_SOURCE_Y ];
static LPBYTE        g_pTextBank1; // Aux
static LPBYTE        g_pTextBank0; // Main

// For tv emulation g_nAppMode
// 2 extra scan lines on bottom?
static BYTE          hgrpixelmatrix[280][192 + 2 * HGR_MATRIX_YOFFSET];
static BYTE          colormixbuffer[6];
static WORD          colormixmap[6][6][6];
//

static int       g_nAltCharSetOffset         = 0; // alternate character set
static BOOL      displaypage2     = 0;
static LPBYTE    framebufferaddr  = (LPBYTE)0;
static LONG      framebufferpitch = 0;
BOOL      graphicsmode     = 0;
static BOOL      hasrefreshed     = 0;
static DWORD     lastpageflip     = 0;
COLORREF  monochrome       = RGB(0xC0,0xC0,0xC0);
//static BOOL      rebuiltsource    = 0;	--?????? not used?
static BOOL      redrawfull       = 1;
static DWORD     dwVBlCounter     = 0;
static LPBYTE    vidlastmem       = NULL;
static DWORD     vidmode          = VF_TEXT;
DWORD     videotype        = VT_COLOR_STANDARD;

static bool g_bTextFlashState = false;
static bool g_bTextFlashFlag = false;

static bool bVideoScannerNTSC = true;  // NTSC video scanning (or PAL)

//-------------------------------------

// Video consts:
const UINT nVBlStop_NTSC	= 21;
const UINT nVBlStop_PAL		= 29;

//-------------------------------------

void DrawDHiResSource ();
void DrawHiResSource ();
void DrawHiResSourceHalfShiftFull ();
void DrawHiResSourceHalfShiftDim ();
void DrawLoResSource ();
void DrawMonoDHiResSource ();
void DrawMonoHiResSource ();
void DrawMonoLoResSource ();
void DrawMonoTextSource (SDL_Surface * dc); // yes, we have just SDL_Surface either for DeviceContext, or bitmap
void DrawTextSource (SDL_Surface * dc);

//===========================================================================
void /*__stdcall */CopySource (int destx, int desty,
                           int xsize, int ysize,
                           int sourcex, int sourcey)
{
  LPBYTE currdestptr   = frameoffsettable [desty]  + destx;
  LPBYTE currsourceptr = g_aSourceStartofLine[sourcey] + sourcex;
  int bytesleft;
  while (ysize--)
  {
    bytesleft = xsize;
    while (bytesleft & 3)
	{
      --bytesleft;
      *(currdestptr+bytesleft) = *(currsourceptr+bytesleft);
    }
    while (bytesleft)
	{
      bytesleft -= 4;
      *(LPDWORD)(currdestptr+bytesleft) = *(LPDWORD)(currsourceptr+bytesleft);
    }
    currdestptr   += framebufferpitch; // we are going top to bottom, as all normal people do! ^_^ (bb)
    currsourceptr += SRCOFFS_TOTAL;
  }
}

//===========================================================================
void CreateFrameOffsetTable (LPBYTE addr, LONG pitch) {
// as I could take it's just needed for windzooeee DD while in FullScreen mode.
// Left for compatiblity purposes. -- bb.
if (framebufferaddr  == addr &&
      framebufferpitch == pitch)
      return;
  framebufferaddr  = addr;
  framebufferpitch = pitch;

  // CREATE THE OFFSET TABLE FOR EACH SCAN LINE IN THE FRAME BUFFER
  for (int loop = 0; loop < 384; loop++)
    frameoffsettable[loop] = framebufferaddr + framebufferpitch * loop; //(383-loop);
}

//===========================================================================
void CreateIdentityPalette () {
//  if (g_hPalette)
//  DeleteObject(g_hPalette);

	ZeroMemory(framebufferinfo, 256 * sizeof(SDL_Color));// must be cleared???
	// SET FRAME BUFFER TABLE ENTRIES TO CUSTOM COLORS
	SETFRAMECOLOR(DEEP_RED,  0xD0,0x00,0x30);
	SETFRAMECOLOR(LIGHT_BLUE,0x60,0xA0,0xFF);
	SETFRAMECOLOR(BROWN,     0x80,0x50,0x00);
	SETFRAMECOLOR(ORANGE,    0xFF,0x80,0x00);
	SETFRAMECOLOR(PINK,      0xFF,0x90,0x80);
	SETFRAMECOLOR(AQUA,      0x40,0xFF,0x90);

	SETFRAMECOLOR(HGR_BLACK,  0x00,0x00,0x00);	// For tv emulation g_nAppMode
	SETFRAMECOLOR(HGR_WHITE,  0xFF,0xFF,0xFE);
	SETFRAMECOLOR(HGR_BLUE,   0x00,0x80,0xFF);
	SETFRAMECOLOR(HGR_RED,    0xF0,0x50,0x00);
	SETFRAMECOLOR(HGR_GREEN,  0x20,0xC0,0x00);
	SETFRAMECOLOR(HGR_MAGENTA,0xA0,0x00,0xFF);
	SETFRAMECOLOR(HGR_GREY1,  0x80,0x80,0x80);
	SETFRAMECOLOR(HGR_GREY2,  0x80,0x80,0x80);
	SETFRAMECOLOR(HGR_YELLOW, 0xD0,0xB0,0x10);
	SETFRAMECOLOR(HGR_AQUA,   0x20,0xB0,0xB0);
	SETFRAMECOLOR(HGR_PURPLE, 0x60,0x50,0xE0);
	SETFRAMECOLOR(HGR_PINK,   0xD0,0x40,0xA0);

	SETFRAMECOLOR( MONOCHROME_CUSTOM
		, GetBValue(monochrome)
		, GetGValue(monochrome)
		, GetRValue(monochrome) );	// chngrd B<->R, why? By me. --bb ^_^

	SETFRAMECOLOR( MONOCHROME_AMBER, 0xFF,0x80,0x00);
	SETFRAMECOLOR( MONOCHROME_GREEN, 0x00,0xC0,0x00);
	SETFRAMECOLOR( MONOCHROME_WHITE, 0xFF,0xFF,0xFF);
	SETFRAMECOLOR(BLACK,       0x00,0x00,0x00);
	SETFRAMECOLOR(DARK_RED,    0x80,0x00,0x00);
	SETFRAMECOLOR(DARK_GREEN,  0x00,0x80,0x00);
	SETFRAMECOLOR(DARK_YELLOW, 0x80,0x80,0x00);
	SETFRAMECOLOR(DARK_BLUE,   0x00,0x00,0x80);
	SETFRAMECOLOR(DARK_MAGENTA,0x80,0x00,0x80);
	SETFRAMECOLOR(DARK_CYAN,   0x00,0x80,0x80);
	SETFRAMECOLOR(LIGHT_GRAY,  0xC0,0xC0,0xC0);
	SETFRAMECOLOR(MONEY_GREEN, 0xC0,0xDC,0xC0);
	SETFRAMECOLOR(SKY_BLUE,    0xA6,0xCA,0xF0);
	SETFRAMECOLOR(CREAM,       0xFF,0xFB,0xF0);
	SETFRAMECOLOR(MEDIUM_GRAY, 0xA0,0xA0,0xA4);
	SETFRAMECOLOR(DARK_GRAY,   0x80,0x80,0x80);
	SETFRAMECOLOR(RED,         0xFF,0x00,0x00);
	SETFRAMECOLOR(GREEN,       0x00,0xFF,0x00);
	SETFRAMECOLOR(YELLOW,      0xFF,0xFF,0x00);
	SETFRAMECOLOR(BLUE,        0x00,0x00,0xFF);
	SETFRAMECOLOR(MAGENTA,     0xFF,0x00,0xFF);
	SETFRAMECOLOR(CYAN,        0x00,0xFF,0xFF);
	SETFRAMECOLOR(WHITE,       0xFF,0xFF,0xFF);

//	g_hPalette = (HPALETTE)0;
//	ReleaseDC(window,dc);
}

//===========================================================================
void CreateDIBSections () {

  CopyMemory(g_pSourceHeader,framebufferinfo, 256 * sizeof(SDL_Color));

  // CREATE THE DEVICE CONTEXT
//  HWND window  = GetDesktopWindow();
//  HDC dc       = GetDC(window);
//  if (g_hDeviceDC)
//    DeleteDC(g_hDeviceDC);
//  g_hDeviceDC = CreateCompatibleDC(dc);

  // CREATE THE FRAME BUFFER DIB SECTION
  if (g_hDeviceBitmap)
    SDL_FreeSurface(g_hDeviceBitmap);
  g_hDeviceBitmap = SDL_CreateRGBSurface(SDL_SWSURFACE, 560, 384, 8, 0, 0, 0, 0);

  if(g_hDeviceBitmap == NULL) fprintf(stderr,"g_hDeviceBitmap was not created!\n");
		  //CreateDIBSection(dc,framebufferinfo,DIB_RGB_COLORS,
                  //                (LPVOID *)&framebufferbits,0,0);

  framebufferbits = (LPBYTE)g_hDeviceBitmap->pixels;
  SDL_SetColors(g_hDeviceBitmap, g_pSourceHeader, 0, 256);
  //SelectObject(g_hDeviceDC,g_hDeviceBitmap);

  // CREATE THE SOURCE IMAGE DIB SECTION
//  HDC sourcedc = CreateCompatibleDC(dc);
//  ReleaseDC(window,dc);
  if (g_hSourceBitmap)
    SDL_FreeSurface(g_hSourceBitmap);
  g_hSourceBitmap = SDL_CreateRGBSurface(SDL_SWSURFACE, SRCOFFS_TOTAL, MAX_SOURCE_Y, 8, 0, 0, 0, 0);
  if(g_hSourceBitmap == NULL) fprintf(stderr,"g_hSourceBitmap was not created!\n");

  g_pSourcePixels = (LPBYTE)g_hSourceBitmap->pixels;
  SDL_SetColors(g_hSourceBitmap, framebufferinfo, 0, 256);
		  //CreateDIBSection(
	//sourcedc,g_pSourceHeader,DIB_RGB_COLORS,
    //(LPVOID *)&g_pSourcePixels,0,0);
  //SelectObject(sourcedc,g_hSourceBitmap);

	// CREATE THE OFFSET TABLE FOR EACH SCAN LINE IN THE SOURCE IMAGE
	for (int y = 0; y < MAX_SOURCE_Y; y++)
		g_aSourceStartofLine[ y ] = g_pSourcePixels + SRCOFFS_TOTAL * y; //((MAX_SOURCE_Y-1) - y);

// before direct access to surface pixels we MUST? lock it (surface)
	int locked = 0;

	// DRAW THE SOURCE IMAGE INTO THE SOURCE BIT BUFFER
	ZeroMemory(g_pSourcePixels,SRCOFFS_TOTAL * /*512*/ MAX_SOURCE_Y);// be consistent, please,Thom! (bb) ^_^ ku

	if ((videotype != VT_MONO_CUSTOM) &&
		(videotype != VT_MONO_AMBER ) &&
		(videotype != VT_MONO_GREEN ) &&
		(videotype != VT_MONO_WHITE ))
	{
		DrawTextSource(g_hSourceBitmap);

		if ( SDL_MUSTLOCK(g_hSourceBitmap) ) {
			SDL_LockSurface(g_hSourceBitmap);
			locked = 1; // the source bitmap is locked
		}

		DrawLoResSource();
		if (videotype == VT_COLOR_HALF_SHIFT_DIM)
			DrawHiResSourceHalfShiftDim();
		else
			DrawHiResSource();
		DrawDHiResSource();
	}
	else
	{
		DrawMonoTextSource(g_hSourceBitmap);
		if ( SDL_MUSTLOCK(g_hSourceBitmap) ) {
			SDL_LockSurface(g_hSourceBitmap);
			locked = 1; // the source bitmap is locked
		}

		DrawMonoLoResSource();
		DrawMonoHiResSource();
		DrawMonoDHiResSource();
	}
	if(locked) SDL_UnlockSurface(g_hSourceBitmap);
	//	DeleteDC(sourcedc);
}

//===========================================================================
void DrawDHiResSource () {
  BYTE colorval[16] = {BLACK,   DARK_BLUE, DARK_GREEN,BLUE,
                       BROWN,   LIGHT_GRAY,GREEN,     AQUA,
                       DEEP_RED,MAGENTA,   DARK_GRAY, LIGHT_BLUE,
                       ORANGE,  PINK,      YELLOW,    WHITE};

#define OFFSET  3
#define SIZE    10
  for (int column = 0; column < 256; column++) {
    int coloffs = SIZE * column;
    for (unsigned byteval = 0; byteval < 256; byteval++) {
      int color[SIZE];
      ZeroMemory(color,sizeof(color));
      unsigned pattern = MAKEWORD(byteval,column);
      int pixel;
      for (pixel = 1; pixel < 15; pixel++) {
        if (pattern & (1 << pixel)) {
          int pixelcolor = 1 << ((pixel-OFFSET) & 3);
          if ((pixel >=  OFFSET+2) && (pixel < SIZE+OFFSET+2) && (pattern & (0x7 << (pixel-4))))
            color[pixel-(OFFSET+2)] |= pixelcolor;
          if ((pixel >=  OFFSET+1) && (pixel < SIZE+OFFSET+1) && (pattern & (0xF << (pixel-4))))
            color[pixel-(OFFSET+1)] |= pixelcolor;
          if ((pixel >=  OFFSET+0) && (pixel < SIZE+OFFSET+0))
            color[pixel-(OFFSET+0)] |= pixelcolor;
          if ((pixel >=  OFFSET-1) && (pixel < SIZE+OFFSET-1) && (pattern & (0xF << (pixel+1))))
            color[pixel-(OFFSET-1)] |= pixelcolor;
          if ((pixel >=  OFFSET-2) && (pixel < SIZE+OFFSET-2) && (pattern & (0x7 << (pixel+2))))
            color[pixel-(OFFSET-2)] |= pixelcolor;
        }
      }

	  if (videotype == VT_COLOR_TEXT_OPTIMIZED)
	  {
	    /***
	    activate for fringe reduction on white hgr text
	    drawback: loss of color mix patterns in hgr g_nAppMode.
	    select videotype by index
	    ***/

		for (pixel = 0; pixel < 13; pixel++)
		{
		  if ((pattern & (0xF << pixel)) == (unsigned)(0xF << pixel))
			for (int pos = pixel; pos < pixel + 4; pos++)
			  if (pos >= OFFSET && pos < SIZE+OFFSET)
				color[pos-OFFSET] = 15;
		}
	  }

      int y = byteval << 1;
      for (int x = 0; x < SIZE; x++) {
        SETSOURCEPIXEL(SRCOFFS_DHIRES+coloffs+x,y  ,colorval[color[x]]);
        SETSOURCEPIXEL(SRCOFFS_DHIRES+coloffs+x,y+1,colorval[color[x]]);
      }
    }
  }
#undef SIZE
#undef OFFSET
}


	enum ColorMapping
	{
		  CM_Magenta
		, CM_Blue
		, CM_Green
		, CM_Orange
		, CM_Black
		, CM_White
		, NUM_COLOR_MAPPING
	};

	const BYTE aColorIndex[ NUM_COLOR_MAPPING ] =
	{
		  HGR_MAGENTA
		, HGR_BLUE
		, HGR_GREEN
		, HGR_RED
		, HGR_BLACK
		, HGR_WHITE
	};

	const BYTE aColorDimmedIndex[ NUM_COLOR_MAPPING ] =
	{
		DARK_MAGENTA, // <- HGR_MAGENTA
		DARK_BLUE   , // <- HGR_BLUE
		DARK_GREEN  , // <- HGR_GREEN
		DEEP_RED    , // <- HGR_RED
		HGR_BLACK   , // no change
		LIGHT_GRAY    // HGR_WHITE
	};


//===========================================================================
void DrawHiResSourceHalfShiftDim ()
{
	//  BYTE colorval[6] = {MAGENTA,BLUE,GREEN,ORANGE,BLACK,WHITE};
	// BYTE colorval[6] = {HGR_MAGENTA,HGR_BLUE,HGR_GREEN,HGR_RED,HGR_BLACK,HGR_WHITE};
	for (int iColumn = 0; iColumn < 16; iColumn++)
	{
		int coloffs = iColumn << 5;

		for (unsigned iByte = 0; iByte < 256; iByte++)
		{
			int aPixels[11];

			aPixels[ 0] = iColumn & 4;
			aPixels[ 1] = iColumn & 8;
			aPixels[ 9] = iColumn & 1;
			aPixels[10] = iColumn & 2;

			int nBitMask = 1;
			int iPixel;
			for (iPixel  = 2; iPixel < 9; iPixel++) {
				aPixels[iPixel] = ((iByte & nBitMask) != 0);
				nBitMask <<= 1;
			}

			int hibit = ((iByte & 0x80) != 0);
			int x     = 0;
			int y     = iByte << 1;

			while (x < 28)
			{
				int adj = (x >= 14) << 1;
				int odd = (x >= 14);

				for (iPixel = 2; iPixel < 9; iPixel++)
				{
					int color = CM_Black;
					if (aPixels[iPixel])
					{
						if (aPixels[iPixel-1] || aPixels[iPixel+1])
						{
							color = CM_White;
						}
						else
							color = ((odd ^ (iPixel&1)) << 1) | hibit;
					}
					else if (aPixels[iPixel-1] && aPixels[iPixel+1])
					{
						/***
						activate for fringe reduction on white hgr text -
						drawback: loss of color mix patterns in hgr g_nAppMode.
						select videotype by index exclusion
						***/

						if (!(aPixels[iPixel-2] && aPixels[iPixel+2]))
							color = ((odd ^ !(iPixel&1)) << 1) | hibit;
					}

					/*
						Address Binary   -> Displayed
						2000:01 0---0001 -> 1 0 0 0  column 1
						2400:81 1---0001 ->  1 0 0 0 half-pixel shift right
						2800:02 1---0010 -> 0 1 0 0  column 2

						2000:02 column 2
						2400:82 half-pixel shift right
						2800:04 column 3

						2000:03 0---0011 -> 1 1 0 0  column 1 & 2
						2400:83 1---0011 ->  1 1 0 0 half-pixel shift right
						2800:06 1---0110 -> 0 1 1 0  column 2 & 3

						@reference: see Beagle Bro's Disk: "Silicon Salid", File: DOUBLE HI-RES
						Fortunately double-hires is supported via pixel doubling, so we can do half-pixel shifts ;-)
					*/
					switch (color)
					{
						case CM_Magenta:
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y  , HGR_MAGENTA  ); // aColorIndex
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y  , DARK_MAGENTA ); // aColorDimmedIndex
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y+1, HGR_MAGENTA  ); // aColorIndex
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y+1, DARK_MAGENTA ); // aColorDimmedIndex
							break;
						case CM_Blue   :
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y  , HGR_BLUE  );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+2,y  , DARK_BLUE );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y+1, HGR_BLUE  );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+2,y+1, DARK_BLUE );
							// Prevent column gaps
							if (hibit)
							{
								if (iPixel <= 2)
								{
									SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y  , DARK_BLUE );
									SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y+1, DARK_BLUE );
								}
							}
							break;
						case CM_Green :
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y  , HGR_GREEN  );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y  , DARK_GREEN );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y+1, HGR_GREEN  );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y+1, DARK_GREEN );
							break;
						case CM_Orange:
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y  , HGR_RED  );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+2,y  , BROWN ); // DARK_RED is a bit "too" red
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y+1, HGR_RED  );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+2,y+1, BROWN ); // DARK_RED is a bit "too" red
							// Prevent column gaps
							if (hibit)
							{
								if (iPixel <= 2)
								{
									SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y  , BROWN ); // DARK_RED is a bit "too" red
									SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y+1, BROWN ); // DARK_RED is a bit "too" red
								}
							}
							break;
						case CM_Black :
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y  , HGR_BLACK );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y  , HGR_BLACK );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y+1, HGR_BLACK );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y+1, HGR_BLACK );
							break;
						case CM_White :

#if HALF_SHIFT_DIM
							// 50% dither -- would look OK, except Gumball, on the "Gumball" font has splotches
//							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y  , HGR_WHITE );
//							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y+1, HGR_WHITE );
//							if (! hibit)
//							{
//								SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj ,y  , HGR_WHITE );
//								SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj ,y+1, HGR_WHITE );
//							}

							// 75% dither -- looks kind of nice actually.  Passes the Gumball cutscene quality test!
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y  , HGR_WHITE );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y+1, HGR_WHITE );

							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj ,y  , LIGHT_GRAY );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj ,y+1, LIGHT_GRAY );
#else
							// Don't dither / half-shift white, since DROL cutscene looks bad :(
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y  , HGR_WHITE );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y  , HGR_WHITE );
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y+1, HGR_WHITE ); // LIGHT_GRAY <- for that half scan-line look
							SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y+1, HGR_WHITE ); // LIGHT_GRAY <- for that half scan-line look
							// Prevent column gaps
							if (hibit)
							{
								if (iPixel <= 2)
								{
									SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y  , HGR_WHITE ); // LIGHT_GRAY HGR_GREY1
									SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y+1, HGR_WHITE ); // LIGHT_GRAY HGR_GREY1
								}
							}
#endif
							break;
						default:
							break;
					}
					x += 2;
				}
			}
		}
	}
}


//===========================================================================
void DrawHiResSource ()
{
	//  BYTE colorval[6] = {MAGENTA,BLUE,GREEN,ORANGE,BLACK,WHITE};
	// BYTE colorval[6] = {HGR_MAGENTA,HGR_BLUE,HGR_GREEN,HGR_RED,HGR_BLACK,HGR_WHITE};
	for (int iColumn = 0; iColumn < 16; iColumn++)
	{
		int coloffs = iColumn << 5;

		for (unsigned iByte = 0; iByte < 256; iByte++)
		{
			int aPixels[11];

			aPixels[ 0] = iColumn & 4;
			aPixels[ 1] = iColumn & 8;
			aPixels[ 9] = iColumn & 1;
			aPixels[10] = iColumn & 2;

			int nBitMask = 1;
			int iPixel;
			for (iPixel  = 2; iPixel < 9; iPixel++) {
				aPixels[iPixel] = ((iByte & nBitMask) != 0);
				nBitMask <<= 1;
			}

			int hibit = ((iByte & 0x80) != 0);
			int x     = 0;
			int y     = iByte << 1;

			while (x < 28)
			{
				int adj = (x >= 14) << 1;
				int odd = (x >= 14);

				for (iPixel = 2; iPixel < 9; iPixel++)
				{
					int color = CM_Black;
					if (aPixels[iPixel])
					{
						if (aPixels[iPixel-1] || aPixels[iPixel+1])
							color = CM_White;
						else
							color = ((odd ^ (iPixel&1)) << 1) | hibit;
					}
					else if (aPixels[iPixel-1] && aPixels[iPixel+1])
					{
						/***
						activate for fringe reduction on white hgr text -
						drawback: loss of color mix patterns in hgr g_nAppMode.
						select videotype by index exclusion
						***/

						if ((videotype == VT_COLOR_STANDARD) || (videotype == VT_COLOR_TVEMU) || !(aPixels[iPixel-2] && aPixels[iPixel+2]))
							color = ((odd ^ !(iPixel&1)) << 1) | hibit;	// // No white HGR text optimization
					}

					// Colors - Top/Bottom Left/Right
					// cTL cTR
					// cBL cBR
					SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y  ,aColorIndex[color]); // TL
					SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y  ,aColorIndex[color]); // TR
					SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj  ,y+1,aColorIndex[color]); // BL
					SETSOURCEPIXEL(SRCOFFS_HIRES+coloffs+x+adj+1,y+1,aColorIndex[color]); // BR
					x += 2;
				}
			}
		}
	}
}


//===========================================================================
void DrawLoResSource () {
  BYTE colorval[16] = {BLACK,     DEEP_RED, DARK_BLUE, MAGENTA,
                       DARK_GREEN,DARK_GRAY,BLUE,      LIGHT_BLUE,
                       BROWN,     ORANGE,   LIGHT_GRAY,PINK,
                       GREEN,     YELLOW,   AQUA,      WHITE};
  for (int color = 0; color < 16; color++)
    for (int x = 0; x < 16; x++)
      for (int y = 0; y < 16; y++)
        SETSOURCEPIXEL(SRCOFFS_LORES+x,(color << 4)+y,colorval[color]);
}


//===========================================================================
int GetMonochromeIndex()
{
	int iMonochrome;

	switch (videotype)
	{
		case VT_MONO_AMBER: iMonochrome = MONOCHROME_AMBER ; break;
		case VT_MONO_GREEN: iMonochrome = MONOCHROME_GREEN ; break;
		case VT_MONO_WHITE: iMonochrome = MONOCHROME_WHITE ; break;
		default           : iMonochrome = MONOCHROME_CUSTOM; break;
	}

	return iMonochrome;
}


//===========================================================================
void DrawMonoDHiResSource ()
{
	int iMonochrome = GetMonochromeIndex();

	for (int column = 0; column < 256; column++)
	{
		int coloffs = 10 * column;
		for (unsigned byteval = 0; byteval < 256; byteval++)
		{
			unsigned pattern = MAKEWORD(byteval,column);
			int      y       = byteval << 1;
			for (int x = 0; x < 10; x++)
			{
				BYTE colorval = pattern & (1 << (x+3)) ? iMonochrome : BLACK;

				SETSOURCEPIXEL(SRCOFFS_DHIRES+coloffs+x,y  ,colorval);
				SETSOURCEPIXEL(SRCOFFS_DHIRES+coloffs+x,y+1,colorval);
			}
		}
	}
}

//===========================================================================
void DrawMonoHiResSource ()
{
	int iMonochrome = GetMonochromeIndex();

	for (int column = 0; column < 512; column += 16)
	{
		for (int y = 0; y < 512; y += 2)
		{
			unsigned val = (y >> 1);
			for (int x = 0; x < 16; x += 2)
			{
				BYTE colorval = (val & 1) ? iMonochrome : BLACK;
				val >>= 1;
				SETSOURCEPIXEL(SRCOFFS_HIRES+column+x  ,y  ,colorval);
				SETSOURCEPIXEL(SRCOFFS_HIRES+column+x+1,y  ,colorval);
				SETSOURCEPIXEL(SRCOFFS_HIRES+column+x  ,y+1,colorval);
				SETSOURCEPIXEL(SRCOFFS_HIRES+column+x+1,y+1,colorval);
			}
		}
	}
}

//===========================================================================
void DrawMonoLoResSource () {
	int iMonochrome = GetMonochromeIndex();

  for (int color = 0; color < 16; color++)
    for (int x = 0; x < 16; x++)
      for (int y = 0; y < 16; y++) {
        BYTE colorval = (color >> (x & 3) & 1) ? iMonochrome : BLACK;
        SETSOURCEPIXEL(SRCOFFS_LORES+x,(color << 4)+y,colorval);
      }
}

//===========================================================================
void DrawMonoTextSource (SDL_Surface * hDstDC)
{
//	HDC     hSrcDC  = CreateCompatibleDC(hDstDC);
//	HBITMAP hBitmap = LoadBitmap(g_hInstance,TEXT("CHARSET40"));
	if(charset40 == NULL) return;

	Uint8 hBrush;
 	switch (videotype)
 	{
 		case VT_MONO_AMBER: hBrush = MONOCHROME_AMBER/*CreateSolidBrush(RGB(0xFF,0x80,0x00))*/; break;
 		case VT_MONO_GREEN: hBrush = MONOCHROME_GREEN/*CreateSolidBrush(RGB(0x00,0xC0,0x00))*/; break;
 		case VT_MONO_WHITE: hBrush = MONOCHROME_WHITE/*CreateSolidBrush(RGB(0xFF,0xFF,0xFF))*/; break;
 		default           : hBrush = MONOCHROME_CUSTOM/*CreateSolidBrush(monochrome)*/; break;
 	}
// 	SelectObject(hSrcDC,hBitmap);
// 	SelectObject(hDstDC,hBrush);
	SDL_Rect srcrect, dstrect;
	dstrect.x = SRCOFFS_40COL;
	dstrect.y = 0;
	dstrect.w = 256;
	dstrect.h = 512;

	srcrect.x = 0;
	srcrect.y = 0;
	srcrect.w = 256;
	srcrect.h = 512;

	// TODO: Update with APPLE_FONT_Y_ values
//	BitBlt(hDstDC,SRCOFFS_40COL,0,256,512,hSrcDC,0,0,MERGECOPY);
	SDL_SoftStretchMono8(charset40, &srcrect, hDstDC, &dstrect, hBrush);

	dstrect.x = SRCOFFS_IIPLUS;
	dstrect.y = 0;
	dstrect.w = 256;
	dstrect.h = 256;
	srcrect.x = 0;
	srcrect.y = 512; // won't work?
	srcrect.w = 256;
	srcrect.h = 256;
	SDL_SoftStretchMono8(charset40, &srcrect, hDstDC, &dstrect, hBrush);
//	BitBlt(hDstDC,SRCOFFS_IIPLUS,0,256,256,hSrcDC,0,512,MERGECOPY);

	dstrect.x = SRCOFFS_80COL;
	dstrect.y = 0;
	dstrect.w = 128;
	dstrect.h = 512;

	srcrect.x = srcrect.y = 0;
	srcrect.w = 256;
	srcrect.h = 512;
	SDL_SoftStretchMono8(charset40, &srcrect, hDstDC, &dstrect, hBrush);
//	StretchBlt(hDstDC,SRCOFFS_80COL,0,128,512,hSrcDC,0,0,256,512,MERGECOPY);

//	SelectObject(hDstDC,GetStockObject(NULL_BRUSH));
//	DeleteObject(hBrush);
//	DeleteDC(hSrcDC);
//	DeleteObject(hBitmap);
}

//===========================================================================
void DrawTextSource (SDL_Surface * dc)
{
//	HDC     memdc  = CreateCompatibleDC(dc);
//	HBITMAP bitmap = LoadBitmap(g_hInstance,TEXT("CHARSET40"));
//	SelectObject(memdc,bitmap);
	if(charset40 == NULL) return;
	SDL_Rect srcrect, dstrect;

	dstrect.x = SRCOFFS_40COL;
	dstrect.y = 0;
	dstrect.w = srcrect.w = 256;
	dstrect.h = srcrect.h = 512;

	srcrect.x = 0;
	srcrect.y = 0;
	SDL_SoftStretch(charset40, &srcrect, dc, &dstrect);
// 	BitBlt(
// 		dc                // hdcDest
// 		,SRCOFFS_40COL ,0 // nXDest, nYDest
// 		,256 ,512         // nWidth, nHeight
// 		,memdc            // hdcSrc
// 		,0 ,0             // nXSrc, nYSrc
// 		,SRCCOPY );       // dwRop

	// Chars for Apple ][
	dstrect.x = SRCOFFS_IIPLUS;
	dstrect.y = 0;
	dstrect.w = srcrect.w = 256;
	dstrect.h = srcrect.h = 256;

	srcrect.x = 0;
	srcrect.y = 512;
	SDL_SoftStretch(charset40, &srcrect, dc, &dstrect);

//	BitBlt(dc,SRCOFFS_IIPLUS,0,256,256,memdc,0,512,SRCCOPY);

	// Chars for 80 col mode
	dstrect.x = SRCOFFS_80COL;
	dstrect.y = 0;
	dstrect.w = 128;
	dstrect.h = 512;

	srcrect.x = srcrect.y = 0;
	srcrect.w = 256;
	srcrect.h = 512;
	SDL_SoftStretch(charset40, &srcrect, dc, &dstrect);

//	StretchBlt(dc,SRCOFFS_80COL,0,128,512,memdc,0,0,256,512,SRCCOPY);

//	DeleteDC(memdc);
//	DeleteObject(bitmap);
}

//===========================================================================
void SetLastDrawnImage () {
  memcpy(vidlastmem+0x400,g_pTextBank0,0x400);
  if (SW_HIRES)
    memcpy(vidlastmem+0x2000,g_pHiresBank0,0x2000);
  if (SW_DHIRES && SW_HIRES)
    memcpy(vidlastmem,g_pHiresBank1,0x2000);
  else if (SW_80COL)	// Don't test for !SW_HIRES, as some 80-col text routines have SW_HIRES set (Bug #8300)
    memcpy(vidlastmem,g_pTextBank1,0x400);
  int loop;
  for (loop = 0; loop < 256; loop++)
    *(memdirty+loop) &= ~2;
}

//===========================================================================

bool Update40ColCell (int x, int y, int xpixel, int ypixel, int offset)
{
	BYTE ch = *(g_pTextBank0+offset);
	bool bCharChanged = (ch != *(vidlastmem+offset+0x400) || redrawfull);

	// FLASHing chars:
	// - FLASHing if:Alt Char Set is OFF && 0x40<=char<=0x7F
	// - The inverse of this char is located at: char+0x40
	bool bCharFlashing = (g_nAltCharSetOffset == 0) && (ch >= 0x40) && (ch <= 0x7F);

	if(bCharChanged || (bCharFlashing && g_bTextFlashFlag))
	{
		bool bInvert = bCharFlashing ? g_bTextFlashState : false;

		CopySource(xpixel,ypixel,
			APPLE_FONT_WIDTH, APPLE_FONT_HEIGHT,
			(IS_APPLE2 ? SRCOFFS_IIPLUS : SRCOFFS_40COL) + ((ch & 0x0F) << 4),
			(ch & 0xF0) + g_nAltCharSetOffset + (bInvert ? 0x40 : 0x00));

		return true;
	}

	return false;
}

inline bool _Update80ColumnCell( BYTE c, const int xPixel, const int yPixel, bool bCharFlashing )
{
	bool bInvert = bCharFlashing ? g_bTextFlashState : false;

	CopySource(
		xPixel, yPixel,
		(APPLE_FONT_WIDTH / 2), APPLE_FONT_HEIGHT,
		SRCOFFS_80COL + ((c & 15)<<3),
		((c >>4) <<4) + g_nAltCharSetOffset + (bInvert ? 0x40 : 0x00));

	return true;
}

//===========================================================================
bool Update80ColCell (int x, int y, int xpixel, int ypixel, int offset)
{
	bool bDirty = false;

#if FLASH_80_COL
	BYTE c1 = *(g_pTextBank1 + offset); // aux
	BYTE c0 = *(g_pTextBank0 + offset); // main

	bool bC1Changed = (c1 != *(vidlastmem + offset +     0) || redrawfull);
	bool bC0Changed = (c0 != *(vidlastmem + offset + 0x400) || redrawfull);

	bool bC1Flashing = (g_nAltCharSetOffset == 0) && (c1 >= 0x40) && (c1 <= 0x7F);
	bool bC0Flashing = (g_nAltCharSetOffset == 0) && (c0 >= 0x40) && (c0 <= 0x7F);

	if (bC1Changed || (bC1Flashing && g_bTextFlashFlag))
		bDirty = _Update80ColumnCell( c1, xpixel, ypixel, bC1Flashing );

	if (bC0Changed || (bC0Flashing && g_bTextFlashFlag))
		bDirty |= _Update80ColumnCell( c0, xpixel + 7, ypixel, bC0Flashing );

#else
	BYTE auxval = *(g_pTextBank1 + offset); // aux
	BYTE mainval = *(g_pTextBank0 + offset); // main

	if ((auxval  != *(vidlastmem+offset)) ||
		(mainval != *(vidlastmem+offset+0x400)) ||
		redrawfull)
	{
		CopySource(xpixel,ypixel,
			(APPLE_FONT_WIDTH / 2), APPLE_FONT_HEIGHT,
			SRCOFFS_80COL + ((auxval & 15)<<3),
			((auxval>>4)<<4) + g_nAltCharSetOffset);

		CopySource(xpixel+7,ypixel,
			(APPLE_FONT_WIDTH / 2), APPLE_FONT_HEIGHT,
			SRCOFFS_80COL + ((mainval & 15)<<3),
			((mainval>>4)<<4) + g_nAltCharSetOffset );

		bDirty = true;
	}
#endif

	return bDirty;
}

//===========================================================================
bool UpdateDHiResCell (int x, int y, int xpixel, int ypixel, int offset)
{
	bool bDirty = false;
	int  yoffset = 0;
  while (yoffset < 0x2000) {
    BYTE byteval1 = (x >  0) ? *(g_pHiresBank0+offset+yoffset-1) : 0;
    BYTE byteval2 = *(g_pHiresBank1 +offset+yoffset);
    BYTE byteval3 = *(g_pHiresBank0+offset+yoffset);
    BYTE byteval4 = (x < 39) ? *(g_pHiresBank1 +offset+yoffset+1) : 0;
    if ((byteval2 != *(vidlastmem+offset+yoffset)) ||
        (byteval3 != *(vidlastmem+offset+yoffset+0x2000)) ||
        ((x >  0) && ((byteval1 & 0x70) != (*(vidlastmem+offset+yoffset+0x1FFF) & 0x70))) ||
        ((x < 39) && ((byteval4 & 0x07) != (*(vidlastmem+offset+yoffset+     1) & 0x07))) ||
        redrawfull) {
      DWORD dwordval = (byteval1 & 0x70)        | ((byteval2 & 0x7F) << 7) |
                      ((byteval3 & 0x7F) << 14) | ((byteval4 & 0x07) << 21);
#define PIXEL  0
#define COLOR  ((xpixel + PIXEL) & 3)
#define VALUE  (dwordval >> (4 + PIXEL - COLOR))
      CopySource(xpixel+PIXEL,ypixel+(yoffset >> 9),7,2,
                 SRCOFFS_DHIRES+10*HIBYTE(VALUE)+COLOR,LOBYTE(VALUE)<<1);
#undef PIXEL
#define PIXEL  7
      CopySource(xpixel+PIXEL,ypixel+(yoffset >> 9),7,2,
                 SRCOFFS_DHIRES+10*HIBYTE(VALUE)+COLOR,LOBYTE(VALUE)<<1);
#undef PIXEL
#undef COLOR
#undef VALUE
      bDirty = true;
    }
    yoffset += 0x400;
  }

	return bDirty;
}


//===========================================================================
BYTE MixColors(BYTE c1, BYTE c2) {	// For tv emulation g_nAppMode
#define COMBINATION(c1,c2,ref1,ref2) (((c1)==(ref1)&&(c2)==(ref2)) || ((c1)==(ref2)&&(c2)==(ref1)))

  if (c1 == c2)
	  return c1;
  if (COMBINATION(c1,c2,HGR_BLUE,HGR_RED))
	  return HGR_GREY1;
  else if (COMBINATION(c1,c2,HGR_GREEN,HGR_MAGENTA))
	  return HGR_GREY2;
  else if (COMBINATION(c1,c2,HGR_RED,HGR_GREEN))
	  return HGR_YELLOW;
  else if (COMBINATION(c1,c2,HGR_BLUE,HGR_GREEN))
	  return HGR_AQUA;
  else if (COMBINATION(c1,c2,HGR_BLUE,HGR_MAGENTA))
	  return HGR_PURPLE;
  else if (COMBINATION(c1,c2,HGR_RED,HGR_MAGENTA))
	  return HGR_PINK;
  else
	  return MONOCHROME_CUSTOM; // visible failure indicator

#undef COMBINATION
}


//===========================================================================
void CreateColorMixMap() {	// For tv emulation g_nAppMode
#define FROM_NEIGHBOUR 0x00

  int t,m,b;
  BYTE cTop, cMid, cBot;
  WORD mixTop, mixBot;

  for (t=0; t<6; t++)
    for (m=0; m<6; m++)
	  for (b=0; b<6; b++) {
        cTop = t | 0x10;
		cMid = m | 0x10;
		cBot = b | 0x10;
        if (cMid < HGR_BLUE) {
		  mixTop = mixBot = cMid;
		} else {
			if (cTop < HGR_BLUE) {
		      mixTop = FROM_NEIGHBOUR;
			} else {
			  mixTop = MixColors(cMid,cTop);
			}
			if (cBot < HGR_BLUE)  {
			  mixBot = FROM_NEIGHBOUR;
			} else {
			  mixBot = MixColors(cMid,cBot);
			}
			if (mixTop == FROM_NEIGHBOUR && mixBot != FROM_NEIGHBOUR) {
			  mixTop = mixBot;
			} else if (mixBot == FROM_NEIGHBOUR && mixTop != FROM_NEIGHBOUR) {
			  mixBot = mixTop;
			} else if (mixBot == FROM_NEIGHBOUR && mixTop == FROM_NEIGHBOUR) {
			  mixBot = mixTop = cMid;
			}
		}
		colormixmap[t][m][b] = (mixTop << 8) | mixBot;
	  }
#undef FROM_NEIGHBOUR
}

//===========================================================================
void /*__stdcall */ MixColorsVertical(int matx, int maty) {	// For tv emulation g_nAppMode

  WORD twoHalfPixel;
  int bot1idx, bot2idx;

  if (SW_MIXED && maty > 159) {
	  if (maty < 161) {
        bot1idx = hgrpixelmatrix[matx][maty+1] & 0x0F;
        bot2idx = 0;
	  } else {
        bot1idx = bot2idx = 0;
	  }
  } else {
    bot1idx = hgrpixelmatrix[matx][maty+1] & 0x0F;
    bot2idx = hgrpixelmatrix[matx][maty+2] & 0x0F;
  }

  twoHalfPixel = colormixmap[hgrpixelmatrix[matx][maty-2] & 0x0F]
	                        [hgrpixelmatrix[matx][maty-1] & 0x0F]
                            [hgrpixelmatrix[matx][maty  ] & 0x0F];
  colormixbuffer[0] = (twoHalfPixel & 0xFF00) >> 8;
  colormixbuffer[1] = twoHalfPixel & 0x00FF;

  twoHalfPixel = colormixmap[hgrpixelmatrix[matx][maty-1] & 0x0F]
	                        [hgrpixelmatrix[matx][maty  ] & 0x0F]
                            [bot1idx];
  colormixbuffer[2] = (twoHalfPixel & 0xFF00) >> 8;
  colormixbuffer[3] = twoHalfPixel & 0x00FF;

  twoHalfPixel = colormixmap[hgrpixelmatrix[matx][maty  ] & 0x0F]
	                        [bot1idx]
                            [bot2idx];
  colormixbuffer[4] = (twoHalfPixel & 0xFF00) >> 8;
  colormixbuffer[5] = twoHalfPixel & 0x00FF;

}


//===========================================================================
void /*__stdcall */ CopyMixedSource (int x, int y, int sourcex, int sourcey) {	// For tv emulation g_nAppMode

  LPBYTE currsourceptr = g_aSourceStartofLine[sourcey]+sourcex;
  LPBYTE currdestptr   = frameoffsettable[y<<1] + (x<<1);
  LPBYTE currptr;

  int matx = x;
  int maty = HGR_MATRIX_YOFFSET + y;
  int count;
  int bufxoffset;
  int hgrlinesabove = (y > 0)?   1 : 0;
  int hgrlinesbelow = SW_MIXED ? ((y < 159)? 1:0) : ((y < 191)? 1:0);
  int i;
  int istart        = 2 - (hgrlinesabove << 1);
  int iend          = 3 + (hgrlinesbelow << 1);

  // transfer 7 pixels (i.e. the visible part of an apple hgr-byte) from row to pixelmatrix
  for (count = 0, bufxoffset = 0;
       count < 7;
	   count++, bufxoffset += 2) {
    hgrpixelmatrix[matx+count][maty] = *(currsourceptr+bufxoffset);

	// color mixing between adjacent scanlines at current x position
    MixColorsVertical(matx+count, maty);

	// transfer up to 6 mixed (half-)pixels of current column to framebuffer
    currptr = currdestptr+bufxoffset;
	if (hgrlinesabove)
		currptr -= framebufferpitch << 1;	// we just should change sign of op: '-' instead of '+'

    for (i = istart;
	     i <= iend;
	     currptr += framebufferpitch, i++) {	// and vice versa
         *currptr = *(currptr+1) = colormixbuffer[i];
	}
  }
}


//===========================================================================
bool UpdateHiResCell (int x, int y, int xpixel, int ypixel, int offset)
{
	bool bDirty  = false;
	int  yoffset = 0;
  while (yoffset < 0x2000) {
    BYTE byteval1 = (x >  0) ? *(g_pHiresBank0+offset+yoffset-1) : 0;
    BYTE byteval2 = *(g_pHiresBank0+offset+yoffset);
    BYTE byteval3 = (x < 39) ? *(g_pHiresBank0+offset+yoffset+1) : 0;
    if ((byteval2 != *(vidlastmem+offset+yoffset+0x2000)) ||
        ((x >  0) && ((byteval1 & 0x60) != (*(vidlastmem+offset+yoffset+0x1FFF) & 0x60))) ||
        ((x < 39) && ((byteval3 & 0x03) != (*(vidlastmem+offset+yoffset+0x2001) & 0x03))) ||
        redrawfull) {
#define COLOFFS  (((byteval1 & 0x60) << 2) | \
                  ((byteval3 & 0x03) << 5))
		if (videotype == VT_COLOR_TVEMU)
		{
  			CopyMixedSource(xpixel >> 1, (ypixel+(yoffset >> 9)) >> 1,
							SRCOFFS_HIRES+COLOFFS+((x & 1) << 4),(((int)byteval2) << 1));
		}
		else
		{
			CopySource(xpixel,ypixel+(yoffset >> 9),
					   14,2,
					   SRCOFFS_HIRES+COLOFFS+((x & 1) << 4),(((int)byteval2) << 1));
		}
#undef COLOFFS
      bDirty = true;
    }
    yoffset += 0x400;
  }

	return bDirty;
}

//===========================================================================
bool UpdateLoResCell (int x, int y, int xpixel, int ypixel, int offset)
{
	BYTE val = *(g_pTextBank0+offset);
	if ((val != *(vidlastmem+offset+0x400)) || redrawfull)
	{
		CopySource(xpixel,ypixel,
			14,8,
			SRCOFFS_LORES+((x & 1) << 1),((val & 0xF) << 4));
		CopySource(xpixel,ypixel+8,
			14,8,
			SRCOFFS_LORES+((x & 1) << 1),(val & 0xF0));
		return true;
	}

	return false;
}

//===========================================================================

bool UpdateDLoResCell (int x, int y, int xpixel, int ypixel, int offset)
{
	BYTE auxval  = *(g_pTextBank1 +offset);
	BYTE mainval = *(g_pTextBank0+offset);

	if	(	(auxval != *(vidlastmem+offset)) ||
			(mainval != *(vidlastmem+offset+0x400)) ||
			redrawfull
		)
	{
		CopySource(	xpixel,ypixel,
			7,8,
			SRCOFFS_LORES+((x & 1) << 1),((auxval & 0xF) << 4));

		CopySource(	xpixel,ypixel+8,
			7,8,
			SRCOFFS_LORES+((x & 1) << 1),(auxval & 0xF0));

		//

		CopySource(	xpixel+7,ypixel,
			7,8,
			SRCOFFS_LORES+((x & 1) << 1),((mainval & 0xF) << 4));

		CopySource(	xpixel+7,ypixel+8,
			7,8,
			SRCOFFS_LORES+((x & 1) << 1),(mainval & 0xF0));

		return true;
	}

	return false;
}


//
// ----- ALL GLOBALLY ACCESSIBLE FUNCTIONS ARE BELOW THIS LINE -----
//

//===========================================================================
BOOL VideoApparentlyDirty ()
{
	if (SW_MIXED || redrawfull)
		return 1;

	DWORD address = (SW_HIRES && !SW_TEXT) ? (0x20 << displaypage2) : (0x4 << displaypage2);
	DWORD length  = (SW_HIRES && !SW_TEXT) ? 0x20 : 0x4;
	while (length--)
		if (*(memdirty+(address++)) & 2)
			return 1;

	//

	bool bCharFlashing = false;

	// Scan visible text page for any flashing chars
	if((SW_TEXT || SW_MIXED) && (g_nAltCharSetOffset == 0))
	{
		BYTE* pnMemText = MemGetMainPtr(0x400 << displaypage2);

		// Scan 8 long-lines of 120 chars (at 128 char offsets):
		// . Skip 8-char holes in TEXT
		for(UINT y=0; y<8; y++)
		{
			for(UINT x=0; x<40*3; x++)
			{
				BYTE ch = pnMemText[y*128+x];
				if((ch >= 0x40) && (ch <= 0x7F))
				{
					bCharFlashing = true;
					break;
				}
			}
		}
	}

	if(bCharFlashing)
		return 1;

	return 0;
}

//===========================================================================
void VideoBenchmark () {
   /*Sleep*/SDL_Delay(1500);	// wait for 1.5 sec before running benchmark

   // PREPARE TWO DIFFERENT FRAME BUFFERS, EACH OF WHICH HAVE HALF OF THE
   // BYTES SET TO 0x14 AND THE OTHER HALF SET TO 0xAA
   int     loop;
  LPDWORD mem32 = (LPDWORD)mem;
  for (loop = 4096; loop < 6144; loop++)
    *(mem32+loop) = ((loop & 1) ^ ((loop & 0x40) >> 6)) ? 0x14141414
                                                        : 0xAAAAAAAA;
  for (loop = 6144; loop < 8192; loop++)
    *(mem32+loop) = ((loop & 1) ^ ((loop & 0x40) >> 6)) ? 0xAAAAAAAA
                                                        : 0x14141414;

  // SEE HOW MANY TEXT FRAMES PER SECOND WE CAN PRODUCE WITH NOTHING ELSE
  // GOING ON, CHANGING HALF OF THE BYTES IN THE VIDEO BUFFER EACH FRAME TO
  // SIMULATE THE ACTIVITY OF AN AVERAGE GAME
  DWORD totaltextfps = 0;
  vidmode            = VF_TEXT;
  FillMemory(mem+0x400,0x400,0x14);
  VideoRedrawScreen();
  DWORD milliseconds = GetTickCount();
  while (GetTickCount() == milliseconds) ;
  milliseconds = GetTickCount();
  DWORD cycle = 0;
  do {
    if (cycle & 1)
      FillMemory(mem+0x400,0x400,0x14);
    else
      CopyMemory(mem+0x400,mem+((cycle & 2) ? 0x4000 : 0x6000),0x400);
    VideoRefreshScreen();
    if (cycle++ >= 3)
      cycle = 0;
    totaltextfps++;
  } while (GetTickCount() - milliseconds < 1000);

  // SEE HOW MANY HIRES FRAMES PER SECOND WE CAN PRODUCE WITH NOTHING ELSE
  // GOING ON, CHANGING HALF OF THE BYTES IN THE VIDEO BUFFER EACH FRAME TO
  // SIMULATE THE ACTIVITY OF AN AVERAGE GAME
  DWORD totalhiresfps = 0;
  vidmode             = VF_HIRES;
  FillMemory(mem+0x2000,0x2000,0x14);
  VideoRedrawScreen();
  milliseconds = GetTickCount();
  while (GetTickCount() == milliseconds) ;
  milliseconds = GetTickCount();
  cycle = 0;
  do {
    if (cycle & 1)
      FillMemory(mem+0x2000,0x2000,0x14);
    else
      CopyMemory(mem+0x2000,mem+((cycle & 2) ? 0x4000 : 0x6000),0x2000);
    VideoRefreshScreen();
    if (cycle++ >= 3)
      cycle = 0;
    totalhiresfps++;
  } while (GetTickCount() - milliseconds < 1000);

  // DETERMINE HOW MANY 65C02 CLOCK CYCLES WE CAN EMULATE PER SECOND WITH
  // NOTHING ELSE GOING ON
  CpuSetupBenchmark();
  DWORD totalmhz10 = 0;
  milliseconds     = GetTickCount();
  while (GetTickCount() == milliseconds) ;
  milliseconds = GetTickCount();
  cycle = 0;
  do {
    CpuExecute(100000);
    totalmhz10++;
  } while (GetTickCount() - milliseconds < 1000);

  // IF THE PROGRAM COUNTER IS NOT IN THE EXPECTED RANGE AT THE END OF THE
  // CPU BENCHMARK, REPORT AN ERROR AND OPTIONALLY TRACK IT DOWN
  if ((regs.pc < 0x300) || (regs.pc > 0x400))
 /*   if (MessageBox(g_hFrameWindow,
                   TEXT("The emulator has detected a problem while running ")
                   TEXT("the CPU benchmark.  Would you like to gather more ")
                   TEXT("information?"),
                   TEXT("Benchmarks"),
                   MB_ICONQUESTION | MB_YESNO | MB_SETFOREGROUND) == IDYES) */
  {
	  printf("The emulator has detected a problem while running the CPU benchmark.\n");

      BOOL error  = 0;
      WORD lastpc = 0x300;
      int  loop   = 0;
      while ((loop < 10000) && !error) {
        CpuSetupBenchmark();
        CpuExecute(loop);
        if ((regs.pc < 0x300) || (regs.pc > 0x400))
          error = 1;
        else {
          lastpc = regs.pc;
          ++loop;
        }
      }
      if (error) {
      /*  TCHAR outstr[256];
      wsprintf(outstr,
                 TEXT("The emulator experienced an error %u clock cycles ")
                 TEXT("into the CPU benchmark.  Prior to the error, the ")
                 TEXT("program counter was at $%04X.  After the error, it ")
                 TEXT("had jumped to $%04X."),
                 (unsigned)loop,
                 (unsigned)lastpc,
                 (unsigned)regs.pc);
        MessageBox(g_hFrameWindow,
                   outstr,
                   TEXT("Benchmarks"),
                   MB_ICONINFORMATION | MB_SETFOREGROUND);*/
	      FILE *f = fopen("//wiiapple-error.txt", "a+");
	      if (f) {
		      fprintf(f, "The emulator experienced an error %u clock cycles into the CPU benchmark.\n",(unsigned)loop);
		      fprintf(f, "Prior to the error, the program counter was at $%04X.\n", (unsigned)lastpc);
		      fprintf(f, " After the error, it had jumped to $%04X.\n", (unsigned)regs.pc);
		      fclose(f);
	      }
	SDL_Delay(5000);
      }
      else {
        /*MessageBox(g_hFrameWindow,
                   TEXT("The emulator was unable to locate the exact ")
                   TEXT("point of the error.  This probably means that ")
                   TEXT("the problem is external to the emulator, ")
                   TEXT("happening asynchronously, such as a problem in ")
                   TEXT("a timer interrupt handler."),
                   TEXT("Benchmarks"),
                   MB_ICONINFORMATION | MB_SETFOREGROUND);*/
	      	      FILE *f = fopen("//wiiapple-error2.txt", "a+");
		      if (f) {
			      fprintf(f, "The emulator was unable to locate the exact point of the error.\n");
			      fprintf(f, "This probably means that the problem is external to the emulator, happening asynchronously,\n");
			      fprintf(f, "such as a problem in a timer interrupt handler.\n");
			      fclose(f);
		      }
	SDL_Delay(5000);
      }
    }

  // DO A REALISTIC TEST OF HOW MANY FRAMES PER SECOND WE CAN PRODUCE
  // WITH FULL EMULATION OF THE CPU, JOYSTICK, AND DISK HAPPENING AT
  // THE SAME TIME
  DWORD realisticfps = 0;
  FillMemory(mem+0x2000,0x2000,0xAA);
  VideoRedrawScreen();
  milliseconds = GetTickCount();
  while (GetTickCount() == milliseconds) ;
  milliseconds = GetTickCount();
  cycle = 0;
  do {
    if (realisticfps < 10) {
      int cycles = 100000;
      while (cycles > 0) {
        DWORD executedcycles = CpuExecute(103);
        cycles -= executedcycles;
        DiskUpdatePosition(executedcycles);
        JoyUpdatePosition();
        VideoUpdateVbl(0);
	  }
    }
    if (cycle & 1)
      FillMemory(mem+0x2000,0x2000,0xAA);
    else
      CopyMemory(mem+0x2000,mem+((cycle & 2) ? 0x4000 : 0x6000),0x2000);
    VideoRefreshScreen();
    if (cycle++ >= 3)
      cycle = 0;
    realisticfps++;
  } while (GetTickCount() - milliseconds < 1000);

  // DISPLAY THE RESULTS

/*  VideoDisplayLogo();
  TCHAR outstr[256];
  wsprintf(outstr,
           TEXT("Pure Video FPS:\t%u hires, %u text\n")
           TEXT("Pure CPU MHz:\t%u.%u%s\n\n")
           TEXT("EXPECTED AVERAGE VIDEO GAME\n")
           TEXT("PERFORMANCE: %u FPS"),
           (unsigned)totalhiresfps,
           (unsigned)totaltextfps,
           (unsigned)(totalmhz10/10),
           (unsigned)(totalmhz10 % 10),
           (LPCTSTR)(IS_APPLE2 ? TEXT(" (6502)") : TEXT("")),
           (unsigned)realisticfps);
  MessageBox(g_hFrameWindow,
             outstr,
             TEXT("Benchmarks"),
             MB_ICONINFORMATION | MB_SETFOREGROUND);*/
  printf("Pure Video FPS:\t%u hires, %u text\n", (unsigned)totalhiresfps, (unsigned)totaltextfps);
  printf("Pure CPU MHz:\t%u.%u%s\n\n", (unsigned)(totalmhz10 / 10), (unsigned)(totalmhz10 % 10),
	 (LPCTSTR)(IS_APPLE2 ? TEXT(" (6502)") : TEXT("")));
  printf("EXPECTED AVERAGE VIDEO GAME PERFORMANCE:\t%u FPS\n\n", (unsigned)realisticfps);
  /*Sleep*/SDL_Delay(1500);

}// VideoBenchmark()


//===========================================================================
BYTE /*__stdcall*/ VideoCheckMode (WORD, WORD address, BYTE, BYTE, ULONG nCyclesLeft)
{
  address &= 0xFF;
  if (address == 0x7F)
    return MemReadFloatingBus(SW_DHIRES != 0, nCyclesLeft);
  else {
    BOOL result = 0;
    switch (address) {
      case 0x1A: result = SW_TEXT;    break;
      case 0x1B: result = SW_MIXED;   break;
      case 0x1D: result = SW_HIRES;   break;
      case 0x1E: result = g_nAltCharSetOffset;   break;
      case 0x1F: result = SW_80COL;   break;
      case 0x7F: result = SW_DHIRES;  break;
    }
    return KeybGetKeycode() | (result ? 0x80 : 0);
  }
}

//===========================================================================
void VideoCheckPage (BOOL force) {
  if ((displaypage2 != (SW_PAGE2 != 0)) &&
      (force || (emulmsec-lastpageflip > 500))) {
    displaypage2 = (SW_PAGE2 != 0);
    VideoRefreshScreen();
    hasrefreshed = 1;
    lastpageflip = emulmsec;
  }
}

//===========================================================================
BYTE /*__stdcall*/ VideoCheckVbl (WORD, WORD, BYTE, BYTE, ULONG nCyclesLeft)
{
	/*
		// Drol expects = 80
		68DE A5 02    LDX #02
		68E0 AD 50 C0 LDA TXTCLR
		68E3 C9 80    CMP #80
		68E5 D0 F7    BNE $68DE

		6957 A5 02    LDX #02
		6959 AD 50 C0 LDA TXTCLR
		695C C9 80    CMP #80
		695E D0 F7    BNE $68DE

		69D3 A5 02    LDX #02
		69D5 AD 50 C0 LDA TXTCLR
		69D8 C9 80    CMP #80
		69DA D0 F7    BNE $68DE

		// Karateka expects < 80
		07DE AD 19 C0 LDA RDVBLBAR
		07E1 30 FB    BMI $7DE

		77A1 AD 19 C0 LDA RDVBLBAR
		77A4 30 FB    BMI $7DE

		// Gumball expects non-zero low-nibble on VBL
		BBB5 A5 60    LDA $60
		BBB7 4D 50 C0 EOR TXTCLR
		BBBA 85 60    STA $60
		BBBC 29 0F    AND #$0F
		BBBE F0 F5    BEQ $BBB5
		BBC0 C9 0F    CMP #$0F
		BBC2 F0 F1    BEQ $BBB5

//		return MemReturnRandomData(dwVBlCounter <= nVBlStop_NTSC);
	if (dwVBlCounter <= nVBlStop_NTSC)
		return (BYTE)(dwVBlCounter & 0x7F); // 0x00;
	else
		return 0x80 | ((BYTE)(dwVBlCounter & 1));
	*/

	bool bVblBar;
	VideoGetScannerAddress(&bVblBar, nCyclesLeft);
    BYTE r = KeybGetKeycode();
    return (r & ~0x80) | ((bVblBar) ? 0x80 : 0);
 }

//===========================================================================
void VideoChooseColor () { // will implement later. May be...???? ^_^
//   CHOOSECOLOR cc;
//   ZeroMemory(&cc,sizeof(CHOOSECOLOR));
//   cc.lStructSize     = sizeof(CHOOSECOLOR);
//   cc.hwndOwner       = g_hFrameWindow;
//   cc.rgbResult       = monochrome;
//   cc.lpCustColors    = customcolors;
//   cc.Flags           = CC_RGBINIT | CC_SOLIDCOLOR;
//   if (ChooseColor(&cc)) {
//     monochrome = cc.rgbResult;
//     VideoReinitialize();
//     if ((g_nAppMode != MODE_LOGO) && (g_nAppMode != MODE_DEBUG))
//       VideoRedrawScreen();
//     RegSaveValue(TEXT("Configuration"),TEXT("Monochrome Color"),1,monochrome);
//   }
}

//===========================================================================
void VideoDestroy () {
// Just free our SDL surfaces and free vidlastmem
  // DESTROY BUFFERS
  //VirtualFree(framebufferinfo,0,MEM_RELEASE);
//  VirtualFree(g_pSourceHeader     ,0,MEM_RELEASE);
  VirtualFree(vidlastmem, 0, MEM_RELEASE);
/*  framebufferinfo = NULL;
  g_pSourceHeader      = NULL;*/
  vidlastmem      = NULL;
  // DESTROY FRAME BUFFER
//  DeleteDC(g_hDeviceDC);
  if(g_hDeviceBitmap) SDL_FreeSurface(g_hDeviceBitmap);
  g_hDeviceBitmap = NULL;
//   g_hDeviceDC     = (HDC)0;
//   g_hDeviceBitmap = (HBITMAP)0;

  // DESTROY SOURCE IMAGE
  if(g_hSourceBitmap) SDL_FreeSurface(g_hSourceBitmap);
  g_hSourceBitmap = NULL;
//  g_hSourceBitmap = (HBITMAP)0;

  // DESTROY LOGO
  if (g_hLogoBitmap)  SDL_FreeSurface(g_hLogoBitmap);
  g_hLogoBitmap = NULL;
//    g_hLogoBitmap = (HBITMAP)0;

  if(charset40) SDL_FreeSurface(charset40);
  charset40 = NULL;
  // DESTROY PALETTE
/*  if (g_hPalette) {
    DeleteObject(g_hPalette);
    g_hPalette = (HPALETTE)0;
  }*/
}

//===========================================================================
//void VideoDrawLogoBitmap (/* HDC hDstDC*/ )
//{
//	HDC memdc = CreateCompatibleDC(framedc);
//	SelectObject(memdc,g_hLogoBitmap);
//	BitBlt(framedc,0,0,560,384,memdc,0,0,SRCCOPY);
//	DeleteDC(memdc);
/*	HDC hSrcDC = CreateCompatibleDC( hDstDC );
	SelectObject( hSrcDC, g_hLogoBitmap );
	BitBlt(
		hDstDC,   // hdcDest
		0, 0,     // nXDest, nYDest
		560, 384, // nWidth, nHeight // HACK: HARD-CODED
		hSrcDC,   // hdcSrc
		0, 0,     // nXSrc, nYSrc
		SRCCOPY   // dwRop
	);

	DeleteObject( hSrcDC );
	hSrcDC = NULL;*/
//}

//===========================================================================
void VideoDisplayLogo () {
	if(!g_hLogoBitmap) return; // nothing to display?
	if(screen->format->palette && g_hLogoBitmap->format->palette)
		SDL_SetColors(screen, g_hLogoBitmap->format->palette->colors,
			      0, g_hLogoBitmap->format->palette->ncolors);
	SDL_BlitSurface(g_hLogoBitmap, NULL, screen, NULL);
	SDL_Flip(screen);


// 	HDC hFrameDC = FrameGetDC();
//
// 	// DRAW THE LOGO
// 	HBRUSH brush = CreateSolidBrush(PALETTERGB(0x70,0x30,0xE0));
// 	if (g_hLogoBitmap)
// 	{
// 		VideoDrawLogoBitmap( hFrameDC );
// 	}
// 	else
// 	{
// 		SelectObject(hFrameDC,brush);
// 		SelectObject(hFrameDC,GetStockObject(NULL_PEN));
// 		Rectangle(hFrameDC,0,0,560+1,384+1);
// 	}
//
// 	// DRAW THE VERSION NUMBER
// 	HFONT font = CreateFont(-20,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,
// 							OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,
// 							VARIABLE_PITCH | 4 | FF_SWISS,
// 							TEXT("Arial"));
// 	SelectObject(hFrameDC,font);
// 	SetTextAlign(hFrameDC,TA_RIGHT | TA_TOP);
// 	SetBkMode(hFrameDC,TRANSPARENT);
//
// 	#define VERSION_TXT "Version "
// 	char* szVersion = new char[strlen(VERSION_TXT) + strlen(VERSIONSTRING) + 1];
// 	strcpy(&szVersion[0], VERSION_TXT);
// 	strcpy(&szVersion[strlen(VERSION_TXT)], VERSIONSTRING);
// 	szVersion[strlen(szVersion)] = 0x00;
//
// 	#define  DRAWVERSION(x,y,c)  SetTextColor(hFrameDC,c);
// 								TextOut(hFrameDC,
// 										540+x,358+y,
// 										szVersion,
// 										strlen(szVersion));
//
// 	if (GetDeviceCaps(hFrameDC,PLANES) * GetDeviceCaps(hFrameDC,BITSPIXEL) <= 4) {
// 	DRAWVERSION( 2, 2,RGB(0x00,0x00,0x00));
// 	DRAWVERSION( 1, 1,RGB(0x00,0x00,0x00));
// 	DRAWVERSION( 0, 0,RGB(0xFF,0x00,0xFF));
// 	}
// 	else {
// 	DRAWVERSION( 1, 1,PALETTERGB(0x30,0x30,0x70));
// 	DRAWVERSION(-1,-1,PALETTERGB(0xC0,0x70,0xE0));
// 	DRAWVERSION( 0, 0,PALETTERGB(0x70,0x30,0xE0));
// 	}
//
// 	delete [] szVersion;
// #undef  DRAWVERSION
//
// 	FrameReleaseDC();
// 	DeleteObject(brush);
// 	DeleteObject(font);
}

//===========================================================================
BOOL VideoHasRefreshed () {
  BOOL result = hasrefreshed;
  hasrefreshed = 0;
  return result;
}

//===========================================================================
void VideoInitialize () {
	SDL_Surface * tmp_surface;
  // CREATE A BUFFER FOR AN IMAGE OF THE LAST DRAWN MEMORY
  vidlastmem = (LPBYTE)VirtualAlloc(NULL,0x10000,MEM_COMMIT,PAGE_READWRITE);
  ZeroMemory(vidlastmem,0x10000);

  // LOAD THE LOGO
  tmp_surface = SDL_LoadBMP("/apps/wiiapple/applelin.bmp");
  if (tmp_surface != NULL)  g_hLogoBitmap = SDL_DisplayFormat(tmp_surface);
  	else fprintf(stderr, "Video: applelin.bmp was not loaded\n");
  SDL_FreeSurface(tmp_surface);

  // LOAD APPLE CHARSET40
  tmp_surface = SDL_LoadBMP("/apps/wiiapple/charset40.bmp");
  if(tmp_surface != NULL)  charset40 = SDL_DisplayFormat(tmp_surface);
  	else fprintf(stderr, "Video: Apple text is not unavailable: charset40.bmp was not loaded\n");
  SDL_FreeSurface(tmp_surface);
  // CREATE A BITMAPINFO STRUCTURE FOR THE FRAME BUFFER
//   framebufferinfo = (LPBITMAPINFO)VirtualAlloc(NULL,
//                                                sizeof(BITMAPINFOHEADER)
//                                                  +256*sizeof(RGBQUAD),
//                                                MEM_COMMIT,
//                                                PAGE_READWRITE);
//   ZeroMemory(framebufferinfo,sizeof(BITMAPINFOHEADER)+256*sizeof(RGBQUAD));
//   framebufferinfo->bmiHeader.biSize     = sizeof(BITMAPINFOHEADER);
//   framebufferinfo->bmiHeader.biWidth    = 560;
//   framebufferinfo->bmiHeader.biHeight   = 384;
//   framebufferinfo->bmiHeader.biPlanes   = 1;
//   framebufferinfo->bmiHeader.biBitCount = 8;
//   framebufferinfo->bmiHeader.biClrUsed  = 256;
//
//   // CREATE A BITMAPINFO STRUCTURE FOR THE SOURCE IMAGE
//   g_pSourceHeader = (LPBITMAPINFO)VirtualAlloc(NULL,
//                                           sizeof(BITMAPINFOHEADER)
//                                             +256*sizeof(RGBQUAD),
//                                           MEM_COMMIT,
//                                           PAGE_READWRITE);
//   ZeroMemory(g_pSourceHeader,sizeof(BITMAPINFOHEADER));
//   g_pSourceHeader->bmiHeader.biSize     = sizeof(BITMAPINFOHEADER);
//   g_pSourceHeader->bmiHeader.biWidth    = SRCOFFS_TOTAL;
//   g_pSourceHeader->bmiHeader.biHeight   = 512;
//   g_pSourceHeader->bmiHeader.biPlanes   = 1;
//   g_pSourceHeader->bmiHeader.biBitCount = 8;
//   g_pSourceHeader->bmiHeader.biClrUsed  = 256;

  // CREATE AN IDENTITY PALETTE AND FILL IN THE CORRESPONDING COLORS IN
  // THE BITMAPINFO STRUCTURE
  CreateIdentityPalette();

  // PREFILL THE 16 CUSTOM COLORS AND MAKE SURE TO INCLUDE THE CURRENT MONOCHROME COLOR
  for (int index = DARK_RED; index <= NUM_COLOR_PALETTE; index++)
    customcolors[index-DARK_RED] = RGB(framebufferinfo[index].r,
                                       framebufferinfo[index].g,
                                       framebufferinfo[index].b);
// bmiColors
  // CREATE THE FRAME BUFFER DIB SECTION AND DEVICE CONTEXT,
  // CREATE THE SOURCE IMAGE DIB SECTION AND DRAW INTO THE SOURCE BIT BUFFER
  CreateDIBSections();

  // RESET THE VIDEO MODE SWITCHES AND THE CHARACTER SET OFFSET
  VideoResetState();
}

//===========================================================================
void VideoRealizePalette (/*HDC dc*/) {
//   if (g_hPalette) {
//     SelectPalette(dc,g_hPalette,0);
//     RealizePalette(dc);
//   }
}

//===========================================================================
void VideoRedrawScreen () {
  redrawfull = 1;
  VideoRefreshScreen();
}

//===========================================================================
void VideoRefreshScreen () {
  LPBYTE addr    = framebufferbits;
  LONG   pitch   = 560; // pitch stands for pixels in a row, if one pixel stands for one byte (560 in our case)
// I could take pitch such: LONG pitch = screen->pitch; . May be it would be better, what'd you think? --bb
  //  HDC    framedc = FrameGetVideoDC(&addr,&pitch);
  CreateFrameOffsetTable(addr,pitch);

  int src_locked = 0;
  if ( SDL_MUSTLOCK(g_hSourceBitmap) ) {
	  SDL_LockSurface(g_hSourceBitmap);
	  src_locked = 1; // the source bitmap is locked
  }
  int frm_locked = 0;
  if ( SDL_MUSTLOCK(g_hDeviceBitmap) ) {
	  SDL_LockSurface(g_hDeviceBitmap);
	  frm_locked = 1; // the frame bitmap is locked
  }


  // CHECK EACH CELL FOR CHANGED BYTES.  REDRAW PIXELS FOR THE CHANGED BYTES
  // IN THE FRAME BUFFER.  MARK CELLS IN WHICH REDRAWING HAS TAKEN PLACE AS
  // DIRTY.
  g_pHiresBank1  = MemGetAuxPtr (0x2000 << displaypage2);
  g_pHiresBank0 = MemGetMainPtr(0x2000 << displaypage2);
  g_pTextBank1   = MemGetAuxPtr (0x400  << displaypage2);
  g_pTextBank0  = MemGetMainPtr(0x400  << displaypage2);
  ZeroMemory(celldirty,40*32);
  UpdateFunc_t update = SW_TEXT
	? SW_80COL
		? Update80ColCell
		: Update40ColCell
	: SW_HIRES
		? (SW_DHIRES && SW_80COL)
			? UpdateDHiResCell
			: UpdateHiResCell
		: (SW_DHIRES && SW_80COL)
			? UpdateDLoResCell
			: UpdateLoResCell;

  BOOL anydirty = 0;
  int  y        = 0;
  int  ypixel   = 0;
  while (y < 20) {
    int offset = ((y & 7) << 7) + ((y >> 3) * 40);
    int x      = 0;
    int xpixel = 0;
    while (x < 40) {
      anydirty |= celldirty[x][y] = update(x,y,xpixel,ypixel,offset+x);
      ++x;
      xpixel += 14;
    }
    ++y;
    ypixel += 16;
  }

  if (SW_MIXED)
    update = SW_80COL ? Update80ColCell
                      : Update40ColCell;

  while (y < 24) {
    int offset = ((y & 7) << 7) + ((y >> 3) * 40);
    int x      = 0;
    int xpixel = 0;
    while (x < 40) {
      anydirty |= celldirty[x][y] = update(x,y,xpixel,ypixel,offset+x);
      ++x;
      xpixel += 14;
    }
    ++y;
    ypixel += 16;
  }

  if(frm_locked) SDL_UnlockSurface(g_hDeviceBitmap);
  if(src_locked) SDL_UnlockSurface(g_hSourceBitmap);
  // Clear this flag after TEXT screen has been updated
  g_bTextFlashFlag = false;

 // try 1 change to 0 and see strange results.... 0_0   --bb
#if 1
  // New simplified code:
  // . Oliver Schmidt gets a flickering mouse cursor with this code
  if (/*framedc &&*/ anydirty)
  {
	  SDL_BlitSurface(g_hDeviceBitmap, NULL, screen, NULL);
	  SDL_Flip(screen);	// flip SDL buffers

//	BitBlt(framedc,0,0,560,384,g_hDeviceDC,0,0,SRCCOPY);
//	GdiFlush();
  }
#else

  // Original code:
  if (!anydirty)
  {
//	FrameReleaseVideoDC();
	SetLastDrawnImage();
	redrawfull = 0;
	return;
  }
	SDL_Rect scrrect, frrect;	// rects for blitting

  // COPY DIRTY CELLS FROM THE DEVICE DEPENDENT BITMAP ONTO THE SCREEN
  // IN LONG HORIZONTAL RECTANGLES
  BOOL remainingdirty = 0;
  y                   = 0;
  ypixel              = 0;
  while (y < 24) {
	int start  = -1;
	int startx = 0;
	int x      = 0;
	int xpixel = 0;
	while (x < 40) {
	  if ((x == 39) && celldirty[x][y])
		if (start >= 0) {
		  xpixel += 14;
		  celldirty[x][y] = 0;
		}
		else
		  remainingdirty = 1;
	  if ((start >= 0) && !celldirty[x][y]) {
		if ((x - startx > 1) || ((x == 39) && (xpixel == 560))) {
		  int height = 1;
		  while ((y+height < 24)
				   && celldirty[startx][y+height]
				   && celldirty[x-1][y+height]
				   && celldirty[(startx+x-1) >> 1][y+height])
			height++;

		  scrrect.w = xpixel - start;
		  scrrect.h = height << 4;
		  scrrect.x = start;
		  scrrect.y = ypixel;

		  frrect.x = start;
		  frrect.y = ypixel;
		  frrect.w = scrrect.w;
		  frrect.h = scrrect.h;
		  SDL_SoftStretch(g_hDeviceBitmap, &frrect, screen, &scrrect);
/*		  BitBlt(framedc,start,ypixel,xpixel-start,height << 4,
				 g_hDeviceDC,start,ypixel,SRCCOPY);*/
		  while (height--) {
			int loop = startx;
			while (loop < x+(xpixel == 560))
			  celldirty[loop++][y+height] = 0;
		  }
		  start = -1;
		}
		else
		  remainingdirty = 1;
		start = -1;
	  }
	  else if ((start == -1) && celldirty[x][y] && (x < 39)) {
		start  = xpixel;
		startx = x;
	  }
	  x++;
	  xpixel += 14;
	}
	y++;
	ypixel += 16;
  }

  // COPY ANY REMAINING DIRTY CELLS FROM THE DEVICE DEPENDENT BITMAP
  // ONTO THE SCREEN IN VERTICAL RECTANGLES
  if (remainingdirty) {
	int x      = 0;
	int xpixel = 0;
	while (x < 40) {
	  int start  = -1;
	  int y      = 0;
	  int ypixel = 0;
	  while (y < 24) {
		if ((y == 23) && celldirty[x][y]) {
		  if (start == -1)
			start = ypixel;
		  ypixel += 16;
		  celldirty[x][y] = 0;
		}
		if ((start >= 0) && !celldirty[x][y]) {
			scrrect.w = 14;
			scrrect.h = ypixel - start;
			scrrect.x = xpixel;
			scrrect.y = start;

			frrect.x = xpixel;
			frrect.y = start;
			frrect.w = scrrect.w;
			frrect.h = scrrect.h;
			SDL_SoftStretch(g_hDeviceBitmap, &frrect, screen, &scrrect);

/*		  BitBlt(framedc,xpixel,start,14,ypixel-start,
				 g_hDeviceDC,xpixel,start,SRCCOPY);*/
		  start = -1;
		}
		else if ((start == -1) && celldirty[x][y])
		  start = ypixel;
		y++;
		ypixel += 16;
	  }
	  x++;
	  xpixel += 14;
	}
  }

  SDL_Flip(screen);	// flip SDL buffers
//  GdiFlush();
#endif

//  FrameReleaseVideoDC();
  SetLastDrawnImage();
  redrawfull = 0;
}

//===========================================================================
void VideoReinitialize () {
  CreateIdentityPalette();
  CreateDIBSections();
}

//===========================================================================
void VideoResetState () {
  g_nAltCharSetOffset     = 0;
  displaypage2 = 0;
  vidmode      = VF_TEXT;
  redrawfull   = 1;
}

//===========================================================================
BYTE /*__stdcall*/ VideoSetMode (WORD, WORD address, BYTE write, BYTE, ULONG nCyclesLeft)
{
  address &= 0xFF;
  DWORD oldpage2 = SW_PAGE2;
  int   oldvalue = g_nAltCharSetOffset+(int)(vidmode & ~(VF_MASK2 | VF_PAGE2));
  switch (address) {
    case 0x00: vidmode &= ~VF_MASK2;   break;
    case 0x01: vidmode |=  VF_MASK2;   break;
    case 0x0C: if (!IS_APPLE2) vidmode &= ~VF_80COL;   break;
    case 0x0D: if (!IS_APPLE2) vidmode |=  VF_80COL;   break;
    case 0x0E: if (!IS_APPLE2) g_nAltCharSetOffset = 0;           break;	// Alternate char set off
    case 0x0F: if (!IS_APPLE2) g_nAltCharSetOffset = 256;         break;	// Alternate char set on
    case 0x50: vidmode &= ~VF_TEXT;    break;
    case 0x51: vidmode |=  VF_TEXT;    break;
    case 0x52: vidmode &= ~VF_MIXED;   break;
    case 0x53: vidmode |=  VF_MIXED;   break;
    case 0x54: vidmode &= ~VF_PAGE2;   break;
    case 0x55: vidmode |=  VF_PAGE2;   break;
    case 0x56: vidmode &= ~VF_HIRES;   break;
    case 0x57: vidmode |=  VF_HIRES;   break;
    case 0x5E: if (!IS_APPLE2) vidmode |=  VF_DHIRES;  break;
    case 0x5F: if (!IS_APPLE2) vidmode &= ~VF_DHIRES;  break;
  }
  if (SW_MASK2)
    vidmode &= ~VF_PAGE2;
  if (oldvalue != g_nAltCharSetOffset+(int)(vidmode & ~(VF_MASK2 | VF_PAGE2))) {
    graphicsmode = !SW_TEXT;
    redrawfull   = 1;
  }
  if (g_bFullSpeed && oldpage2 && !SW_PAGE2) {
    static DWORD lasttime = 0;
    DWORD currtime = GetTickCount();
    if (currtime-lasttime >= 20)
      lasttime = currtime;
    else
      oldpage2 = SW_PAGE2;
  }
  if (oldpage2 != SW_PAGE2) {
    static DWORD lastrefresh = 0;
    if ((displaypage2 && !SW_PAGE2) || (!behind)) {
      displaypage2 = (SW_PAGE2 != 0);
      if (!redrawfull) {
        VideoRefreshScreen();
        hasrefreshed = 1;
        lastrefresh  = emulmsec;
      }
    }
    else if ((!SW_PAGE2) && (!redrawfull) && (emulmsec-lastrefresh >= 20)) {
      displaypage2 = 0;
      VideoRefreshScreen();
      hasrefreshed = 1;
      lastrefresh  = emulmsec;
    }
    lastpageflip = emulmsec;
  }
  return MemReadFloatingBus(nCyclesLeft);
}

//===========================================================================

void VideoUpdateVbl (DWORD dwCyclesThisFrame)
{
	dwVBlCounter = (DWORD) ((double)dwCyclesThisFrame / (double)uCyclesPerLine);
}

//===========================================================================

// Called at 60Hz (every 16.666ms)
void VideoUpdateFlash()
{
	static UINT nTextFlashCnt = 0;

	nTextFlashCnt++;

	if(nTextFlashCnt == 60/6)	// Flash rate = 6Hz (every 166ms)
	{
		nTextFlashCnt = 0;
		g_bTextFlashState = !g_bTextFlashState;

		// Redraw any FLASHing chars if any text showing. NB. No FLASH g_nAppMode for 80 cols
		if ((SW_TEXT || SW_MIXED) ) // && !SW_80COL) // FIX: FLASH 80-Column
			g_bTextFlashFlag = true;
	}
}

//===========================================================================

bool VideoGetSW80COL()
{
	return SW_80COL ? true : false;
}

//===========================================================================

DWORD VideoGetSnapshot(SS_IO_Video* pSS)
{
	pSS->bAltCharSet = !(g_nAltCharSetOffset == 0);
	pSS->dwVidMode = vidmode;
	return 0;
}

//===========================================================================

DWORD VideoSetSnapshot(SS_IO_Video* pSS)
{
	g_nAltCharSetOffset = !pSS->bAltCharSet ? 0 : 256;
	vidmode = pSS->dwVidMode;

	//

	graphicsmode = !SW_TEXT;
    displaypage2 = (SW_PAGE2 != 0);

	return 0;
}

//===========================================================================

WORD VideoGetScannerAddress(bool* pbVblBar_OUT, const DWORD uExecutedCycles)
{
    // get video scanner position
    //
    int nCycles = CpuGetCyclesThisFrame(uExecutedCycles);

    // machine state switches
    //
	int nHires    = (SW_HIRES & !SW_TEXT) ? 1 : 0;
    int nPage2    = (SW_PAGE2) ? 1 : 0;
    int n80Store = (MemGet80Store()) ? 1 : 0;

    // calculate video parameters according to display standard
    //
    int nScanLines  = bVideoScannerNTSC ? kNTSCScanLines : kPALScanLines;
//    int nVSyncLine  = bVideoScannerNTSC ? kNTSCVSyncLine : kPALVSyncLine; -- not used??
//    int nScanCycles = nScanLines * kHClocks;					-- not used???

    // calculate horizontal scanning state
    //
    int nHClock = (nCycles + kHPEClock) % kHClocks; // which horizontal scanning clock
    int nHState = kHClock0State + nHClock; // H state bits
    if (nHClock >= kHPresetClock) // check for horizontal preset
    {
        nHState -= 1; // correct for state preset (two 0 states)
    }
    int h_0 = (nHState >> 0) & 1; // get horizontal state bits
    int h_1 = (nHState >> 1) & 1;
    int h_2 = (nHState >> 2) & 1;
    int h_3 = (nHState >> 3) & 1;
    int h_4 = (nHState >> 4) & 1;
    int h_5 = (nHState >> 5) & 1;

    // calculate vertical scanning state
    //
    int nVLine  = nCycles / kHClocks; // which vertical scanning line
    int nVState = kVLine0State + nVLine; // V state bits
    if ((nVLine >= kVPresetLine)) // check for previous vertical state preset
    {
        nVState -= nScanLines; // compensate for preset
    }
    int v_A = (nVState >> 0) & 1; // get vertical state bits
    int v_B = (nVState >> 1) & 1;
    int v_C = (nVState >> 2) & 1;
    int v_0 = (nVState >> 3) & 1;
    int v_1 = (nVState >> 4) & 1;
    int v_2 = (nVState >> 5) & 1;
    int v_3 = (nVState >> 6) & 1;
    int v_4 = (nVState >> 7) & 1;
//    int v_5 = (nVState >> 8) & 1;	--- not used?

    // calculate scanning memory address
    //
    if (SW_HIRES && SW_MIXED && (v_4 & v_2))	// NICK: Should this be (SW_HIRES && !SW_TEXT) instead of just 'SW_HIRES' ?
    {
        nHires = 0; // (address is in text memory)
    }

    int nAddend0 = 0x68; // 1            1            0            1
    int nAddend1 =              (h_5 << 5) | (h_4 << 4) | (h_3 << 3);
    int nAddend2 = (v_4 << 6) | (v_3 << 5) | (v_4 << 4) | (v_3 << 3);
    int nSum     = (nAddend0 + nAddend1 + nAddend2) & (0x0F << 3);

    int nAddress = 0;
    nAddress |= h_0 << 0; // a0
    nAddress |= h_1 << 1; // a1
    nAddress |= h_2 << 2; // a2
    nAddress |= nSum;     // a3 - aa6
    nAddress |= v_0 << 7; // a7
    nAddress |= v_1 << 8; // a8
    nAddress |= v_2 << 9; // a9
    nAddress |= ((nHires) ? v_A : (1 ^ (nPage2 & (1 ^ n80Store)))) << 10; // a10
    nAddress |= ((nHires) ? v_B : (nPage2 & (1 ^ n80Store))) << 11; // a11
    if (nHires) // hires?
    {
        // Y: insert hires only address bits
        //
        nAddress |= v_C << 12; // a12
        nAddress |= (1 ^ (nPage2 & (1 ^ n80Store))) << 13; // a13
        nAddress |= (nPage2 & (1 ^ n80Store)) << 14; // a14
    }
    else
    {
        // N: text, so no higher address bits unless Apple ][, not Apple //e
        //
        if ((IS_APPLE2) && // Apple ][?
            (kHPEClock <= nHClock) && // Y: HBL?
            (nHClock <= (kHClocks - 1)))
        {
            nAddress |= 1 << 12; // Y: a12 (add $1000 to address!)
        }
    }

    // update VBL' state
    //
	if (pbVblBar_OUT != NULL)
	{
		if (v_4 & v_3) // VBL?
		{
			*pbVblBar_OUT = false; // Y: VBL' is false
		}
		else
		{
			*pbVblBar_OUT = true; // N: VBL' is true
		}
	}
    return static_cast<WORD>(nAddress);
}

//===========================================================================

// Derived from VideoGetScannerAddress()
bool VideoGetVbl(const DWORD uExecutedCycles)
{
    // get video scanner position
    //
    int nCycles = CpuGetCyclesThisFrame(uExecutedCycles);

    // calculate video parameters according to display standard
    //
    int nScanLines  = bVideoScannerNTSC ? kNTSCScanLines : kPALScanLines;

    // calculate vertical scanning state
    //
    int nVLine  = nCycles / kHClocks; // which vertical scanning line
    int nVState = kVLine0State + nVLine; // V state bits
    if ((nVLine >= kVPresetLine)) // check for previous vertical state preset
    {
        nVState -= nScanLines; // compensate for preset
    }
    int v_3 = (nVState >> 6) & 1;
    int v_4 = (nVState >> 7) & 1;

    // update VBL' state
    //
	if (v_4 & v_3) // VBL?
	{
		return false; // Y: VBL' is false
	}
	else
	{
		return true; // N: VBL' is true
	}
}

//===========================================================================
