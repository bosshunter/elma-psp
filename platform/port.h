// port.h - PSP port glue shared between the game code and the platform layer.
// (port) Part of the PSP adaptation of the Elasto Mania source code.
#ifndef ELMA_PORT_H
#define ELMA_PORT_H

// Gameplay screen. It is drawn at 1.25x the PSP screen and scaled down to
// 480x272, so the view is a bit zoomed out (the bike takes less of the screen).
// For another zoom keep the 480:272 ratio (and at most 640x480):
#define PORT_FIZX (752)
#define PORT_FIZY (426)

// In-game zoom (Zoom In / Zoom Out controls) changes the size the gameplay is
// drawn at; zoom 1.0 is PORT_FIZX x PORT_FIZY. Zooming out draws a bigger
// picture (slower), up to this size; zooming in a smaller one:
#define PORT_JATEKMAXX (848) // = PORT_MENUX, the width of the screen buffers
#define PORT_JATEKMAXY (480) // row capacity
// The current gameplay size (set by the zoom, kiraj320.cpp):
extern int Port_jatekx, Port_jateky;

// Menu screen: the full width of the PSP screen at the classic 480 lines
// (480x272 scaled up to 480 lines, rounded to 16). It is scaled down to
// 480x272; the classic 640x480 menu layout sits in the middle:
#define PORT_MENUX (848)
#define PORT_MENUY (480)

// The classic 640x480 screen (the layout of the menus and the editor):
#define PORT_KLASSZX (640)
#define PORT_KLASSZY (480)

// Level editor screen. Normally the classic 640x480, stretched to fill the
// PSP screen. Built with PORT_SZELESEDITOR (cmake -DELMA_WIDE_EDITOR=ON) it is
// 848x480, the PSP's own shape, so nothing is stretched and the level view is
// wider. Either way L shows it 1:1 (scrolling with the cursor):
#ifdef PORT_SZELESEDITOR
#define PORT_EDX (PORT_MENUX)
#else
#define PORT_EDX (PORT_KLASSZX)
#endif
#define PORT_EDY (480)
// The editor's dialogs keep their 640-wide layout, moved to the middle:
#define PORT_EDOFSX ((PORT_EDX-PORT_KLASSZX)/2)

// Widest screen row the game draws in gameplay or the editor (textures are
// widened to at least this):
#define PORT_MAXSZEL ((PORT_JATEKMAXX > PORT_EDX) ? PORT_JATEKMAXX : PORT_EDX)

// Key codes of the D-Pad Up / Down buttons in gameplay. They are not keyboard
// codes (V and T were used before, then 0x3B / 0x3C, which are F1 / F2 on the
// PC), so old saved bindings can be recognised and moved to the new defaults:
// Up = timer, Down = minimap.
#define PORT_KOD_DFEL (0xF0)
#define PORT_KOD_DLE  (0xF1)
// Fixes the bindings of a player loaded from state.dat (psp_platform.cpp):
struct jatekosopciok;
void port_billkodjavit( jatekosopciok* popc );

// Size of the screen the game is currently drawing into:
// PORT_FIZX x PORT_FIZY while a level is being played or replayed, 640x480 in
// the level editor, PORT_MENUX x PORT_MENUY in the menus.
extern int Port_kepx, Port_kepy;
// Sets Port_kepx/Port_kepy for the current mode:
void port_kepmeretfrissit( void );

// Nonzero while one of the level editor's file lists is open: the D-Pad moves
// through the list (with auto-repeat), the analog stick moves the cursor,
// Cross clicks and Start selects the highlighted entry.
extern int Port_listaismetles;
// Nonzero if the last character returned by mk_getextchar() was an
// auto-repeat of a held direction (lists wrap around only on a real press):
int mk_utolsoismetelt( void );

// Nonzero while gameplay is running (480x272 view and gameplay buttons):
extern int Port_szeles;
// Nonzero while gameplay button mapping is active (gameplay, or when the
// "Customize controls" screen is waiting for a button):
extern int Port_jatekgombok;

// Put one of these at the top of a function to switch modes for its duration:
struct port_jatek_guard {
	int regiszeles, regigombok;
	port_jatek_guard( void );
	~port_jatek_guard( void );
};
struct port_gombfelvetel_guard {
	int regigombok;
	port_gombfelvetel_guard( void );
	~port_gombfelvetel_guard( void );
};

// Opens the PSP on-screen keyboard and feeds the result into the game's
// keyboard character queue, so the game's own text-entry loop processes it:
//   OK     -> 'torlesszam' x 'torlokod' (to clear the old text), the typed
//             characters, then MK_ENTER (unless enterrel is 0)
//   Cancel -> MK_ESC
// csakszam: numeric keyboard. maxhossz: maximum number of characters.
void port_ime_bevitel( const char* cim, const char* kezdo, int maxhossz,
					   int csakszam, int torlesszam, int torlokod,
					   int enterrel = 1 );

// Timing overlay (build with -DELMA_PROFILE=ON): the game marks the physics
// part of each frame, the platform measures the rest:
#ifdef PORT_PROFIL
void port_fizkezd( void );
void port_fizvege( void );
// Parts of the gameplay drawing: 0 background, 1 foreground, 2 bikes,
// 3 objects, 4 minimap, 5 timer
void port_reszkezd( void );
void port_reszvege( int resz );
// Pixel loops of the bike drawing (inside part 2):
void port_pixkezd( void );
void port_pixvege( void );
#else
#define port_pixkezd()
#define port_pixvege()
#define port_fizkezd()
#define port_fizvege()
#define port_reszkezd()
#define port_reszvege( resz )
#endif

// GPU menus (menukep.cpp): instead of composing each frame on the CPU, the
// menu hands the platform its two background pictures (plain and the "ball"
// version, both PORT_MENUX x PORT_MENUY), the discs to cut out of them and the
// helmet sprite; the GPU draws them all. 'verzio' changes whenever the
// pictures were redrawn, so they are copied to the GPU only then.
class pic8;
struct port_korong { float x, y, r; int golyobol; };
#define PORT_MAXKORONG (96)
void port_gpumenu( pic8* sima, pic8* golyo, int verzio,
				   int db, const port_korong* korongok,
				   pic8* sisak, int sisakx, int sisaky );
// The menu intro animation (menukep.cpp), for the next port_gpumenu frame:
// the background's top is at row 'elt' (above it: colour hatterszin); the
// text picture (szovegatl: its transparent colour; made again when
// szovegverzio changes) is moved up by elt; the title picture at x, y.
void port_gpuintro( int elt, int hatterszin, pic8* szoveg, int szovegatl, int szovegverzio,
					pic8* intro, int introx, int introy );
void port_gpuintro_vege( void ); // frees the intro's pictures
// The memory of the two menu pictures (PORT_MENUX x PORT_MENUY, rows one after
// the other), which the GPU draws from: 0 plain, 1 with the balls' picture.
unsigned char* port_menukep( int i );
void port_gpuvar( void ); // waits until the GPU has finished the frame it is drawing

// GPU gameplay drawing (kiraj320.cpp, doboz.cpp, ecset.cpp). The CPU draws
// the picture as the original did, except for the bikes, which the GPU draws
// on top of it; a mask (set around the bikes) keeps the foreground, the
// minimap and the timer in front of them. Coordinates are screen-buffer
// pixels (x right, y down). For each view, in this order:
//   port_gpu_nezet, port_gpu_motor + port_gpu_negyszog (the parts of each
//   bike), port_gpu_maszk,
//   then port_gpu_jelol / port_gpu_jelolteglalap for what is in front.
// Nonzero if the GPU draws the bikes (not with cpurajz.inf):
int  port_gpujatek( void );
// Nonzero if cpurajz.inf is next to EBOOT.PBP: everything is drawn by the
// CPU, as in v1.0:
int  port_cpurajz( void );
void port_gpu_nezet( int x1, int y1, int x2, int y2 );   // following items are clipped to this view
void port_gpu_negyszog( const float* xy, const float* uv, // 4 corners: r, r+u, r+v, r+u+v
						unsigned char* tex, int xsize, int ysize, // 256-wide 8-bit picture
						unsigned char lyuk );                     // its transparent colour
// Changes whenever a bike picture (kiskep) is deleted: the GPU's copies of
// them are made again (a new picture may get the address of an old one):
extern int Port_kiskepverzio;
// Makes the GPU's copies of a bike picture in advance (when the LGR file is
// read), for the palette 'paletta' (256 colours as ddpal keeps them):
void port_gpukep_elore( unsigned char* tex, int xsize, int ysize, unsigned char lyuk,
						const void* paletta ); // 256-wide texture
void port_gpu_motor( void );            // the parts that follow are another bike
void port_gpu_maszk( void );            // the bikes of the view are done: clears the mask around them
int  port_gpu_maszksor( int y );        // nonzero if row y has some of the mask
void port_gpu_jelol( int x, int y, int n ); // n pixels of row y are in front of the bikes
void port_gpu_jelolteglalap( int x1, int y1, int x2, int y2 ); // a box is in front

#endif
