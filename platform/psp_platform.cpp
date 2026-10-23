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
#include <pspge.h>
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
// GU display list. The vertices of each frame are allocated in it too
// (sceGuGetMemory), so it must hold a whole frame: a GPU menu with all its
// balls needs about 20 KB, so there is plenty of room:
#define LISTAMERET (0x20000) // words: 512 KB
static unsigned int __attribute__((aligned(16))) Lista[LISTAMERET];
static int Guinit = 0;
static void gpuvar( void ); // waits for the GPU if it is still drawing
// Front buffer or palette changed but not shown yet. Shown from mv_check, so
// many small front-buffer updates (level editor) cost one screen refresh:
static int Piszkos = 0;

// Level editor: 1:1 view (scrolls with the cursor) instead of the fitted view.
static int Nagyitas = 0;
static int Nezx = 0, Nezy = 0;

static int mostszerkeszto( void );

#define PALSZIN( r, g, b ) ( 0xff000000u | ((unsigned)(b)<<16) | ((unsigned)(g)<<8) | (unsigned)(r) )

static void mutatoinit( void );

static void guinit( void ) {
	sceGuInit();
	sceGuStart( GU_DIRECT, Lista );
	sceGuDrawBuffer( GU_PSM_8888, (void*)0, 512 );
	sceGuDispBuffer( 480, 272, (void*)0x88000, 512 );
	// VRAM: two screen buffers (0, 0x88000), the copy of the game's picture
	// (VRAMKEP, see keretrajzol), the depth buffer (never used) after them:
	sceGuDepthBuffer( (void*)0x178000, 512 );
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
	mutatoinit();
	Guinit = 1;
}

// The game's two 8-bit screen buffers are in video memory, after the two
// PSP screen buffers (0 .. 0x110000), written by the CPU uncached: then the
// GPU reads them from its own memory and need not compete with the CPU for
// main memory (which made it many times slower). With cpurajz.inf they are
// in main memory, as in v1.0.
#define VRAMBUFF (0x110000)
#define VRAMBUFFMERET (BUFW*480 + 1024) // a game screen buffer in video memory
static int Buffvramban = 0;
// After them, video memory for the GPU's copies of the bike pictures:
static unsigned int Vramtar = 0, Vramtarvege = 0, Vramtarfoglalt = 0;

// cpurajz.inf next to EBOOT.PBP: everything is drawn as in v1.0, by the CPU
// (menus, intro, bikes; the screen buffers in main memory, each frame shown
// when the GPU has finished it). Decided once, at the start.
static int Cpurajz = -1;
int port_cpurajz( void ) {
	if( Cpurajz < 0 )
		Cpurajz = access( "cpurajz.inf", 0 ) == 0;
	return Cpurajz;
}

static void ddinit( void ) {
	Buffvramban = !port_cpurajz() &&
				  VRAMBUFF + 2*VRAMBUFFMERET <= 0x200000;
	Vramtar = Buffvramban ? VRAMBUFF + 2*VRAMBUFFMERET : VRAMBUFF;
	Vramtar = (Vramtar + 63) & ~63u;
	Vramtarvege = 0x200000;
	for( int i = 0; i < 2; i++ ) {
		// A little extra at the end: the GPU may fetch one texel past the last row
		if( Buffvramban )
			Buff[i] = (unsigned char*)( 0x40000000 | (uintptr_t)sceGeEdramGetAddr() ) // uncached
					  + VRAMBUFF + i*VRAMBUFFMERET;
		else
			Buff[i] = (unsigned char*)memalign( 64, BUFW*BUFH + 1024 );
		if( !Buff[i] )
			uzenet( (char*)"memory ddinit!" );
		memset( Buff[i], 0, Buffvramban ? VRAMBUFFMERET : BUFW*BUFH + 1024 );
	}
	for( int i = 0; i < 256; i++ )
		Paletta[i] = PALSZIN( i, i, i );
	Margrafikus = 1;
}

static void ddfinish( void ) {
	gpuvar();
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
						float dx, float dy, float dw, float dh, int simit,
						int stride = BUFW ) {
	float skx = dw/sw, sky = dh/sh;
	sceGuTexFilter( simit ? GU_LINEAR : GU_NEAREST, simit ? GU_LINEAR : GU_NEAREST );
	for( int s = sx; s < sx+sw; s += 64 ) {
		int w = sx+sw-s;
		if( w > 64 )
			w = 64;
		int alap = s & ~15; // texture address must be 16-byte aligned
		int u0 = s - alap;
		sceGuTexImage( 0, 128, 512, stride, buff + sy*stride + alap );
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

// The GPU draws a frame while the CPU goes on with the next one. Before the
// CPU changes something the GPU may still be reading (a screen buffer, the
// palette, the display list), it waits for the GPU here; by then the GPU has
// almost always finished long ago, so this costs nothing:
//
// Showing a frame: a small thread waits for the GPU to finish the picture and
// then puts it on the screen (from the next vertical blank). Meanwhile the
// CPU goes on with the next frame; it only waits before the GPU starts the
// frame after that, which goes into the other screen buffer of the two: that
// one must not be on the screen any more.
static int Gufut = 0;                 // 1: a list runs; 2: the thread shows it when done
static SceUID Inditsema = -1, Keszsema = -1;
static int Lathato = 1;               // the screen buffer on the screen (0 or 1)
static int Rajzfb = 0;                // the screen buffer the GPU draws into
static volatile int Ujkep = 0;        // Rajzfb has been handed to the display
static volatile unsigned int Kitettvcount = 0; // the vblank count when it was
#define FBCIM( i ) ((void*)((i) ? 0x88000 : 0))
// Timing (shown by the timing overlay): the GPU's own drawing time, the time
// the CPU waited for the GPU and for the vertical blank:
static volatile SceUInt64 Gpuindul = 0, Gpuexec = 0, Gpuvarido = 0, Vblido = 0;
static volatile SceUInt64 Gpuvege = 0, Gpukesz = 0, Ebredes = 0;
// Points in the list (sceGuSignal): 1 picture drawn, 2 bikes drawn
static volatile SceUInt64 Gpujel[3] = { 0, 0, 0 }, Gpujelossz[3] = { 0, 0, 0 };
static void gpujel( int id ) {
	if( id >= 1 && id <= 2 )
		Gpujel[id] = sceKernelGetSystemTimeWide();
}

// Called (in an interrupt) when the GPU reaches the end of a list:
static void gpuveget( int ) {
	Gpuvege = sceKernelGetSystemTimeWide();
}

static void fbkitesz( void ) {
	sceDisplaySetFrameBuf( (unsigned char*)sceGeEdramGetAddr() + (uintptr_t)FBCIM( Rajzfb ),
						   512, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME );
	Kitettvcount = sceDisplayGetVcount();
	Ujkep = 1;
}

static int mutatoszal( SceSize, void* ) {
	while( 1 ) {
		if( sceKernelWaitSema( Inditsema, 1, NULL ) < 0 )
			break;
		sceGuSync( 0, 0 );
		SceUInt64 most = sceKernelGetSystemTimeWide();
		Gpuexec += most - Gpuindul;
		if( Gpuvege > Gpuindul ) { // when the GPU really finished
			Gpukesz += Gpuvege - Gpuindul;
			Ebredes += most - Gpuvege;
			for( int i = 1; i <= 2; i++ )
				if( Gpujel[i] > Gpuindul )
					Gpujelossz[i] += Gpujel[i] - Gpuindul;
		}
		fbkitesz();
		sceKernelSignalSema( Keszsema, 1 );
	}
	return 0;
}

static int Mutatovan = 0; // the thread runs

static void mutatoinit( void ) {
	if( port_cpurajz() )
		return; // as in v1.0: each frame is shown when the GPU has finished it
	Lathato = 1; // see sceGuDispBuffer in guinit
	Inditsema = sceKernelCreateSema( "elma_kep", 0, 0, 1, NULL );
	Keszsema = sceKernelCreateSema( "elma_kesz", 0, 0, 1, NULL );
	sceGuSetCallback( GU_CALLBACK_FINISH, gpuveget );
	sceGuSetCallback( GU_CALLBACK_SIGNAL, gpujel );
	SceUID szal = sceKernelCreateThread( "elma_mutat", mutatoszal, 0x10, 0x1000,
										 PSP_THREAD_ATTR_USER, NULL );
	if( szal >= 0 && Inditsema >= 0 && Keszsema >= 0 && sceKernelStartThread( szal, 0, NULL ) >= 0 )
		Mutatovan = 1;
}

static void gpuvar( void ) {
	if( !Gufut )
		return;
	SceUInt64 t = sceKernelGetSystemTimeWide();
	if( Gufut == 1 )
		sceGuSync( 0, 0 );
	if( Gufut == 2 )
		sceKernelWaitSema( Keszsema, 1, NULL );
	Gufut = 0;
	Gpuvarido += sceKernelGetSystemTimeWide() - t;
}

// Before the GPU draws into a screen buffer: the frame shown last must be on
// the screen already, so the other buffer is free. Returns the time waited.
static SceUInt64 fbvalaszt( void ) {
	gpuvar();
	SceUInt64 t = sceKernelGetSystemTimeWide();
	if( Ujkep ) {
		if( sceDisplayGetVcount() == Kitettvcount ) {
			SceUInt64 tv = sceKernelGetSystemTimeWide();
			sceDisplayWaitVblankStart(); // also keeps the pace at 60 frames a second
			Vblido += sceKernelGetSystemTimeWide() - tv;
		}
		Lathato = Rajzfb;
		Ujkep = 0;
	}
	Rajzfb = 1 - Lathato;
	return sceKernelGetSystemTimeWide() - t;
}


// ---------------------------------------------------------------------------
// GPU menus (see port_gpumenu in port.h)
// ---------------------------------------------------------------------------
static int Jatekkepen = 0;      // the screen shows a frame of a play or replay
static int Menugpu = 0;                  // the next frame shown is a GPU menu
static int Prof_w = 0, Prof_h = 0;       // timing overlay box in the screen buffer
static unsigned char* Menutex[2] = { NULL, NULL }; // plain and ball background
static int Menutexverzio = -1;
static port_korong Menukorong[PORT_MAXKORONG];
static int Menukorongdb = 0;
#define SISAKMAX (128)
static unsigned char __attribute__((aligned(16))) Sisaktex[SISAKMAX*SISAKMAX];
static int Sisakw = 0, Sisakh = 0;
// Where the helmet is drawn: it glides to a new menu item in about a tenth
// of a second instead of jumping there. The PSP's screen is slow to change a
// bright red pixel back to dark green, so a jump left a faint copy of the
// helmet behind for a moment; while it moves, that trail stays with it.
static float Sisakx = 0, Sisaky = 0;
static int Sisakvan = 0, Sisakmenuverzio = -1;
static SceUInt64 Sisakido = 0;
static unsigned int __attribute__((aligned(16))) Sisakpal[256]; // palette, transparent colour clear
// The intro animation (port_gpuintro):
static int Menuintro = 0, Introkert = 0;
static int Introelt = 0, Introhatterszin = 0, Introszovegatl = -1, Introszovegverzio = -1;
static unsigned char* Introszovegtex = NULL;
static unsigned char* Introkeptex = NULL;
static pic8* Introforras = NULL;
static int Introkepw = 0, Introkeph = 0, Introkepx = 0, Introkepy = 0, Introkepatl = -1;

void port_gpuvar( void ) {
	gpuvar();
}

unsigned char* port_menukep( int i ) {
	i &= 1;
	if( !Menutex[i] ) {
		Menutex[i] = (unsigned char*)memalign( 64, PORT_MENUX*PORT_MENUY + 1024 );
		if( !Menutex[i] )
			uzenet( (char*)"memory port_menukep!" );
		memset( Menutex[i], 0, PORT_MENUX*PORT_MENUY + 1024 );
	}
	return Menutex[i];
}

static void menutexmasol( unsigned char* cel, pic8* forras ) {
	if( forras->getptr( 0 ) == cel && forras->getxsize() == PORT_MENUX )
		return; // the picture is in this memory already (port_menukep)
	int w = forras->getxsize(), h = forras->getysize();
	if( w > PORT_MENUX ) w = PORT_MENUX;
	if( h > PORT_MENUY ) h = PORT_MENUY;
	for( int y = 0; y < h; y++ )
		memcpy( cel + y*PORT_MENUX, forras->getptr( y ), w );
}

void port_gpumenu( pic8* sima, pic8* golyo, int verzio,
				   int db, const port_korong* korongok,
				   pic8* sisak, int sisakx, int sisaky ) {
	gpuvar(); // the GPU may still be reading the textures of the last frame
	if( !Menutex[0] ) {
		for( int i = 0; i < 2; i++ ) {
			Menutex[i] = (unsigned char*)memalign( 64, PORT_MENUX*PORT_MENUY + 1024 );
			if( !Menutex[i] )
				uzenet( (char*)"memory port_gpumenu!" );
			memset( Menutex[i], 0, PORT_MENUX*PORT_MENUY + 1024 );
		}
	}
	if( verzio != Menutexverzio ) {
		menutexmasol( Menutex[0], sima );
		menutexmasol( Menutex[1], golyo );
		Menutexverzio = verzio;
	}
	if( db > PORT_MAXKORONG )
		db = PORT_MAXKORONG;
	memcpy( Menukorong, korongok, db*sizeof( port_korong ) );
	Menukorongdb = db;
	Sisakw = Sisakh = 0;
	if( sisak ) {
		Sisakw = sisak->getxsize();
		Sisakh = sisak->getysize();
		// One row and column of transparent border around the frame: the
		// smoothing must not reach the rest of the texture (the frames differ
		// in size, so it may hold a part of an earlier one):
		if( Sisakw > SISAKMAX-2 ) Sisakw = SISAKMAX-2;
		if( Sisakh > SISAKMAX-2 ) Sisakh = SISAKMAX-2;
		unsigned char lyuk = sisak->atlatszo >= 0 ? (unsigned char)sisak->atlatszo : 0;
		memset( Sisaktex, lyuk, sizeof( Sisaktex ) );
		for( int y = 0; y < Sisakh; y++ )
			memcpy( Sisaktex + (y+1)*SISAKMAX + 1, sisak->getptr( y ), Sisakw );
		SceUInt64 most = sceKernelGetSystemTimeWide();
		float dx = sisakx - Sisakx, dy = sisaky - Sisaky;
		if( !Sisakvan || verzio != Sisakmenuverzio || Introkert ||
			dx > 300 || dx < -300 || dy > 300 || dy < -300 ) {
			Sisakx = sisakx; // a new menu screen: straight there
			Sisaky = sisaky;
		}
		else {
			float dt = (most - Sisakido) / 1000000.0f;
			if( dt > 0.1f ) dt = 0.1f;
			if( dt < 0.0f ) dt = 0.0f;
			float a = 1.0f - expf( -dt/0.025f ); // most of the way in 0.1 s
			Sisakx += dx*a;
			Sisaky += dy*a;
			if( fabsf( sisakx - Sisakx ) < 0.5f && fabsf( sisaky - Sisaky ) < 0.5f ) {
				Sisakx = sisakx;
				Sisaky = sisaky;
			}
			else
				Piszkos = 1;
		}
		Sisakvan = 1;
		Sisakmenuverzio = verzio;
		Sisakido = most;
		memcpy( Sisakpal, Paletta, sizeof( Sisakpal ) );
		if( sisak->atlatszo >= 0 )
			Sisakpal[sisak->atlatszo] &= 0x00ffffff; // alpha 0
	}
	Jatekkepen = 0;
	Menuintro = Introkert; // the intro's extra layers, if port_gpuintro was called
	Introkert = 0;
	Menugpu = 1;
	Piszkos = 1; // shown by the next mv_check
}

// The intro (see port.h): the background slides up from the bottom (its top
// at 'elt'), the text, the helmet and the balls come down, the title picture
// goes down. Called before port_gpumenu, for that frame only.
void port_gpuintro( int elt, int hatterszin, pic8* szoveg, int szovegatl, int szovegverzio,
					pic8* intro, int introx, int introy ) {
	gpuvar(); // the GPU may still be reading the textures of the last frame
	Introelt = elt;
	Introhatterszin = hatterszin;
	Introszovegatl = szovegatl;
	if( szoveg && szovegverzio != Introszovegverzio ) {
		if( !Introszovegtex )
			Introszovegtex = (unsigned char*)memalign( 64, PORT_MENUX*PORT_MENUY + 1024 );
		if( Introszovegtex )
			menutexmasol( Introszovegtex, szoveg );
		Introszovegverzio = szovegverzio;
	}
	Introkepatl = -1;
	if( intro ) {
		Introkepw = intro->getxsize() > PORT_MENUX ? PORT_MENUX : intro->getxsize();
		Introkeph = intro->getysize() > PORT_MENUY ? PORT_MENUY : intro->getysize();
		if( intro != Introforras ) {
			if( !Introkeptex )
				Introkeptex = (unsigned char*)memalign( 64, PORT_MENUX*PORT_MENUY + 1024 );
			if( Introkeptex )
				for( int y = 0; y < Introkeph; y++ )
					memcpy( Introkeptex + y*PORT_MENUX, intro->getptr( y ), Introkepw );
			Introforras = intro;
		}
		Introkepatl = intro->atlatszo;
		Introkepx = introx;
		Introkepy = introy;
	}
	Introkert = 1;
}

// The intro is over: its pictures are not needed any more.
void port_gpuintro_vege( void ) {
	gpuvar();
	Introkert = Menuintro = 0;
	if( Introszovegtex ) { free( Introszovegtex ); Introszovegtex = NULL; }
	if( Introkeptex ) { free( Introkeptex ); Introkeptex = NULL; }
	Introforras = NULL;
	Introszovegverzio = -1;
}

// A picture with one transparent colour (atl), its rows sy.. drawn from
// screen-menu position (x, y); the parts off the screen are left out (the
// GPU must not get negative screen coordinates):
// (each picture its own palette: the GPU reads them only when it runs the list)
static unsigned int __attribute__((aligned(16))) Atlpal[2][256];
static void atlatszorajzol( unsigned char* tex, int w, int h, int x, int y, int atl, int melyik ) {
	const float skx = 480.0f/PORT_MENUX, sky = 272.0f/PORT_MENUY;
	int sx = 0, sy = 0;
	if( y < 0 ) { sy = -y; h -= sy; y = 0; }
	if( x < 0 ) { sx = -x; w -= sx; x = 0; }
	if( y + h > PORT_MENUY ) h = PORT_MENUY - y;
	if( x + w > PORT_MENUX ) w = PORT_MENUX - x;
	if( w <= 0 || h <= 0 )
		return;
	unsigned int* pal = Atlpal[melyik & 1];
	memcpy( pal, Paletta, 256*4 );
	if( atl >= 0 && atl < 256 )
		pal[atl] &= 0x00ffffff;
	sceKernelDcacheWritebackRange( pal, 256*4 );
	sceGuClutLoad( 256/8, pal );
	sceGuEnable( GU_ALPHA_TEST );
	sceGuAlphaFunc( GU_GREATER, 0x7f, 0xff );
	sceGuTexFunc( GU_TFX_REPLACE, GU_TCC_RGBA );
	kiteglalap( tex, sx, sy, w, h, x*skx, y*sky, w*skx, h*sky, 1, PORT_MENUX );
	sceGuDisable( GU_ALPHA_TEST );
	sceGuTexFunc( GU_TFX_REPLACE, GU_TCC_RGB );
	sceGuClutLoad( 256/8, Paletta );
}

struct csucsc { float u, v; float x, y, z; };

// A filled disc, textured with the same part of the texture it covers on
// screen (so it looks like a window into that picture):
static void menukorong( unsigned char* tex, float cx, float cy, float r,
						float skx, float sky, unsigned int szin = 0 ) {
	if( r < 1.0f )
		return;
	int x0 = (int)(cx - r) - 1, y0 = (int)(cy - r) - 1;
	int x1 = (int)(cx + r) + 2, y1 = (int)(cy + r) + 2;
	if( x0 < 0 ) x0 = 0;
	if( y0 < 0 ) y0 = 0;
	if( x1 > PORT_MENUX ) x1 = PORT_MENUX;
	if( y1 > PORT_MENUY ) y1 = PORT_MENUY;
	if( x0 >= x1 || y0 >= y1 )
		return;
	int alap = x0 & ~15; // 16-byte aligned texture start
	int tw = 16, th = 16;
	while( tw < x1-alap && tw < 512 ) tw <<= 1;
	while( th < y1-y0 && th < 512 ) th <<= 1;
	if( tex ) {
		sceGuTexImage( 0, tw, th, PORT_MENUX, tex + y0*PORT_MENUX + alap );
		sceGuTexFlush();
	}
	else {
		sceGuDisable( GU_TEXTURE_2D );
		sceGuColor( szin );
	}
	const int N = 28;
	csucsc* v = (csucsc*)sceGuGetMemory( (N+2)*sizeof( csucsc ) );
	v[0].u = cx - alap; v[0].v = cy - y0;
	v[0].x = cx*skx; v[0].y = cy*sky; v[0].z = 0;
	for( int i = 0; i <= N; i++ ) {
		float a = 6.2831853f*i/N;
		float px = cx + r*cosf( a ), py = cy + r*sinf( a );
		if( px < 0 ) px = 0;
		if( py < 0 ) py = 0;
		if( px > PORT_MENUX-1 ) px = PORT_MENUX-1;
		if( py > PORT_MENUY-1 ) py = PORT_MENUY-1;
		v[i+1].u = px - alap; v[i+1].v = py - y0;
		v[i+1].x = px*skx; v[i+1].y = py*sky; v[i+1].z = 0;
	}
	sceGuDrawArray( GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
					N+2, 0, v );
	if( !tex )
		sceGuEnable( GU_TEXTURE_2D );
}

// Draws the GPU menu (the caller has started the display list and set up
// the palette and the 8-bit texture mode):
static void menurajzol( void ) {
	const float skx = 480.0f/PORT_MENUX, sky = 272.0f/PORT_MENUY;
	if( !Menuintro ) {
		kiteglalap( Menutex[0], 0, 0, PORT_MENUX, PORT_MENUY, 0, 0, 480, 272, 1, PORT_MENUX );
		for( int i = 0; i < Menukorongdb; i++ ) {
			port_korong* k = &Menukorong[i];
			menukorong( Menutex[k->golyobol ? 1 : 0], k->x, k->y, k->r, skx, sky );
		}
	}
	else {
		// The intro: the background colour, the background from row 'elt' down:
		int elt = Introelt;
		if( elt < 0 ) elt = 0;
		if( elt > PORT_MENUY ) elt = PORT_MENUY;
		sceGuClearColor( Paletta[Introhatterszin & 0xff] );
		sceGuClear( GU_COLOR_BUFFER_BIT );
		int ey = (int)(elt*sky + 0.5f);
		if( elt < PORT_MENUY )
			kiteglalap( Menutex[0], 0, 0, PORT_MENUX, PORT_MENUY-elt, 0, elt*sky,
						480, (PORT_MENUY-elt)*sky, 1, PORT_MENUX );
		// The balls show the menu pictures where they are on the screen; above
		// the sliding background they are plain (green, or the background
		// colour for the small ones), as in the original:
		for( int resz = 0; resz < 2; resz++ ) {
			if( resz == 0 ) {
				if( ey <= 0 ) continue;
				sceGuScissor( 0, 0, 480, ey );
			}
			else {
				if( ey >= 272 ) continue;
				sceGuScissor( 0, ey, 480, 272-ey );
			}
			for( int i = 0; i < Menukorongdb; i++ ) {
				port_korong* k = &Menukorong[i];
				if( resz == 1 )
					menukorong( Menutex[k->golyobol ? 1 : 0], k->x, k->y, k->r, skx, sky );
				else
					menukorong( NULL, k->x, k->y, k->r, skx, sky,
								Paletta[k->golyobol ? 248 : (Introhatterszin & 0xff)] );
			}
		}
		sceGuScissor( 0, 0, 480, 272 );
		if( Introszovegtex )
			atlatszorajzol( Introszovegtex, PORT_MENUX, PORT_MENUY, 0, -Introelt, Introszovegatl, 0 );
	}
	if( Sisakw > 0 && Sisakh > 0 ) {
		int tw = 16, th = 16;
		while( tw < Sisakw+2 ) tw <<= 1;
		while( th < Sisakh+2 ) th <<= 1;
		sceGuClutLoad( 256/8, Sisakpal );
		sceGuEnable( GU_ALPHA_TEST );
		// Only pixels that are mostly helmet: the smoothed edge would
		// otherwise show the hidden transparent colour as a thin line
		sceGuAlphaFunc( GU_GREATER, 0x7f, 0xff );
		sceGuTexFunc( GU_TFX_REPLACE, GU_TCC_RGBA );
		sceGuTexFilter( GU_LINEAR, GU_LINEAR );
		sceGuTexImage( 0, tw, th, SISAKMAX, Sisaktex );
		sceGuTexFlush();
		csucs* v = (csucs*)sceGuGetMemory( 2*sizeof( csucs ) );
		v[0].u = 1; v[0].v = 1;
		v[0].x = Sisakx*skx; v[0].y = Sisaky*sky; v[0].z = 0;
		v[1].u = 1+Sisakw; v[1].v = 1+Sisakh;
		v[1].x = (Sisakx+Sisakw)*skx; v[1].y = (Sisaky+Sisakh)*sky; v[1].z = 0;
		sceGuDrawArray( GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
						2, 0, v );
		sceGuDisable( GU_ALPHA_TEST );
		sceGuTexFunc( GU_TFX_REPLACE, GU_TCC_RGB );
		sceGuClutLoad( 256/8, Paletta );
	}
	if( Menuintro && Introkeptex && Introkepatl >= -1 && Introkepw > 0 )
		atlatszorajzol( Introkeptex, Introkepw, Introkeph, Introkepx, Introkepy, Introkepatl, 1 );
}


// ---------------------------------------------------------------------------
// GPU gameplay drawing (see port_gpu_* in port.h)
// ---------------------------------------------------------------------------
enum { GP_NEZET, GP_NEGYSZOG };
// GP_NEZET: a..d the view; e, f: its mask boxes are Gpdoboz[e..f-1]
struct gpparancs { int tipus, a, b, c, d, e, f; };
struct gpnegyszog { float xy[8], uv[8]; unsigned char* tex; int xsize, ysize; int csoport; unsigned char lyuk; };
#define GPMAXPARANCS (512)
#define GPMAXNEGYSZOG (256)
static gpparancs Gpparancs[GPMAXPARANCS];
static gpnegyszog Gpnegyszog[GPMAXNEGYSZOG];
static int Gpparancsdb = 0, Gpnegyszogdb = 0;
static int Gpjatek = -1;        // -1: not decided yet
static int Gpvan = 0;           // the frame in the back buffer has GPU items
static int Palettaverzio = 0;   // changes whenever the palette changes
// Palettes with one colour transparent (the "hole" colour of a bike part):
static unsigned int* Lyukpal[256];
static int Lyukpalverzio[256];
// The mask: 1 where the CPU picture must stay in front of the bikes (the
// foreground, the minimap, the timer). Only the box around the bikes of the
// view being drawn is kept up to date:
// One for each screen buffer: the GPU may still read the one of the frame
// shown while the CPU makes the next.
static unsigned char* Maszkok[2] = { NULL, NULL }; // (both the same one, see port_gpu_maszk)
static unsigned char* Maszk = NULL; // the one of the frame being drawn
// One box around each bike (x1, y1, x2, y2), those of the view being drawn
// are Gpdoboz[Dobozkezd..Gpdobozdb-1]:
#define GPMAXDOBOZ (16)
static int Gpdoboz[GPMAXDOBOZ][4];
static int Gpdobozdb = 0, Dobozkezd = 0;
static int Nezetpar = -1;                         // the view's GP_NEZET command
static int Gpcsoport = 0;                         // the bike being recorded
static unsigned int __attribute__((aligned(16))) Maszkpal[256];

int port_gpujatek( void ) {
	if( Gpjatek < 0 ) {
		Gpjatek = !port_cpurajz();
		if( Gpjatek ) {
			// One mask (memory is short): port_gpu_maszk waits until the GPU
			// has finished with it before writing it
			Maszkok[0] = Maszkok[1] = (unsigned char*)memalign( 64, BUFW*480 + 1024 );
			if( !Maszkok[0] )
				Gpjatek = 0;
			else
				memset( Maszkok[0], 0, BUFW*480 + 1024 );
			Maszk = Maszkok[1-Elso];
			memset( Maszkpal, 0, sizeof( Maszkpal ) );
			Maszkpal[1] = 0xff000000;
		}
	}
	return Gpjatek;
}

static void gp_uj( void ) {
	Gpparancsdb = Gpnegyszogdb = 0;
	Nezetpar = -1;
	Gpdobozdb = Dobozkezd = 0;
}

static gpparancs* gp_parancs( int tipus ) {
	if( Gpparancsdb >= GPMAXPARANCS )
		return NULL;
	gpparancs* p = &Gpparancs[Gpparancsdb++];
	p->tipus = tipus;
	return p;
}

void port_gpu_nezet( int x1, int y1, int x2, int y2 ) {
	Dobozkezd = Gpdobozdb;
	Nezetpar = -1;
	gpparancs* p = gp_parancs( GP_NEZET );
	if( !p ) return;
	p->a = x1; p->b = y1; p->c = x2; p->d = y2;
	p->e = p->f = Gpdobozdb;
	Nezetpar = Gpparancsdb-1;
}

void port_gpu_motor( void ) {
	Gpcsoport++;
}

void port_gpu_negyszog( const float* xy, const float* uv,
						unsigned char* tex, int xsize, int ysize, unsigned char lyuk ) {
	if( Gpnegyszogdb >= GPMAXNEGYSZOG || Nezetpar < 0 )
		return;
	// Parts outside the view are left out:
	gpparancs* n = &Gpparancs[Nezetpar];
	float x1 = xy[0], x2 = xy[0], y1 = xy[1], y2 = xy[1];
	for( int k = 1; k < 4; k++ ) {
		if( xy[2*k] < x1 ) x1 = xy[2*k];
		if( xy[2*k] > x2 ) x2 = xy[2*k];
		if( xy[2*k+1] < y1 ) y1 = xy[2*k+1];
		if( xy[2*k+1] > y2 ) y2 = xy[2*k+1];
	}
	if( x2 < n->a || x1 > n->c+1 || y2 < n->b || y1 > n->d+1 )
		return;
	gpparancs* p = gp_parancs( GP_NEGYSZOG );
	if( !p ) return;
	gpnegyszog* q = &Gpnegyszog[Gpnegyszogdb];
	p->a = Gpnegyszogdb++;
	memcpy( q->xy, xy, sizeof( q->xy ) );
	memcpy( q->uv, uv, sizeof( q->uv ) );
	q->tex = tex;
	q->xsize = xsize;
	q->ysize = ysize;
	q->lyuk = lyuk;
	q->csoport = Gpcsoport;
}

void port_gpu_maszk( void ) {
	Dobozkezd = Gpdobozdb;
	if( Nezetpar < 0 || !Maszk )
		return;
	gpuvar(); // the GPU may still be reading the mask (it has nearly always finished)
	gpparancs* n = &Gpparancs[Nezetpar];
	// A box around the parts of each bike:
	int i = Nezetpar+1;
	while( i < Gpparancsdb && Gpdobozdb < GPMAXDOBOZ ) {
		if( Gpparancs[i].tipus != GP_NEGYSZOG ) {
			i++;
			continue;
		}
		int csoport = Gpnegyszog[Gpparancs[i].a].csoport;
		float x1 = 1e9f, y1 = 1e9f, x2 = -1e9f, y2 = -1e9f;
		for( ; i < Gpparancsdb; i++ ) {
			if( Gpparancs[i].tipus != GP_NEGYSZOG )
				continue;
			gpnegyszog* q = &Gpnegyszog[Gpparancs[i].a];
			if( q->csoport != csoport )
				break;
			for( int k = 0; k < 4; k++ ) {
				if( q->xy[2*k] < x1 ) x1 = q->xy[2*k];
				if( q->xy[2*k] > x2 ) x2 = q->xy[2*k];
				if( q->xy[2*k+1] < y1 ) y1 = q->xy[2*k+1];
				if( q->xy[2*k+1] > y2 ) y2 = q->xy[2*k+1];
			}
		}
		// A little more, for the smoothing of the edges:
		int ix1 = (int)x1 - 3, iy1 = (int)y1 - 3, ix2 = (int)x2 + 3, iy2 = (int)y2 + 3;
		if( ix1 < n->a ) ix1 = n->a;
		if( iy1 < n->b ) iy1 = n->b;
		if( ix2 > n->c ) ix2 = n->c;
		if( iy2 > n->d ) iy2 = n->d;
		if( ix2 > BUFW-1 ) ix2 = BUFW-1;
		if( iy2 > 479 ) iy2 = 479;
		if( ix1 < 0 ) ix1 = 0;
		if( iy1 < 0 ) iy1 = 0;
		if( ix1 > ix2 || iy1 > iy2 )
			continue;
		int* d = Gpdoboz[Gpdobozdb++];
		d[0] = ix1; d[1] = iy1; d[2] = ix2; d[3] = iy2;
		for( int y = iy1; y <= iy2; y++ )
			memset( Maszk + y*BUFW + ix1, 0, ix2-ix1+1 );
	}
	n->f = Gpdobozdb;
}

int port_gpu_maszksor( int y ) {
	for( int i = Dobozkezd; i < Gpdobozdb; i++ )
		if( y >= Gpdoboz[i][1] && y <= Gpdoboz[i][3] )
			return 1;
	return 0;
}

void port_gpu_jelol( int x, int y, int n ) {
	for( int i = Dobozkezd; i < Gpdobozdb; i++ ) {
		int* d = Gpdoboz[i];
		if( y < d[1] || y > d[3] )
			continue;
		int xa = x, xb = x+n-1;
		if( xa < d[0] ) xa = d[0];
		if( xb > d[2] ) xb = d[2];
		if( xa <= xb )
			memset( Maszk + y*BUFW + xa, 1, xb-xa+1 );
	}
}

void port_gpu_jelolteglalap( int x1, int y1, int x2, int y2 ) {
	for( int y = y1; y <= y2; y++ )
		port_gpu_jelol( x1, y, x2-x1+1 );
}

static unsigned int* lyukpaletta( unsigned char lyuk ) {
	if( !Lyukpal[lyuk] ) {
		Lyukpal[lyuk] = (unsigned int*)memalign( 16, 256*4 );
		Lyukpalverzio[lyuk] = -1;
	}
	if( Lyukpalverzio[lyuk] != Palettaverzio ) {
		memcpy( Lyukpal[lyuk], Paletta, 256*4 );
		Lyukpal[lyuk][lyuk] &= 0x00ffffff; // transparent
		Lyukpalverzio[lyuk] = Palettaverzio;
	}
	return Lyukpal[lyuk];
}

// ---------------------------------------------------------------------------
// The bike pictures for the GPU: full colour copies at 1/2, 1/4, 1/8 ... of
// the size (each pixel the average of four, the transparent ones left out),
// made the first time a picture is drawn. A bike part is drawn from the copy
// nearest to its size on the screen: drawing a big picture much smaller made
// the GPU read it all over (from main memory, while the CPU was using it),
// which was very slow. The small copies are in video memory if there is room.
// ---------------------------------------------------------------------------
int Port_kiskepverzio = 0;
#define MAXSZINT (8)
struct gpkep {
	unsigned char* tex;           // the picture (the key)
	int szintdb;                  // copies 1..szintdb-1 (0 is the 8-bit picture itself)
	unsigned int* szint[MAXSZINT];
	int w[MAXSZINT], h[MAXSZINT], tbw[MAXSZINT];
	int ramban[MAXSZINT];         // allocated in main memory (to be freed)
};
#define MAXGPKEP (64)
static gpkep Gpkepek[MAXGPKEP];
static int Gpkepdb = 0;
static int Gpkepverzio = -1;
static unsigned int Gpkeppal = 0;          // the palette the copies were made with (its hash)
static int Paletahashverzio = -1;
static unsigned int Paletahash = 0;        // of Paletta

static unsigned int palhash( const unsigned int* pal ) {
	unsigned int h = 2166136261u;
	for( int i = 0; i < 256; i++ ) {
		h ^= pal[i];
		h *= 16777619u;
	}
	return h | 1; // never 0
}

static void gpkepurit( void ) {
	for( int i = 0; i < Gpkepdb; i++ )
		for( int k = 1; k < Gpkepek[i].szintdb; k++ )
			if( Gpkepek[i].ramban[k] )
				free( Gpkepek[i].szint[k] );
	Gpkepdb = 0;
	Vramtarfoglalt = 0;
}

static unsigned int* gpkepfoglal( int bajt, int* ramban ) {
	bajt = (bajt + 63) & ~63;
	if( Vramtar + Vramtarfoglalt + bajt <= Vramtarvege ) {
		unsigned int* p = (unsigned int*)( (unsigned char*)( 0x40000000 | (uintptr_t)sceGeEdramGetAddr() )
										   + Vramtar + Vramtarfoglalt );
		Vramtarfoglalt += bajt;
		*ramban = 0;
		return p;
	}
	*ramban = 1;
	return (unsigned int*)memalign( 64, bajt );
}

// The copies for a palette: made again when the pictures or the colours change
// (not when the menus' palette comes and goes):
static void gpkeppaletta( const unsigned int* pal, unsigned int hash ) {
	if( Gpkepverzio != Port_kiskepverzio || Gpkeppal != hash ) {
		gpuvar(); // the GPU may still be drawing from the old copies
		gpkepurit();
		Gpkepverzio = Port_kiskepverzio;
		Gpkeppal = hash;
	}
}

static gpkep* gpkepkeszit( unsigned char* tex, int xsize, int ysize, unsigned char lyuk,
						   const unsigned int* pal );

static gpkep* gpkepkeres( gpnegyszog* q ) {
	if( Paletahashverzio != Palettaverzio ) {
		Paletahash = palhash( Paletta );
		Paletahashverzio = Palettaverzio;
	}
	gpkeppaletta( Paletta, Paletahash );
	for( int i = 0; i < Gpkepdb; i++ )
		if( Gpkepek[i].tex == q->tex )
			return &Gpkepek[i];
	return gpkepkeszit( q->tex, q->xsize, q->ysize, q->lyuk, Paletta );
}

void port_gpukep_elore( unsigned char* tex, int xsize, int ysize, unsigned char lyuk,
						const void* paletta ) {
	if( !tex || !paletta || !Buff[0] || port_cpurajz() )
		return;
	const unsigned int* pal = (const unsigned int*)paletta;
	gpkeppaletta( pal, palhash( pal ) );
	for( int i = 0; i < Gpkepdb; i++ )
		if( Gpkepek[i].tex == tex )
			return;
	gpkepkeszit( tex, xsize, ysize, lyuk, pal );
}

static gpkep* gpkepkeszit( unsigned char* tex, int xsize, int ysize, unsigned char lyuk,
						   const unsigned int* pal ) {
	// stands for the picture in the code below:
	gpnegyszog qq;
	qq.tex = tex; qq.xsize = xsize; qq.ysize = ysize; qq.lyuk = lyuk;
	gpnegyszog* q = &qq;
	if( Gpkepdb >= MAXGPKEP )
		gpkepurit();
	gpkep* g = &Gpkepek[Gpkepdb++];
	g->tex = q->tex;
	g->szint[0] = NULL;
	g->w[0] = q->xsize; g->h[0] = q->ysize; g->tbw[0] = 256;
	g->ramban[0] = 0;
	// The full size picture in colour, transparent pixels with alpha 0:
	int w = q->xsize, h = q->ysize;
	unsigned int* elozo = (unsigned int*)malloc( w*h*4 );
	if( !elozo ) { g->szintdb = 1; return g; }
	for( int y = 0; y < h; y++ )
		for( int x = 0; x < w; x++ ) {
			unsigned char c = q->tex[y*256+x];
			elozo[y*w+x] = c == q->lyuk ? 0 : (pal[c] | 0xff000000u);
		}
	int ew = w, eh = h, etbw = w;
	unsigned int* eredeti = elozo;
	int k = 1;
	while( k < MAXSZINT && (ew > 1 || eh > 1) ) {
		int nw = (ew+1)/2, nh = (eh+1)/2;
		int tbw = (nw + 3) & ~3; // 16 bytes
		int ramban = 1;
		// The half size copy is only a step to the smaller ones (memory is
		// short; a part drawn that big comes from the picture itself):
		unsigned int* uj = k == 1 ? (unsigned int*)memalign( 64, tbw*nh*4 )
								  : gpkepfoglal( tbw*nh*4, &ramban );
		if( !uj ) break;
		for( int y = 0; y < nh; y++ )
			for( int x = 0; x < tbw; x++ ) {
				unsigned int r = 0, gg = 0, b = 0, db = 0, osszes = 0;
				if( x < nw )
					for( int dy = 0; dy < 2; dy++ )
						for( int dx = 0; dx < 2; dx++ ) {
							int sx = 2*x+dx, sy = 2*y+dy;
							if( sx >= ew || sy >= eh ) continue;
							unsigned int c = elozo[sy*etbw+sx];
							osszes++;
							unsigned int a = c >> 24;
							if( !a ) continue;
							// weighted by alpha: transparent pixels add no colour
							r += (c & 0xff)*a; gg += ((c >> 8) & 0xff)*a; b += ((c >> 16) & 0xff)*a;
							db += a;
						}
				unsigned int ki = 0;
				if( db ) {
					unsigned int alfa = osszes ? db/osszes : 0;
					ki = (alfa << 24) | ((b/db) << 16) | ((gg/db) << 8) | (r/db);
				}
				uj[y*tbw+x] = ki;
			}
		g->szint[k] = uj; g->w[k] = nw; g->h[k] = nh; g->tbw[k] = tbw; g->ramban[k] = ramban;
		if( k == 2 ) { // the half size one is not kept
			free( g->szint[1] );
			g->szint[1] = NULL;
			g->ramban[1] = 0;
		}
		elozo = uj; ew = nw; eh = nh; etbw = tbw;
		k++;
	}
	if( k == 2 && g->szint[1] ) { // only a half size one was made: not kept
		free( g->szint[1] );
		g->szint[1] = NULL;
		g->ramban[1] = 0;
		k = 1;
	}
	g->szintdb = k;
	free( eredeti );
	return g;
}

static int ketto_hatvany( int n ) {
	int t = 1;
	while( t < n ) t <<= 1;
	return t;
}

// Draws the GPU items of the frame; the buffer picture (bx x by) is already
// drawn scaled to 480x272:
static void gp_rajzol( unsigned char* buff, int bx, int by ) {
	const float skx = 480.0f/bx, sky = 272.0f/by;
	unsigned char* maszk = Maszkok[Elso];
	if( !maszk )
		return;
	int aktlyuk = -1;
	for( int i = 0; i < Gpparancsdb; i++ ) {
		gpparancs* p = &Gpparancs[i];
		switch( p->tipus ) {
			case GP_NEZET: {
				sceGuDisable( GU_STENCIL_TEST );
				if( p->e >= p->f ) {
					// No bikes in this view: nothing to draw until the next one
					sceGuScissor( 0, 0, 0, 0 );
					break;
				}
				// The view on the PSP screen (sceGuScissor takes x, y, w, h):
				int x1 = (int)(p->a*skx), y1 = (int)(p->b*sky);
				int x2 = (int)((p->c+1)*skx+0.999f), y2 = (int)((p->d+1)*sky+0.999f);
				if( x2 > 480 ) x2 = 480;
				if( y2 > 272 ) y2 = 272;
				sceGuScissor( x1, y1, x2-x1, y2-y1 );
				// The mask goes into the PSP's stencil (the alpha of the screen):
				// 0xff in the view, then in each box 0, and 0xff where the mask
				// is set. The bikes are drawn where it is 0:
				sceGuPixelMask( 0x00ffffff ); // the colours stay
				sceGuEnable( GU_STENCIL_TEST );
				sceGuStencilFunc( GU_ALWAYS, 0xff, 0xff );
				sceGuStencilOp( GU_REPLACE, GU_REPLACE, GU_REPLACE );
				sceGuDisable( GU_TEXTURE_2D );
				{
					csucs* v = (csucs*)sceGuGetMemory( 2*sizeof( csucs ) );
					v[0].u = 0; v[0].v = 0; v[0].x = x1; v[0].y = y1; v[0].z = 0;
					v[1].u = 0; v[1].v = 0; v[1].x = x2; v[1].y = y2; v[1].z = 0;
					sceGuDrawArray( GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
									2, 0, v );
				}
				sceGuEnable( GU_TEXTURE_2D );
				sceGuClutLoad( 256/8, Maszkpal );
				aktlyuk = -2;
				sceGuTexFunc( GU_TFX_REPLACE, GU_TCC_RGBA );
				for( int d = p->e; d < p->f; d++ ) {
					int* b = Gpdoboz[d];
					int w = b[2]-b[0]+1, h = b[3]-b[1]+1;
					sceGuStencilFunc( GU_ALWAYS, 0, 0xff );
					sceGuStencilOp( GU_ZERO, GU_ZERO, GU_ZERO );
					kiteglalap( maszk, b[0], b[1], w, h, b[0]*skx, b[1]*sky, w*skx, h*sky, 0 );
					sceGuEnable( GU_ALPHA_TEST );
					sceGuAlphaFunc( GU_GREATER, 0x7f, 0xff );
					sceGuStencilFunc( GU_ALWAYS, 0xff, 0xff );
					sceGuStencilOp( GU_KEEP, GU_KEEP, GU_REPLACE );
					kiteglalap( maszk, b[0], b[1], w, h, b[0]*skx, b[1]*sky, w*skx, h*sky, 0 );
					sceGuDisable( GU_ALPHA_TEST );
				}
				sceGuPixelMask( 0 );
				sceGuTexFunc( GU_TFX_REPLACE, GU_TCC_RGB );
				sceGuStencilFunc( GU_EQUAL, 0, 0xff );
				sceGuStencilOp( GU_KEEP, GU_KEEP, GU_KEEP );
				break;
			}
			case GP_NEGYSZOG: {
				gpnegyszog* q = &Gpnegyszog[p->a];
				gpkep* g = gpkepkeres( q );
				// How many picture pixels fall on one screen pixel:
				float ux = (q->xy[2]-q->xy[0])*skx, uy = (q->xy[3]-q->xy[1])*sky;
				float vx = (q->xy[4]-q->xy[0])*skx, vy = (q->xy[5]-q->xy[1])*sky;
				float hu = sqrtf( ux*ux+uy*uy ), hv = sqrtf( vx*vx+vy*vy );
				float aranyu = hu > 0.5f ? q->xsize/hu : 64.0f;
				float aranyv = hv > 0.5f ? q->ysize/hv : 64.0f;
				float arany = aranyu > aranyv ? aranyu : aranyv;
				int k = 0;
				while( k+1 < g->szintdb && arany >= 2.0f ) {
					arany *= 0.5f;
					k++;
				}
				if( k == 1 )
					k = 0; // (no half size copy: the picture itself)
				float osztas = (float)(1 << k);
				if( k == 0 ) {
					// The 8-bit picture itself (drawn at about its size or bigger)
					if( aktlyuk != q->lyuk ) {
						sceGuClutLoad( 256/8, lyukpaletta( q->lyuk ) );
						aktlyuk = q->lyuk;
					}
					sceGuTexMode( GU_PSM_T8, 0, 0, 0 );
					sceGuTexImage( 0, 256, ketto_hatvany( q->ysize < 16 ? 16 : q->ysize ), 256, q->tex );
				}
				else {
					sceGuTexMode( GU_PSM_8888, 0, 0, 0 );
					sceGuTexImage( 0, ketto_hatvany( g->w[k] ), ketto_hatvany( g->h[k] ),
								   g->tbw[k], g->szint[k] );
				}
				sceGuEnable( GU_ALPHA_TEST );
				sceGuAlphaFunc( GU_GREATER, 0x7f, 0xff );
				sceGuTexFunc( GU_TFX_REPLACE, GU_TCC_RGBA );
				sceGuTexFilter( GU_LINEAR, GU_LINEAR );
				sceGuTexFlush();
				csucs* v = (csucs*)sceGuGetMemory( 4*sizeof( csucs ) );
				for( int i2 = 0; i2 < 4; i2++ ) {
					v[i2].u = q->uv[2*i2]/osztas;
					v[i2].v = q->uv[2*i2+1]/osztas;
					v[i2].x = q->xy[2*i2]*skx;
					v[i2].y = q->xy[2*i2+1]*sky;
					v[i2].z = 0;
				}
				sceGuDrawArray( GU_TRIANGLE_STRIP, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
								4, 0, v );
				sceGuDisable( GU_ALPHA_TEST );
				sceGuTexFunc( GU_TFX_REPLACE, GU_TCC_RGB );
				sceGuTexMode( GU_PSM_T8, 0, 0, 0 );
				break;
			}
		}
	}
	sceGuDisable( GU_STENCIL_TEST );
	if( aktlyuk != -1 )
		sceGuClutLoad( 256/8, Paletta );
	sceGuScissor( 0, 0, 480, 272 );
}

// The game's 8-bit picture is copied into video memory before the GPU draws
// it: reading it from main memory, a few bytes at a time, was very slow
// (and slower still while the CPU was busy with that memory itself).
#define VRAMKEP (0x110000) // BUFW x 480 bytes, up to 0x173600

static unsigned char* vramba( unsigned char* buff, int bx, int by ) {
	if( Buffvramban || port_cpurajz() )
		return buff; // already there (cpurajz.inf: drawn from main memory, as in v1.0)
	unsigned char* v = (unsigned char*)sceGeEdramGetAddr() + VRAMKEP;
	if( by > 480 )
		by = 480;
	// One block copy by the GPU, as 16-bit pixels (two bytes each):
	sceGuCopyImage( GU_PSM_4444, 0, 0, (bx+1)/2, by, BUFW/2, buff, 0, 0, BUFW/2, v );
	sceGuTexSync();
	return v;
}

// Renders the current front buffer. Normally it goes onto the screen by
// itself when the GPU is done ('kozvetlen': the caller shows it with
// keretvege, after drawing something over it, e.g. a system dialog).
static SceUInt64 Utolsovaras = 0; // the time keretrajzol waited
static void keretrajzol( int kozvetlen = 0 ) {
	Utolsovaras = fbvalaszt();
	unsigned char* buff = Buff[Elso];
	// The CPU's data cache is only 16 KB: writing all of it back is much
	// cheaper than walking the whole screen buffer line by line:
	sceKernelDcacheWritebackAll();

	sceGuStart( GU_DIRECT, Lista );
	sceGuDrawBufferList( GU_PSM_8888, FBCIM( Rajzfb ), 512 );
	// Restate everything we rely on (system dialogs draw with their own state):
	sceGuEnable( GU_TEXTURE_2D );
	sceGuDisable( GU_BLEND );
	sceGuDisable( GU_DEPTH_TEST );
	sceGuDepthMask( GU_TRUE ); // no depth writes
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
	if( Menugpu && Menutex[0] ) {
		menurajzol();
		// The timing overlay is drawn into the screen buffer: show that part
		if( Prof_w > 0 && Prof_h > 0 )
			kiteglalap( buff, 0, 0, Prof_w, Prof_h, 0, 0,
						Prof_w*480.0f/PORT_MENUX, Prof_h*272.0f/PORT_MENUY, 1 );
	}
	else if( Nagyitas && mostszerkeszto() && bx == PORT_EDX && by == PORT_EDY ) {
		buff = vramba( buff, bx, by );
		kiteglalap( buff, Nezx, Nezy, 480, 272, 0, 0, 480, 272, 0 );
	}
	else {
		buff = vramba( buff, bx, by );
		int simit = !(bx == 480 && by == 272);
		kiteglalap( buff, 0, 0, bx, by, 0, 0, 480, 272, simit );
		sceGuSignal( GU_SIGNAL_NOWAIT, 1 );
		if( Gpvan ) {
			gp_rajzol( buff, bx, by ); // the bikes, behind the masked parts
			sceGuSignal( GU_SIGNAL_NOWAIT, 2 );
			// The timing overlay is drawn into the screen buffer: show that part
			if( Prof_w > 0 && Prof_h > 0 )
				kiteglalap( buff, 0, 0, Prof_w, Prof_h, 0, 0,
							Prof_w*480.0f/bx, Prof_h*272.0f/by, 1 );
		}
	}

	sceGuFinish();
	if( kozvetlen || !Mutatovan ) {
		Gufut = 1;
		if( !kozvetlen ) { // no thread: shown the simple way
			gpuvar();
			fbkitesz();
		}
	}
	else {
		Gufut = 2; // no waiting here: the thread shows it
		Gpuindul = sceKernelGetSystemTimeWide();
		sceKernelSignalSema( Inditsema, 1 );
	}
}

// Shows a frame drawn with keretrajzol( 1 ):
static void keretvege( void ) {
	gpuvar();
	sceGuSync( 0, 0 );
	fbkitesz();
	sceDisplayWaitVblankStart();
}

#ifdef PORT_TESZT
// ---------------------------------------------------------------------------
// Test mode (build with -DELMA_TEST=ON): reads teszt.txt from the game folder,
// one command per line, times in milliseconds from the start:
//   <ms> press <BUTTON> <ms held>   (CROSS CIRCLE SQUARE TRIANGLE UP DOWN LEFT
//                                    RIGHT L R START SELECT)
//   <ms> shot <file.bmp>            saves the next shown picture (8-bit BMP)
//   <ms> quit
// ---------------------------------------------------------------------------
struct tesztparancs { unsigned int ido, gomb, tartas; char fajl[40]; int tipus; };
static tesztparancs Teszt[400];
static int Tesztdb = 0, Tesztkov = 0;
static SceUInt64 Tesztkezd = 0;
static unsigned int Tesztgombok = 0;
static unsigned int Tesztvege[16];
static char Tesztshot[40] = "";

static unsigned int tesztgomb( const char* n ) {
	static const char* nevek[] = { "CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "UP", "DOWN",
		"LEFT", "RIGHT", "L", "R", "START", "SELECT" };
	static const unsigned int gombok[] = { PSP_CTRL_CROSS, PSP_CTRL_CIRCLE, PSP_CTRL_SQUARE,
		PSP_CTRL_TRIANGLE, PSP_CTRL_UP, PSP_CTRL_DOWN, PSP_CTRL_LEFT, PSP_CTRL_RIGHT,
		PSP_CTRL_LTRIGGER, PSP_CTRL_RTRIGGER, PSP_CTRL_START, PSP_CTRL_SELECT };
	for( int i = 0; i < 12; i++ )
		if( strcmp( n, nevek[i] ) == 0 )
			return gombok[i];
	return 0;
}

static void tesztbetolt( void ) {
	FILE* h = fopen( "teszt.txt", "rt" );
	if( !h )
		return;
	char sor[120];
	while( Tesztdb < 400 && fgets( sor, sizeof( sor ), h ) ) {
		tesztparancs* p = &Teszt[Tesztdb];
		char cmd[20] = "", a1[40] = "";
		unsigned int a2 = 0;
		if( sscanf( sor, "%u %19s %39s %u", &p->ido, cmd, a1, &a2 ) < 2 )
			continue;
		p->tipus = 0;
		if( strcmp( cmd, "press" ) == 0 ) { p->tipus = 1; p->gomb = tesztgomb( a1 ); p->tartas = a2 ? a2 : 100; }
		if( strcmp( cmd, "shot" ) == 0 )  { p->tipus = 2; strncpy( p->fajl, a1, 39 ); p->fajl[39] = 0; }
		if( strcmp( cmd, "quit" ) == 0 )  p->tipus = 3;
		if( p->tipus )
			Tesztdb++;
	}
	fclose( h );
	Tesztkezd = sceKernelGetSystemTimeWide();
}

static void tesztlep( void ) {
	if( !Tesztkezd )
		return;
	unsigned int most = (unsigned int)((sceKernelGetSystemTimeWide()-Tesztkezd)/1000);
	while( Tesztkov < Tesztdb && Teszt[Tesztkov].ido <= most ) {
		tesztparancs* p = &Teszt[Tesztkov++];
		printf( "teszt %u ms: %d %x %s\n", most, p->tipus, p->gomb, p->fajl );
		if( p->tipus == 1 ) {
			Tesztgombok |= p->gomb;
			for( int b = 0; b < 16; b++ )
				if( p->gomb & (1u << b) )
					Tesztvege[b] = most + p->tartas;
		}
		if( p->tipus == 2 )
			strcpy( Tesztshot, p->fajl );
		if( p->tipus == 3 ) {
			sceKernelExitGame();
		}
	}
	for( int b = 0; b < 16; b++ )
		if( (Tesztgombok & (1u << b)) && most >= Tesztvege[b] )
			Tesztgombok &= ~(1u << b);
}

// 8-bit BMP of the picture being shown (the game's own drawing, before scaling):
static void tesztkep( unsigned char* buff, int bx, int by ) {
	if( !Tesztshot[0] )
		return;
	FILE* h = fopen( Tesztshot, "wb" );
	Tesztshot[0] = 0;
	if( !h )
		return;
	int sorhossz = (bx+3) & ~3;
	unsigned int meret = 54 + 1024 + sorhossz*by;
	unsigned char fej[54];
	memset( fej, 0, 54 );
	fej[0] = 'B'; fej[1] = 'M';
	memcpy( fej+2, &meret, 4 );
	unsigned int ofs = 54+1024; memcpy( fej+10, &ofs, 4 );
	unsigned int hdr = 40; memcpy( fej+14, &hdr, 4 );
	memcpy( fej+18, &bx, 4 ); memcpy( fej+22, &by, 4 );
	fej[26] = 1; fej[28] = 8;
	unsigned int szinek = 256; memcpy( fej+46, &szinek, 4 );
	fwrite( fej, 1, 54, h );
	for( int i = 0; i < 256; i++ ) {
		unsigned int c = Paletta[i];
		unsigned char e[4] = { (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c, 0 };
		fwrite( e, 1, 4, h );
	}
	unsigned char nulla[4] = { 0, 0, 0, 0 };
	for( int y = by-1; y >= 0; y-- ) {
		fwrite( buff + y*BUFW, 1, bx, h );
		fwrite( nulla, 1, sorhossz-bx, h );
	}
	fclose( h );
}
#endif

#ifdef PORT_PROFIL
// ---------------------------------------------------------------------------
// Timing overlay. Per frame (averaged over 30 frames), in milliseconds:
//   DRAW  the game drawing into the screen buffer (gameplay: the whole view)
//   PHYS  the physics steps (gameplay only)
//   GPU   handing the picture to the GPU (cache writeback, scaling, palette)
//   WAIT  waiting for the vertical blank: time to spare
//   REST  everything else (game logic, sound mixing, menu drawing)
// ---------------------------------------------------------------------------
static SceUInt64 Prof_lockkezd = 0, Prof_fizkezd = 0, Prof_utolso = 0;
static SceUInt64 Prof_rajz = 0, Prof_fiz = 0, Prof_gpu = 0, Prof_varas = 0, Prof_osszes = 0;
static int Prof_db = 0;
static char Prof_szoveg[240] = "";

void port_fizkezd( void ) { Prof_fizkezd = sceKernelGetSystemTimeWide(); }
void port_fizvege( void ) { Prof_fiz += sceKernelGetSystemTimeWide() - Prof_fizkezd; }
static void prof_lockkezd( void ) { Prof_lockkezd = sceKernelGetSystemTimeWide(); }
static void prof_lockvege( void ) { Prof_rajz += sceKernelGetSystemTimeWide() - Prof_lockkezd; }

// 3x5 font for the overlay text (bit 14 = top left):
static const char* Prof_betuk = "0123456789.:ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const unsigned short Prof_minta[] = {
	0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF,
	0x0002, 0x0410, 0x2BED, 0x6BAE, 0x3923, 0x6B6E, 0x79E7, 0x79E4, 0x396B, 0x5BED,
	0x7497, 0x126A, 0x5BAD, 0x4927, 0x5FED, 0x6B6D, 0x2B6A, 0x6BA4, 0x2B73, 0x6BAD,
	0x388E, 0x7492, 0x5B6F, 0x5B6A, 0x5BFD, 0x5AAD, 0x5A92, 0x72A7,
};

// Parts of DRAW in gameplay (kiraj320.cpp marks them):
#define PROF_RESZDB (6)
static SceUInt64 Prof_resz[PROF_RESZDB], Prof_reszkezd = 0;
void port_reszkezd( void ) { Prof_reszkezd = sceKernelGetSystemTimeWide(); }
static SceUInt64 Prof_pix = 0, Prof_pixkezd = 0;
void port_pixkezd( void ) { Prof_pixkezd = sceKernelGetSystemTimeWide(); }
void port_pixvege( void ) { Prof_pix += sceKernelGetSystemTimeWide() - Prof_pixkezd; }
void port_reszvege( int i ) {
	if( i >= 0 && i < PROF_RESZDB )
		Prof_resz[i] += sceKernelGetSystemTimeWide() - Prof_reszkezd;
}

static void prof_kiir( unsigned char* buff, int bx, int by ) {
	if( !Prof_szoveg[0] )
		return;
	// Darkest and brightest colours of the current palette:
	int sotet = 0, vilagos = 0, smin = 1000, smax = -1;
	for( int i = 0; i < 256; i++ ) {
		unsigned int c = Paletta[i];
		int s = (c & 0xff) + ((c >> 8) & 0xff) + ((c >> 16) & 0xff);
		if( s < smin ) { smin = s; sotet = i; }
		if( s > smax ) { smax = s; vilagos = i; }
	}
	int nagy = bx >= 700 ? 3 : 2; // the picture is scaled down to 480 wide
	// Lines are separated by '|':
	int sorok = 1, leghosszabb = 0, h1 = 0;
	for( const char* p = Prof_szoveg; *p; p++ ) {
		if( *p == '|' ) { sorok++; h1 = 0; }
		else if( ++h1 > leghosszabb ) leghosszabb = h1;
	}
	int w = (leghosszabb*4+1)*nagy, h = (6*sorok+1)*nagy;
	if( w > bx ) w = bx;
	if( h > by ) h = by;
	Prof_w = w;
	Prof_h = h;
	for( int y = 0; y < h; y++ )
		memset( buff + y*BUFW, sotet, w );
	int k = 0, sorsz = 0;
	for( const char* q = Prof_szoveg; *q; q++ ) {
		if( *q == '|' ) { sorsz++; k = 0; continue; }
		int oszlop = k++;
		if( *q == ' ' )
			continue;
		const char* p = strchr( Prof_betuk, *q );
		if( !p )
			continue;
		unsigned short m = Prof_minta[p-Prof_betuk];
		for( int sor = 0; sor < 5; sor++ )
			for( int osz = 0; osz < 3; osz++ ) {
				if( !(m & (1 << (14 - sor*3 - osz))) )
					continue;
				int x0 = (1 + oszlop*4 + osz)*nagy, y0 = (1 + sorsz*6 + sor)*nagy;
				for( int dy = 0; dy < nagy; dy++ )
					for( int dx = 0; dx < nagy; dx++ )
						if( x0+dx < bx && y0+dy < by )
							buff[(y0+dy)*BUFW + x0+dx] = vilagos;
			}
	}
}

static void prof_frissit( SceUInt64 gpu, SceUInt64 varas ) {
	SceUInt64 most = sceKernelGetSystemTimeWide();
	if( Prof_utolso ) {
		Prof_gpu += gpu;
		Prof_varas += varas;
		Prof_osszes += most - Prof_utolso;
		Prof_db++;
	}
	Prof_utolso = most;
	if( Prof_db >= 30 ) {
		double n = Prof_db*1000.0;
		double ossz = Prof_osszes/n;
		double rajz = Prof_rajz/n, fiz = Prof_fiz/n, g = Prof_gpu/n, v = Prof_varas/n;
		double tobbi = ossz - rajz - fiz - g - v;
		if( tobbi < 0 ) tobbi = 0;
		snprintf( Prof_szoveg, sizeof( Prof_szoveg ),
				  "FPS %.1f DRAW %.1f PHYS %.1f GPU %.1f WAIT %.1f REST %.1f|"
				  "BACK %.1f FRONT %.1f BIKE %.1f PIX %.1f OBJ %.1f MAP %.1f TIME %.1f|"
				  "GPUX %.1f GPUEND %.1f WAKE %.1f GWAIT %.1f VBL %.1f|"
				  "G:PIC %.1f G:BIKES %.1f",
				  ossz > 0 ? 1000.0/ossz : 0.0, rajz, fiz, g, v, tobbi,
				  Prof_resz[0]/n, Prof_resz[1]/n, Prof_resz[2]/n, Prof_pix/n,
				  Prof_resz[3]/n, Prof_resz[4]/n, Prof_resz[5]/n,
				  Gpuexec/n, Gpukesz/n, Ebredes/n, Gpuvarido/n, Vblido/n,
				  Gpujelossz[1]/n, Gpujelossz[2]/n );
		Gpuexec = Gpukesz = Ebredes = Gpuvarido = Vblido = 0;
		Gpujelossz[1] = Gpujelossz[2] = 0;
		Prof_pix = 0;
		Prof_rajz = Prof_fiz = Prof_gpu = Prof_varas = Prof_osszes = 0;
		memset( Prof_resz, 0, sizeof( Prof_resz ) );
		Prof_db = 0;
	}
}
#else
#define prof_lockkezd()
#define prof_lockvege()
#endif

static void megjelenit( void ) {
	Piszkos = 0;
	if( !Guinit || !Buff[Elso] )
		return;
#ifdef PORT_PROFIL
	SceUInt64 t1 = sceKernelGetSystemTimeWide();
	prof_kiir( Buff[Elso], Buffx[Elso], Buffy[Elso] );
	keretrajzol();
	SceUInt64 t2 = sceKernelGetSystemTimeWide();
	prof_frissit( t2-t1-Utolsovaras, Utolsovaras );
#else
#ifdef PORT_TESZT
	tesztkep( Buff[Elso], Buffx[Elso], Buffy[Elso] );
#endif
	keretrajzol();
#endif
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
	// No waiting for the GPU here: it reads the other screen buffer (and the
	// other mask), the CPU's buffer was finished with before the GPU started.
	Menugpu = 0; // the screen shows the CPU picture again
	gp_uj();     // GPU items for this frame start empty
	Maszk = Maszkok[1-Elso];
	Bufflocked = 1;
	Buffx[1-Elso] = Port_kepx;
	Buffy[1-Elso] = Port_kepy;
	prof_lockkezd();
	return sorokat( Buff[1-Elso] );
}

void unlockbackbuffer( void ) {
	if( !Bufflocked )
		hiba( (char*)"unlockbackbuffer-ben !Bufflocked!" );
	Bufflocked = 0;
	prof_lockvege();
	Gpvan = Gpparancsdb > 0;
	Jatekkepen = Port_jatekgombok; // a frame of a play or replay
	Elso = 1-Elso; // Flip
	megjelenit();
}

unsigned char** lockfrontbuffer( void ) {
	if( Bufflocked )
		hiba( (char*)"lockfrontbuffer-ben Bufflocked!" );
	gpuvar();
	Menugpu = 0; // the screen shows the CPU picture again
	Gpvan = 0;
	Jatekkepen = 0;
	Bufflocked = 1;
	Buffx[Elso] = Port_kepx;
	Buffy[Elso] = Port_kepy;
	prof_lockkezd();
	return sorokat( Buff[Elso] );
}

void unlockfrontbuffer( void ) {
	if( !Bufflocked )
		hiba( (char*)"unlockfrontbuffer-ben !Bufflocked!" );
	Bufflocked = 0;
	prof_lockvege();
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
	gpuvar(); // the GPU may still be reading the palette
	memcpy( Paletta, pddpal, sizeof( Paletta ) );
	Palettaverzio++;
	// Like DirectDraw, a palette change is visible at once (next mv_check).
	// Not for a gameplay frame: the game sets the menus' palette when a level
	// ends, and the last frame of the level was shown again in the menus'
	// colours for a moment before the menu came:
	if( !Jatekkepen )
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
#ifdef PORT_TESZT
	tesztlep();
	Gombok |= Tesztgombok;
	Lx = Ly = 128; // no stick in the emulator test runs
#endif

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
	mut->billsnap = 0;                    // not assigned ("???"; Triangle when set to I)
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
	gpuvar();
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
		keretrajzol( 1 );
		gpuvar(); // the keyboard draws over our picture
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
#ifdef PORT_TESZT
	tesztbetolt();
#endif

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
