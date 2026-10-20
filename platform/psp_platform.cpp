// psp_platform.cpp - PSP implementation of the game's platform layer.
//
// (port) Part of the PSP adaptation of the Elasto Mania source code.
// Replaces F_WIN/W_V.CPP (window, main loop, mouse, timer, errors),
// F_WIN/W_DD.CPP (DirectDraw), F_WIN/W_K.CPP + F_WIN/W_DI.CPP (keyboard),
// and F_WIN/W_SOUND2.CPP (DirectSound streaming). The interfaces are the ones
// declared in M_V.H, M_DD.H, M_K.H and HANG.H, so the game code is unchanged.
//
// Video:  sceGu, the game's 8-bit screen used as a CLUT texture (T8 + 256-colour
//         palette). Gameplay is drawn at 600x340 and the menus at 848x480, both
//         scaled down to the full 480x272 screen. The level editor (640x480,
//         or 848x480 in the wide editor build) is scaled to the full screen too,
//         or shown 1:1 and scrolled (L toggles).
// Input:  sceCtrl buttons + analog stick (the stick works as the D-Pad, except
//         in the level editor, where the stick and the D-Pad move the cursor).
// Sound:  sceAudio, 44100 Hz stereo, upsampled from the original 11025 Hz mixer.
// Text:   sceUtilityOsk (PSP on-screen keyboard).

#include "all.h"
#include "dik.h"
#include "port.h"

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <psppower.h>
#include <psputility.h>
#include <pspdebug.h>
// (compat/ has its own malloc.h, so declare newlib's memalign directly)
extern "C" void* memalign( size_t align, size_t size );

PSP_MODULE_INFO( "Elma", PSP_MODULE_USER, 1, 0 );
PSP_MAIN_THREAD_ATTR( THREAD_ATTR_USER | THREAD_ATTR_VFPU );
PSP_HEAP_SIZE_KB( -2048 ); // all memory but 2 MB (thread stacks, system dialogs)

// ---------------------------------------------------------------------------
// Globals expected by the game (originally in the Windows layer)
// ---------------------------------------------------------------------------

int Win_95 = 1;
int Margrafikus = 0;
int Locky0_alul = 0;
int Mostvoltegynemcurpos = 0; // used by the mixer (psp_hhigh.cpp)
extern int Waitforretrace;    // main.cpp

int Port_kepx = PORT_MENUX, Port_kepy = PORT_MENUY;
int Port_szeles = 0;
int Port_jatekgombok = 0;
int Port_listaismetles = 0;
int Port_jatekx = PORT_FIZX, Port_jateky = PORT_FIZY;

// Gameplay at Port_jatekx x Port_jateky, the level editor at 640x480, the menus
// at PORT_MENUX x PORT_MENUY:
void port_kepmeretfrissit( void ) {
	extern int Editorban_dialnak; // dialog.cpp
	if( Port_szeles ) {
		Port_kepx = Port_jatekx; // zoom 1.0: PORT_FIZX x PORT_FIZY
		Port_kepy = Port_jateky;
	}
	else if( Editorban_dialnak ) {
		Port_kepx = PORT_EDX;
		Port_kepy = PORT_EDY;
	}
	else {
		Port_kepx = PORT_MENUX;
		Port_kepy = PORT_MENUY;
	}
}

static void beallitkepmeret( void ) {
	port_kepmeretfrissit();
}

port_jatek_guard::port_jatek_guard( void ) {
	regiszeles = Port_szeles;
	regigombok = Port_jatekgombok;
	Port_szeles = 1;
	Port_jatekgombok = 1;
	beallitkepmeret();
}
port_jatek_guard::~port_jatek_guard( void ) {
	Port_szeles = regiszeles;
	Port_jatekgombok = regigombok;
	beallitkepmeret();
}
port_gombfelvetel_guard::port_gombfelvetel_guard( void ) {
	regigombok = Port_jatekgombok;
	Port_jatekgombok = 1;
}
port_gombfelvetel_guard::~port_gombfelvetel_guard( void ) {
	Port_jatekgombok = regigombok;
}

// ---------------------------------------------------------------------------
// Video (replaces W_DD.CPP)
// ---------------------------------------------------------------------------

#define BUFW (PORT_MENUX) // row stride of the game's screen buffers (widest screen)
#define BUFH (512)        // rows: power of two >= 480, the GPU reads them as a texture

static unsigned char* Buff[2] = { NULL, NULL };
static int Buffx[2] = { PORT_MENUX, PORT_MENUX }; // size of the picture in each buffer
static int Buffy[2] = { PORT_MENUY, PORT_MENUY };
static int Elso = 0;                              // index of the front buffer
static unsigned char* Sormut[PORT_KLASSZY];       // row pointers handed out
static int Bufflocked = 0;
static unsigned int __attribute__((aligned(16))) Paletta[256]; // current palette (ABGR)
static unsigned int __attribute__((aligned(16))) Lista[0x1000]; // GU display list (one frame needs < 2 KB)
static int Guinit = 0;
// Front buffer or palette changed but not shown yet. Shown from mv_check, so
// many small front-buffer updates (level editor) cost one screen refresh:
static int Piszkos = 0;

// Level editor: 1:1 view (scrolls with the cursor) instead of the fitted view.
static int Nagyitas = 0;
static int Nezx = 0, Nezy = 0;

static int mostszerkeszto( void );

#define PALSZIN( r, g, b ) ( 0xff000000u | ((unsigned)(b)<<16) | ((unsigned)(g)<<8) | (unsigned)(r) )

static void guinit( void ) {
	sceGuInit();
	sceGuStart( GU_DIRECT, Lista );
	sceGuDrawBuffer( GU_PSM_8888, (void*)0, 512 );
	sceGuDispBuffer( 480, 272, (void*)0x88000, 512 );
	sceGuDepthBuffer( (void*)0x110000, 512 );
	sceGuOffset( 2048 - 240, 2048 - 136 );
	sceGuViewport( 2048, 2048, 480, 272 );
	sceGuScissor( 0, 0, 480, 272 );
	sceGuEnable( GU_SCISSOR_TEST );
	sceGuDisable( GU_DEPTH_TEST );
	sceGuDisable( GU_BLEND );
	sceGuEnable( GU_TEXTURE_2D );
	sceGuClearColor( 0xff000000 );
	sceGuClear( GU_COLOR_BUFFER_BIT );
	sceGuFinish();
	sceGuSync( 0, 0 );
	sceDisplayWaitVblankStart();
	sceGuDisplay( GU_TRUE );
	Guinit = 1;
}

static void ddinit( void ) {
	for( int i = 0; i < 2; i++ ) {
		// A little extra at the end: the GPU may fetch one texel past the last row
		Buff[i] = (unsigned char*)memalign( 64, BUFW*BUFH + 1024 );
		if( !Buff[i] )
			uzenet( (char*)"memory ddinit!" );
		memset( Buff[i], 0, BUFW*BUFH + 1024 );
	}
	for( int i = 0; i < 256; i++ )
		Paletta[i] = PALSZIN( i, i, i );
	Margrafikus = 1;
}

static void ddfinish( void ) {
	if( Guinit ) {
		sceGuTerm();
		Guinit = 0;
	}
}

struct csucs {
	float u, v;
	float x, y, z;
};

// Draws the source rectangle of the front buffer to the screen rectangle, in
// 64-pixel wide strips (GU textures are at most 512 wide).
static void kiteglalap( unsigned char* buff, int sx, int sy, int sw, int sh,
						float dx, float dy, float dw, float dh, int simit ) {
	float skx = dw/sw, sky = dh/sh;
	sceGuTexFilter( simit ? GU_LINEAR : GU_NEAREST, simit ? GU_LINEAR : GU_NEAREST );
	for( int s = sx; s < sx+sw; s += 64 ) {
		int w = sx+sw-s;
		if( w > 64 )
			w = 64;
		int alap = s & ~15; // texture address must be 16-byte aligned
		int u0 = s - alap;
		sceGuTexImage( 0, 128, 512, BUFW, buff + sy*BUFW + alap );
		csucs* v = (csucs*)sceGuGetMemory( 2*sizeof( csucs ) );
		v[0].u = u0;
		v[0].v = 0;
		v[0].x = dx + (s-sx)*skx;
		v[0].y = dy;
		v[0].z = 0;
		v[1].u = u0 + w;
		v[1].v = sh;
		v[1].x = dx + (s-sx+w)*skx;
		v[1].y = dy + sh*sky;
		v[1].z = 0;
		sceGuDrawArray( GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
						2, 0, v );
	}
}

// Renders the current front buffer; the caller finishes with keretvege().
static void keretrajzol( void ) {
	unsigned char* buff = Buff[Elso];
	sceKernelDcacheWritebackRange( buff, BUFW*BUFH );
	sceKernelDcacheWritebackRange( Paletta, sizeof( Paletta ) );

	sceGuStart( GU_DIRECT, Lista );
	// Restate everything we rely on (system dialogs draw with their own state):
	sceGuEnable( GU_TEXTURE_2D );
	sceGuDisable( GU_BLEND );
	sceGuDisable( GU_DEPTH_TEST );
	sceGuDisable( GU_ALPHA_TEST );
	sceGuScissor( 0, 0, 480, 272 );
	sceGuClearColor( 0xff000000 );
	sceGuClear( GU_COLOR_BUFFER_BIT );
	sceGuClutMode( GU_PSM_8888, 0, 0xff, 0 );
	sceGuClutLoad( 256/8, Paletta );
	sceGuTexMode( GU_PSM_T8, 0, 0, 0 );
	sceGuTexFunc( GU_TFX_REPLACE, GU_TCC_RGB );
	sceGuTexWrap( GU_CLAMP, GU_CLAMP );
	sceGuTexFlush();

	// The picture in the front buffer fills the whole PSP screen (gameplay,
	// menus, and the editor, which is stretched when it is 640x480). The
	// editor's 1:1 view (L) shows a 480x272 part of it instead:
	int bx = Buffx[Elso], by = Buffy[Elso];
	if( Nagyitas && mostszerkeszto() && bx == PORT_EDX && by == PORT_EDY )
		kiteglalap( buff, Nezx, Nezy, 480, 272, 0, 0, 480, 272, 0 );
	else {
		int simit = !(bx == 480 && by == 272);
		kiteglalap( buff, 0, 0, bx, by, 0, 0, 480, 272, simit );
	}

	sceGuFinish();
	sceGuSync( 0, 0 );
}

static void keretvege( void ) {
	sceDisplayWaitVblankStart();
	sceGuSwapBuffers();
}

static void megjelenit( void ) {
	Piszkos = 0;
	if( !Guinit || !Buff[Elso] )
		return;
	keretrajzol();
	keretvege();
}

static unsigned char** sorokat( unsigned char* buff ) {
	int ymeret = Port_kepy;
	unsigned char* mut = buff;
	for( int y = 0; y < ymeret; y++ ) {
		if( Locky0_alul )
			Sormut[ymeret-1-y] = mut;
		else
			Sormut[y] = mut;
		mut += BUFW;
	}
	return Sormut;
}

unsigned char** lockbackbuffer( int fizikai ) {
	fizikai++;
	if( Bufflocked )
		hiba( (char*)"lockbackbuffer-ben Bufflocked!" );
	Bufflocked = 1;
	Buffx[1-Elso] = Port_kepx;
	Buffy[1-Elso] = Port_kepy;
	return sorokat( Buff[1-Elso] );
}

void unlockbackbuffer( void ) {
	if( !Bufflocked )
		hiba( (char*)"unlockbackbuffer-ben !Bufflocked!" );
	Bufflocked = 0;
	Elso = 1-Elso; // Flip
	megjelenit();
}

unsigned char** lockfrontbuffer( void ) {
	if( Bufflocked )
		hiba( (char*)"lockfrontbuffer-ben Bufflocked!" );
	Bufflocked = 1;
	Buffx[Elso] = Port_kepx;
	Buffy[Elso] = Port_kepy;
	return sorokat( Buff[Elso] );
}

void unlockfrontbuffer( void ) {
	if( !Bufflocked )
		hiba( (char*)"unlockfrontbuffer-ben !Bufflocked!" );
	Bufflocked = 0;
	Piszkos = 1; // shown by the next mv_check
}

// Palette: 'tomb' holds 256 RGB triplets with 6-bit components (VGA style).
ddpal::ddpal( unsigned char* tomb ) {
	unsigned int* pal = new unsigned int[256];
	if( !pal )
		hiba( (char*)"memory ddpal!" );
	for( int i = 0; i < 256; i++ ) {
		unsigned char r = (unsigned char)(tomb[3*i]<<2);
		unsigned char g = (unsigned char)(tomb[3*i+1]<<2);
		unsigned char b = (unsigned char)(tomb[3*i+2]<<2);
		pal[i] = PALSZIN( r, g, b );
	}
	pddpal = pal;
}

ddpal::~ddpal( void ) {
	delete[] (unsigned int*)pddpal;
	pddpal = NULL;
}

void ddpal::set( void ) {
	memcpy( Paletta, pddpal, sizeof( Paletta ) );
	// Like DirectDraw, a palette change is visible at once (next mv_check):
	Piszkos = 1;
}

// ---------------------------------------------------------------------------
// Buttons and analog stick (replaces W_DI.CPP)
// ---------------------------------------------------------------------------

enum { MOD_MENU = 0, MOD_EDITOR, MOD_JATEK };

static unsigned int Gombok = 0;   // buttons pressed now
static unsigned int Elozogombok = 0;
static unsigned int Tiltott = 0;  // held through a mode change: ignored until released
static int Aktmod = MOD_MENU;
static unsigned char Lx = 128, Ly = 128;
static unsigned int Analoggombok = 0; // D-Pad directions the stick is pushed to
#define DPAD_GOMBOK (PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT)

static int kellmod( void ) {
	extern int Editorban_dialnak; // dialog.cpp
	if( Port_jatekgombok )
		return MOD_JATEK;
	if( Editorban_dialnak )
		return MOD_EDITOR;
	return MOD_MENU;
}

static int mostszerkeszto( void ) {
	return !Port_szeles && kellmod() == MOD_EDITOR;
}

// In the level editor the analog stick moves the mouse cursor (also while a
// file list is open):
static int egerstick( void ) {
	return kellmod() == MOD_EDITOR;
}

// ... and so does the D-Pad, except in a file list, where it moves through
// the list:
static int egerdpad( void ) {
	return kellmod() == MOD_EDITOR && !Port_listaismetles;
}


// The analog stick as a D-Pad. With hysteresis, so a direction does not
// flicker on and off near the threshold:
static unsigned int analogiranyok( int dx, int dy ) {
	const int be = 64, ki = 40;
	unsigned int r = 0;
	#define IRANY( gomb, ertek ) \
		if( (Analoggombok & (gomb)) ? (ertek) > ki : (ertek) > be ) r |= (gomb)
	IRANY( PSP_CTRL_RIGHT, dx );
	IRANY( PSP_CTRL_LEFT, -dx );
	IRANY( PSP_CTRL_DOWN, dy );
	IRANY( PSP_CTRL_UP, -dy );
	#undef IRANY
	return r;
}

static void olvasbemenet( void ) {
	SceCtrlData pad;
	memset( &pad, 0, sizeof( pad ) );
	sceCtrlPeekBufferPositive( &pad, 1 );
	Elozogombok = Gombok;
	Gombok = pad.Buttons;
	Lx = pad.Lx;
	Ly = pad.Ly;

	// The left analog stick does the same as the D-Pad in the menus and in
	// gameplay:
	if( egerstick() )
		Analoggombok = 0;
	else
		Analoggombok = analogiranyok( (int)Lx-128, (int)Ly-128 );
	Gombok |= Analoggombok;

	port_kepmeretfrissit();

	// A button held through a mode change is ignored until released. Opening
	// or closing an editor file list counts too (so the click that opened a
	// list does not also click inside it):
	static int Aktlista = 0;
	int mod = kellmod();
	if( mod != Aktmod || Port_listaismetles != Aktlista ) {
		Aktmod = mod;
		Aktlista = Port_listaismetles;
		Tiltott = Gombok;
	}
	Tiltott &= Gombok;


	// Level editor: L switches between the fitted and the 1:1 view
	unsigned int uj = Gombok & ~Elozogombok & ~Tiltott;
	if( Aktmod == MOD_EDITOR && (uj & PSP_CTRL_LTRIGGER) ) {
		Nagyitas = !Nagyitas;
		Piszkos = 1;
	}
}

// Fills a 256-entry DirectInput-style key state table:
static void refreshDI( unsigned char* buffer ) {
	memset( buffer, 0, 256 );
	unsigned int g = Gombok & ~Tiltott;
	#define BE( gomb, kod ) if( g & (gomb) ) buffer[kod] = 0x80

	if( Aktmod == MOD_JATEK ) {
		// Gameplay. These codes are the default key bindings (see
		// di_reset_state), so they can also be reassigned in Options.
		BE( PSP_CTRL_CROSS,    DIK_UP );       // accelerate
		BE( PSP_CTRL_SQUARE,   DIK_DOWN );     // brake
		BE( PSP_CTRL_RIGHT,    DIK_RIGHT );    // rotate clockwise
		BE( PSP_CTRL_LEFT,     DIK_LEFT );     // rotate anticlockwise
		BE( PSP_CTRL_CIRCLE,   DIK_SPACE );    // turn around
		BE( PSP_CTRL_SELECT,   DIK_ESCAPE );   // back to the menu
		BE( PSP_CTRL_UP,       PORT_KOD_DFEL ); // timer on/off
		BE( PSP_CTRL_DOWN,     PORT_KOD_DLE );  // minimap on/off
		BE( PSP_CTRL_LTRIGGER, DIK_MINUS );    // smaller screen
		BE( PSP_CTRL_RTRIGGER, DIK_EQUALS );   // bigger screen
		BE( PSP_CTRL_TRIANGLE, DIK_I );        // screenshot
		BE( PSP_CTRL_START,    DIK_RETURN );
	}
	else {
		// Menus and level editor. In the editor (outside the file lists) the
		// D-Pad moves the cursor and gives no arrow keys:
		if( !egerdpad() ) {
			int r = (g & PSP_CTRL_RTRIGGER) != 0;
			if( r ) {
				BE( PSP_CTRL_UP,   DIK_PRIOR );    // page up
				BE( PSP_CTRL_DOWN, DIK_NEXT );     // page down
			}
			else {
				BE( PSP_CTRL_UP,   DIK_UP );
				BE( PSP_CTRL_DOWN, DIK_DOWN );
			}
			BE( PSP_CTRL_LEFT,   DIK_LEFT );
			BE( PSP_CTRL_RIGHT,  DIK_RIGHT );
		}
		BE( PSP_CTRL_CIRCLE, DIK_ESCAPE );     // back / cancel
		BE( PSP_CTRL_START,  DIK_RETURN );
		BE( PSP_CTRL_SQUARE, DIK_BACK );       // backspace (text fields)
		if( Aktmod == MOD_MENU ) {
			BE( PSP_CTRL_CROSS, DIK_RETURN );  // select (in the editor Cross only clicks)
		}
		else {
			// In the editor Cross is the left mouse button (see getbutbmou).
			BE( PSP_CTRL_TRIANGLE, DIK_SPACE ); // editor SPACE actions
		}
	}
	#undef BE
}

// Default key bindings (called from state_s::resetcontrols):
void di_reset_state( state_s* mut ) {
	mut->opciok1.billgaz    = DIK_UP;     // Cross
	mut->opciok1.billfek    = DIK_DOWN;   // Square
	mut->opciok1.billugras1 = DIK_RIGHT;  // D-Pad Right
	mut->opciok1.billugras2 = DIK_LEFT;   // D-Pad Left
	mut->opciok1.billfordul = DIK_SPACE;  // Circle
	mut->opciok1.billview   = PORT_KOD_DLE;  // D-Pad Down
	mut->opciok1.billtime   = PORT_KOD_DFEL; // D-Pad Up
	mut->opciok1.billshowkep= 0;          // no button left for it

	// A second player has no controller on the PSP:
	mut->opciok2.billgaz = 0;
	mut->opciok2.billfek = 0;
	mut->opciok2.billugras1 = 0;
	mut->opciok2.billugras2 = 0;
	mut->opciok2.billfordul = 0;
	mut->opciok2.billview   = 0;
	mut->opciok2.billtime   = 0;
	mut->opciok2.billshowkep= 0;

	mut->billplussz = 0;                  // not assigned (L / R zoom instead)
	mut->billminusz = 0;
	mut->billzoomin  = DIK_EQUALS;        // R
	mut->billzoomout = DIK_MINUS;         // L
	mut->billsnap = DIK_I;                // Triangle
}

// Names shown in Options / Customize controls:
char* kodtobillnev( int billkod ) {
	switch( billkod ) {
		case DIK_UP:     return (char*)"Cross";
		case DIK_DOWN:   return (char*)"Square";
		case DIK_RIGHT:  return (char*)"D-Pad Right";
		case DIK_LEFT:   return (char*)"D-Pad Left";
		case DIK_SPACE:  return (char*)"Circle";
		case PORT_KOD_DFEL: return (char*)"D-Pad Up";
		case PORT_KOD_DLE:  return (char*)"D-Pad Down";
		case DIK_MINUS:  return (char*)"L";
		case DIK_EQUALS: return (char*)"R";
		case DIK_I:      return (char*)"Triangle";
		case DIK_ESCAPE: return (char*)"Select";
		case DIK_RETURN: return (char*)"Start";
		default:         return NULL;
	}
}

// Bindings saved by earlier versions used V and T for D-Pad Up and Down (in
// either order). Those become the new defaults, timer on Up and minimap on
// Down. Afterwards the codes are no longer V / T, so this happens only once
// and the buttons can be rebound freely:
static int regidpadkod( int k ) {
	return k == DIK_V || k == DIK_T || k == 0x3B || k == 0x3C;
}

void port_billkodjavit( jatekosopciok* popc ) {
	int v = popc->billview, t = popc->billtime;
	if( regidpadkod( v ) && regidpadkod( t ) && v != t ) {
		popc->billtime = PORT_KOD_DFEL;
		popc->billview = PORT_KOD_DLE;
	}
}

int controlaltnyomva( void ) {
	return 0; // There is no Ctrl+Alt on the PSP
}

// ---------------------------------------------------------------------------
// Keyboard character queue (replaces W_K.CPP)
// ---------------------------------------------------------------------------

#define BUFFSIZE (160)
static int Buffer[BUFFSIZE];
static unsigned char Ismetlesek[BUFFSIZE]; // 1: the character is an auto-repeat
static unsigned char Buffer1[256], Buffer2[256];
static int Elsojon = 1;
static int Charszam = 0;
static int Konvtomb[256];
static int Utolsoismetelt = 0;

static void beirchar( int c, int ismetelt = 0 ) {
	if( Charszam < BUFFSIZE ) {
		Ismetlesek[Charszam] = (unsigned char)ismetelt;
		Buffer[Charszam++] = c;
	}
}

int mk_utolsoismetelt( void ) {
	return Utolsoismetelt;
}

static void initwkonvtomb( void ) {
	for( int i = 0; i < 256; i++ )
		Konvtomb[i] = 0;
	Konvtomb[DIK_ESCAPE] = MK_ESC;
	Konvtomb[DIK_RETURN] = MK_ENTER;
	Konvtomb[DIK_UP] = MK_UP;
	Konvtomb[DIK_DOWN] = MK_DOWN;
	Konvtomb[DIK_LEFT] = MK_LEFT;
	Konvtomb[DIK_RIGHT] = MK_RIGHT;
	Konvtomb[DIK_PRIOR] = MK_PGUP;
	Konvtomb[DIK_NEXT] = MK_PGDOWN;
	Konvtomb[DIK_SPACE] = ' ';
	Konvtomb[DIK_BACK] = MK_BACKSPACE;
	// Gameplay-only codes produce no characters
}

void mk_init( void ) {
	memset( Buffer1, 0, sizeof( Buffer1 ) );
	memset( Buffer2, 0, sizeof( Buffer2 ) );
	Charszam = 0;
	Elsojon = 1;
	initwkonvtomb();
}

int mk_getextchar( void ) {
	while( 1 ) {
		mv_check();
		if( Charszam > 0 ) {
			int c = Buffer[0];
			Utolsoismetelt = Ismetlesek[0];
			for( int i = 0; i < Charszam-1; i++ ) {
				Buffer[i] = Buffer[i+1];
				Ismetlesek[i] = Ismetlesek[i+1];
			}
			Charszam--;
			return c;
		}
	}
}

void mk_emptychar( void ) {
	mv_check();
	Charszam = 0;
}

int mk_kbhit( void ) {
	mv_check();
	return Charszam > 0;
}

int mk_getstate( int kod ) {
	if( kod < 0 || kod > 255 )
		return 0;
	return Elsojon ? Buffer2[kod] : Buffer1[kod];
}

static void mkw_getDIstate( void ) {
	if( Elsojon )
		refreshDI( Buffer1 );
	else
		refreshDI( Buffer2 );
	Elsojon = !Elsojon;
}

static void mkw_setkeydown( void ) {
	unsigned char *regi, *uj;
	if( Elsojon ) {
		regi = Buffer1;
		uj = Buffer2;
	}
	else {
		regi = Buffer2;
		uj = Buffer1;
	}
	for( int i = 0; i < 256; i++ )
		if( !regi[i] && uj[i] && Konvtomb[i] )
			beirchar( Konvtomb[i] );

	// Auto-repeat of a held direction (Up / Down, Page Up / Page Down) in the
	// menus and in the editor's file lists. A repeat is only queued when the
	// previous one has been used up, so a slow screen never scrolls on after
	// the button is released:
	static const int ismkodok[4] = { DIK_UP, DIK_DOWN, DIK_PRIOR, DIK_NEXT };
	static SceInt64 Kovetkezo[4] = { 0, 0, 0, 0 };
	const SceInt64 kesleltetes = 400000, ismetlesido = 75000; // microseconds
	int lehet = Aktmod == MOD_MENU || (Aktmod == MOD_EDITOR && Port_listaismetles);
	SceInt64 most = sceKernelGetSystemTimeWide();
	for( int k = 0; k < 4; k++ ) {
		int kod = ismkodok[k];
		if( !uj[kod] || !lehet || !regi[kod] ) {
			Kovetkezo[k] = most + kesleltetes;
			continue;
		}
		if( most >= Kovetkezo[k] && Charszam == 0 ) {
			beirchar( Konvtomb[kod], 1 );
			Kovetkezo[k] = most + ismetlesido;
		}
	}
}

// ---------------------------------------------------------------------------
// Mouse emulation for the level editor: the analog stick moves the cursor,
// Cross = left button, Circle = right button. Coordinates are 640x480.
// ---------------------------------------------------------------------------

static double Egerx = PORT_EDX/2, Egery = PORT_EDY/2;
static SceInt64 Utolsomozgas = 0;

static void mozgategeret( void ) {
	SceInt64 most = sceKernelGetSystemTimeWide();
	double dt = Utolsomozgas ? (most-Utolsomozgas)/1000000.0 : 0.0;
	Utolsomozgas = most;
	if( dt > 0.1 )
		dt = 0.1;
	if( !egerstick() )
		return; // the stick is the D-Pad now

	const int holtsav = 30;
	int dx = (int)Lx-128, dy = (int)Ly-128;
	if( dx > -holtsav && dx < holtsav )
		dx = 0;
	if( dy > -holtsav && dy < holtsav )
		dy = 0;
	if( dx || dy ) {
		// Faster as the stick is pushed further (up to ~600 pixel/sec):
		double fx = dx/128.0, fy = dy/128.0;
		Egerx += fx*fabs( fx )*600.0*dt;
		Egery += fy*fabs( fy )*600.0*dt;
	}

	// The D-Pad moves the cursor too, a bit slower than the stick pushed all
	// the way (350 vs 600 pixel/sec, for more exact placing):
	const double dpadseb = 350.0;
	unsigned int dp = egerdpad() ? (Gombok & ~Tiltott & DPAD_GOMBOK) : 0;
	if( dp & PSP_CTRL_LEFT )  Egerx -= dpadseb*dt;
	if( dp & PSP_CTRL_RIGHT ) Egerx += dpadseb*dt;
	if( dp & PSP_CTRL_UP )    Egery -= dpadseb*dt;
	if( dp & PSP_CTRL_DOWN )  Egery += dpadseb*dt;
	if( Egerx < 0 ) Egerx = 0;
	if( Egery < 0 ) Egery = 0;
	if( Egerx > Port_kepx-1 ) Egerx = Port_kepx-1;
	if( Egery > Port_kepy-1 ) Egery = Port_kepy-1;

	// 1:1 editor view: keep the cursor inside the visible part
	if( Nagyitas && mostszerkeszto() ) {
		const int szel = 48;
		int mx = (int)Egerx, my = (int)Egery;
		int ujx = Nezx, ujy = Nezy;
		if( mx < ujx+szel ) ujx = mx-szel;
		if( mx > ujx+480-szel ) ujx = mx-480+szel;
		if( my < ujy+szel ) ujy = my-szel;
		if( my > ujy+272-szel ) ujy = my-272+szel;
		if( ujx < 0 ) ujx = 0;
		if( ujy < 0 ) ujy = 0;
		if( ujx > PORT_EDX-480 ) ujx = PORT_EDX-480;
		if( ujy > PORT_EDY-272 ) ujy = PORT_EDY-272;
		if( ujx != Nezx || ujy != Nezy ) {
			Nezx = ujx;
			Nezy = ujy;
			Piszkos = 1;
		}
	}
}

void getmou( int* px, int* py ) {
	*px = (int)Egerx;
	*py = (int)Egery;
}

void setmou( int x, int y ) {
	Egerx = x;
	Egery = y;
}

int getbutbmou( void ) {
	mv_check();
	return Aktmod == MOD_EDITOR &&
		   ((Gombok & ~Tiltott) & PSP_CTRL_CROSS);
}

int getbutjmou( void ) {
	mv_check();
	return Aktmod == MOD_EDITOR && !Port_listaismetles &&
		   ((Gombok & ~Tiltott) & PSP_CTRL_CIRCLE);
}

// ---------------------------------------------------------------------------
// Main loop helpers, timer, errors (replaces W_V.CPP)
// ---------------------------------------------------------------------------

static void kilepeskezel( void );
static int hibabanvan( void );

void mv_check( int nemkellnyomas ) {
	kilepeskezel();
	// Not while queued (e.g. injected keyboard) characters are still being
	// processed, so typed text appears at once instead of letter by letter:
	if( Piszkos && !Bufflocked && Charszam == 0 )
		megjelenit();
	hang();
	olvasbemenet();
	mozgategeret();
	mkw_getDIstate();
	if( Aktmod == MOD_JATEK ) // no dimming or sleep during play / replays
		scePowerTick( PSP_POWER_TICK_ALL );
	if( nemkellnyomas )
		return;
	mkw_setkeydown();
}

void dsoundfinish( void );

void mv_exit( char* text ) {
	text = text;
	dsoundfinish();
	ddfinish();
	sceKernelExitGame();
	while( 1 )
		sceKernelSleepThread();
}

static double Startido = 0;

static double ezredmp( void ) {
	return sceKernelGetSystemTimeWide()/1000.0;
}

// Returns time in 1/182 seconds, like the original:
double mv_stopperido( void ) {
	return ezredmp()*0.182 - Startido;
}

void mv_startstopper( void ) {
	Startido = ezredmp()*0.182;
}

void getbiostort( unsigned short* pbios, unsigned short* ptort ) {
	SceInt64 t = sceKernelGetSystemTimeWide();
	*pbios = (unsigned short)(t/54925);
	*ptort = (unsigned short)(t%54925);
}

int s_random( int maximum ) {
	return rand()%maximum;
}

// Shows a message with the debug font when the game's own graphics are not
// available (e.g. the data files are missing):
static void szovegesuzenet( const char* text ) {
	if( Guinit ) {
		sceGuTerm();
		Guinit = 0;
	}
	pspDebugScreenInit();
	pspDebugScreenPrintf( "Elasto Mania\n\n%s\n\nPress X or O to exit.\n", text );
	while( 1 ) {
		SceCtrlData pad;
		memset( &pad, 0, sizeof( pad ) );
		sceCtrlPeekBufferPositive( &pad, 1 );
		if( pad.Buttons & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE) )
			break;
		sceDisplayWaitVblankStart();
	}
}

static int Hibaban = 0;
static int hibabanvan( void ) {
	return Hibaban;
}
static char Tmphibatext[500];

static void directhiba( char* text1, char* text2, char* text3, char* text4 ) {
	FILE* h = fopen( Hibaban ? "error_b.txt" : "error.txt", "wt" );
	if( h ) {
		fprintf( h, "%s\n", text1 );
		if( text2 ) fprintf( h, "%s\n", text2 );
		if( text3 ) fprintf( h, "%s\n", text3 );
		if( text4 ) fprintf( h, "%s\n", text4 );
		if( Hibaban )
			fprintf( h, "Two errors while processing!\n" );
		fclose( h );
	}
	if( Hibaban )
		mv_exit();
	Hibaban = 1;

	// The error may come while a buffer is locked or during gameplay: release
	// the locks and show the message on the normal 640x480 screen with menu
	// buttons (Cross/Start = Enter, Circle = Esc):
	Bufflocked = 0;
	void port_picfeloldas( void ); // d_pic.cpp
	port_picfeloldas();
	Locky0_alul = 0;
	Port_szeles = 0;
	Port_jatekgombok = 0;
	beallitkepmeret();

	if( Margrafikus ) {
		if( text4 && text3 ) {
			if( strlen( text3 ) + strlen( text4 ) < 490 ) {
				strcpy( Tmphibatext, text3 );
				strcat( Tmphibatext, " " );
				strcat( Tmphibatext, text4 );
				text3 = Tmphibatext;
			}
		}
		void grafikushiba( char* text1, char* text2 = NULL, char* text3 = NULL );
		grafikushiba( text1, text2, text3 );
		while( 1 ) {
			int c = mk_getextchar();
			if( c == MK_ESC || c == MK_ENTER )
				break;
		}
	}
	else {
		char text[600];
		snprintf( text, sizeof( text ), "%s %s %s %s", text1,
				  text2 ? text2 : "", text3 ? text3 : "", text4 ? text4 : "" );
		szovegesuzenet( text );
	}
	mv_exit();
}

void hiba( char* text1, char* text2, char* text3 ) {
	if( access( "message.inf", 0 ) == 0 )
		directhiba( (char*)"<<message.inf accessed>>", text1, text2, text3 );
	directhiba( (char*)"Sorry, internal error.", NULL, NULL, NULL );
}

void uzenet( char* text1, char* text2, char* text3 ) {
	if( strstr( text1, "memory" ) && !strstr( text1, "about" ) ) {
		if( access( "message.inf", 0 ) == 0 )
			directhiba( (char*)"message.inf accessed\n", text1, text2, text3 );
		directhiba( (char*)"Sorry, out of memory!", NULL, NULL, NULL );
	}
	directhiba( text1, text2, text3, NULL );
}

// ---------------------------------------------------------------------------
// On-screen keyboard
// ---------------------------------------------------------------------------

static void ascii2utf16( unsigned short* dest, const char* sour, int max ) {
	int i = 0;
	for( ; sour && sour[i] && i < max-1; i++ )
		dest[i] = (unsigned short)(unsigned char)sour[i];
	dest[i] = 0;
}

void port_ime_bevitel( const char* cim, const char* kezdo, int maxhossz,
					   int csakszam, int torlesszam, int torlokod,
					   int enterrel ) {
	static unsigned short cim16[128];
	static unsigned short kezdo16[128];
	static unsigned short ki16[128];
	if( maxhossz < 1 )
		maxhossz = 1;
	if( maxhossz > 100 )
		maxhossz = 100;
	ascii2utf16( cim16, cim, 128 );
	ascii2utf16( kezdo16, kezdo, maxhossz+1 );
	memset( ki16, 0, sizeof( ki16 ) );

	SceUtilityOskData adat;
	memset( &adat, 0, sizeof( adat ) );
	adat.language = PSP_UTILITY_OSK_LANGUAGE_ENGLISH;
	adat.lines = 1;
	adat.unk_24 = 1;
	adat.inputtype = csakszam ? PSP_UTILITY_OSK_INPUTTYPE_LATIN_DIGIT
							  : PSP_UTILITY_OSK_INPUTTYPE_ALL;
	adat.desc = cim16;
	adat.intext = kezdo16;
	adat.outtextlength = 128;
	adat.outtextlimit = maxhossz;
	adat.outtext = ki16;

	SceUtilityOskParams param;
	memset( &param, 0, sizeof( param ) );
	param.base.size = sizeof( param );
	sceUtilityGetSystemParamInt( PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &param.base.language );
	sceUtilityGetSystemParamInt( PSP_SYSTEMPARAM_ID_INT_BUTTON_SWAP, &param.base.buttonSwap );
	param.base.graphicsThread = 17;
	param.base.accessThread = 19;
	param.base.fontThread = 18;
	param.base.soundThread = 16;
	param.datacount = 1;
	param.data = &adat;

	Charszam = 0;
	if( Piszkos && !Bufflocked )
		megjelenit(); // the keyboard is drawn over the current screen
	if( sceUtilityOskInitStart( &param ) < 0 ) {
		beirchar( MK_ESC );
		return;
	}
	int kesz = 0;
	while( !kesz ) {
		hang();
		keretrajzol();
		switch( sceUtilityOskGetStatus() ) {
			case PSP_UTILITY_DIALOG_VISIBLE:
				sceUtilityOskUpdate( 1 );
				break;
			case PSP_UTILITY_DIALOG_QUIT:
				sceUtilityOskShutdownStart();
				break;
			case PSP_UTILITY_DIALOG_NONE:
				kesz = 1;
				break;
			default:
				break;
		}
		keretvege();
	}

	// The button that closed the keyboard is still held: ignore it (and anything
	// else held, e.g. L) until released.
	Tiltott = ~0u;
	olvasbemenet();

	unsigned short* szoveg = ki16;
	if( adat.result == PSP_UTILITY_OSK_RESULT_UNCHANGED && ki16[0] == 0 )
		szoveg = kezdo16; // confirmed without changes
	if( adat.result == PSP_UTILITY_OSK_RESULT_CANCELLED ||
		(csakszam && szoveg[0] == 0) ) { // an empty number counts as cancel
		beirchar( MK_ESC );
		return;
	}
	for( int i = 0; i < torlesszam; i++ )
		beirchar( torlokod );
	for( int i = 0; szoveg[i] && i < maxhossz; i++ ) {
		unsigned short c = szoveg[i];
		if( c >= 32 && c < 127 )
			beirchar( (int)c );
	}
	if( enterrel )
		beirchar( MK_ENTER );
	Piszkos = 1;
}

// ---------------------------------------------------------------------------
// Sound output (replaces W_SOUND2.CPP). The mixer (psp_hhigh.cpp) is the
// original one at 11025 Hz mono; here it is upsampled 4x to the PSP's
// 44100 Hz stereo output.
// ---------------------------------------------------------------------------

#define HANG_GRAIN      (256)          // mixer samples per output block
#define HANG_KIMENET    (HANG_GRAIN*4) // 44100 Hz frames per output block
#define HANG_GYURU      (8192)         // ring buffer, power of two
#define HANG_CEL        (1100)         // keep about 1/10 sec queued, like the original

static short Gyuru[HANG_GYURU];
static volatile unsigned int Irpos = 0, Olvpos = 0;
static int Hangcsatorna = -1;
static SceUID Hangszal = -1;
static volatile int Hangfut = 0;
static volatile int Hangtorles = 0; // main thread asks the audio thread to drop the queue

static int hangszal( SceSize args, void* argp ) {
	args = args;
	argp = argp;
	static short be[HANG_GRAIN];
	static short __attribute__((aligned(64))) ki[HANG_KIMENET*2];
	short elozo = 0;
	while( Hangfut ) {
		if( Hangtorles ) {
			Hangtorles = 0;
			Olvpos = Irpos;
		}
		unsigned int van = Irpos - Olvpos;
		__sync_synchronize(); // samples written before Irpos was advanced
		if( van >= HANG_GRAIN && van <= HANG_GYURU ) {
			for( int i = 0; i < HANG_GRAIN; i++ )
				be[i] = Gyuru[(Olvpos+i) & (HANG_GYURU-1)];
			__sync_synchronize();
			Olvpos += HANG_GRAIN;
		}
		else
			memset( be, 0, sizeof( be ) );
		// 4x linear interpolation, mono -> stereo:
		for( int i = 0; i < HANG_GRAIN; i++ ) {
			int a = elozo, b = be[i];
			for( int k = 0; k < 4; k++ ) {
				short s = (short)(a + (b-a)*(k+1)/4);
				ki[(i*4+k)*2] = s;
				ki[(i*4+k)*2+1] = s;
			}
			elozo = be[i];
		}
		sceAudioOutputBlocking( Hangcsatorna, PSP_AUDIO_VOLUME_MAX, ki );
	}
	return 0;
}

void initdsound( int secondaryeloir ) {
	secondaryeloir = secondaryeloir;
	if( Hangcsatorna >= 0 )
		return;
	Hangcsatorna = sceAudioChReserve( PSP_AUDIO_NEXT_CHANNEL, HANG_KIMENET,
									  PSP_AUDIO_FORMAT_STEREO );
	if( Hangcsatorna < 0 ) {
		Hangenabled = 0;
		return;
	}
	Irpos = Olvpos = 0;
	Hangfut = 1;
	Hangszal = sceKernelCreateThread( "elma_hang", hangszal, 0x12, 0x4000, PSP_THREAD_ATTR_USER, NULL );
	if( Hangszal >= 0 )
		sceKernelStartThread( Hangszal, 0, NULL );
}

void dsoundfinish( void ) {
	if( Hangszal >= 0 ) {
		Hangfut = 0;
		sceKernelWaitThreadEnd( Hangszal, NULL );
		sceKernelDeleteThread( Hangszal );
		Hangszal = -1;
	}
	if( Hangcsatorna >= 0 ) {
		sceAudioChRelease( Hangcsatorna );
		Hangcsatorna = -1;
	}
}

void callbackhang( short* sbuff, int buffsize ); // psp_hhigh.cpp

// Called often from mv_check: tops up the ring buffer with mixed sound.
void hang( void ) {
	if( !Hangenabled || Hangcsatorna < 0 )
		return;
	static short darab[HANG_GRAIN];
	while( Irpos - Olvpos < HANG_CEL ) {
		int getstatesoundon( void );
		if( Mute || !getstatesoundon() )
			memset( darab, 0, sizeof( darab ) );
		else
			callbackhang( darab, HANG_GRAIN );
		for( int i = 0; i < HANG_GRAIN; i++ )
			Gyuru[(Irpos+i) & (HANG_GYURU-1)] = darab[i];
		__sync_synchronize(); // publish the samples before Irpos
		Irpos += HANG_GRAIN;
	}
}

void nullazhangot( void ) {
	// Drop what is queued so sound stops at once (done by the audio thread,
	// which is the only one that moves the read position):
	Hangtorles = 1;
}

// ---------------------------------------------------------------------------
// HOME button (exit) and entry point (replaces WinMain)
// ---------------------------------------------------------------------------

// HOME -> Quit: the game's own thread saves state.dat first (players,
// options, controls, progress), at its next mv_check. If it does not get
// there within 3 seconds (e.g. stuck loading), the game exits anyway.
static volatile int Kilepeskeres = 0;  // set by the HOME callback
static volatile int Kilepeskesz = 0;   // set when the save is done

static int kilepes_callback( int arg1, int arg2, void* common ) {
	arg1 = arg1;
	arg2 = arg2;
	common = common;
	Kilepeskeres = 1;
	for( int i = 0; i < 300 && !Kilepeskesz; i++ )
		sceKernelDelayThread( 10000 );
	sceKernelExitGame();
	return 0;
}

static void kilepeskezel( void ) {
	static int Volt = 0;
	if( !Kilepeskeres || Volt )
		return;
	Volt = 1;
	if( State && !hibabanvan() ) // not while an error is being shown
		State->save();
	Kilepeskesz = 1;
	while( 1 ) // the HOME callback ends the game
		sceKernelDelayThread( 100000 );
}

static int callback_szal( SceSize args, void* argp ) {
	args = args;
	argp = argp;
	int cbid = sceKernelCreateCallback( "Exit Callback", kilepes_callback, NULL );
	sceKernelRegisterExitCallback( cbid );
	sceKernelSleepThreadCB();
	return 0;
}

static void callbackokbeallit( void ) {
	int thid = sceKernelCreateThread( "update_thread", callback_szal, 0x11, 0xFA0, 0, 0 );
	if( thid >= 0 )
		sceKernelStartThread( thid, 0, 0 );
}

// The game data lives next to EBOOT.PBP (e.g. ms0:/PSP/GAME/ELMA/):
static int adatkonyvtar( const char* argv0 ) {
	char dir[256];
	if( !argv0 )
		return -1;
	strncpy( dir, argv0, sizeof( dir )-1 );
	dir[sizeof( dir )-1] = 0;
	char* per = strrchr( dir, '/' );
	if( !per )
		return -1;
	*per = 0;
	return chdir( dir );
}

int main( int argc, char* argv[] ) {
	callbackokbeallit();

	// The game renders in software; give it the full clock:
	scePowerSetClockFrequency( 333, 333, 166 );

	sceCtrlSetSamplingCycle( 0 );
	sceCtrlSetSamplingMode( PSP_CTRL_MODE_ANALOG );

	if( adatkonyvtar( argc > 0 ? argv[0] : NULL ) != 0 ) {
		szovegesuzenet( "Could not find the game folder." );
		mv_exit();
	}
	if( access( "elma.res", 0 ) != 0 ) {
		szovegesuzenet( "Game data not found. Copy the game files (elma.res, lev, lgr, rec ...)\n"
						"into the same folder as EBOOT.PBP." );
		mv_exit();
	}

	guinit();

	void upgradehakell( void );
	upgradehakell();

	void initqopen( void );
	initqopen();

	mk_init();

	Mute = 1;

	void initmenukep1( void );
	void initmenukep2( void );
	initmenukep1();

	mv_check();

	if( access( "noretrac.inf", 0 ) == 0 )
		Waitforretrace = 0;

	ddinit();

	initmenukep2();

	mv_main();

	hiba( (char*)"main vegere erkezett!" );
	return 0;
}
