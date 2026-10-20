#include	"all.h"

int Hangmarmegy = 0;

// (port) Name of a key binding for the Help screen: the PSP button name,
// "???" if nothing is assigned, or "Key code: N" for a key without a button.
char* kodtobillnev( int billkod ); // (port) psp_platform.cpp
static char* billnev_help( int billkod, char* puffer ) {
	char* nev = kodtobillnev( billkod );
	if( nev )
		strcpy( puffer, nev );
	else if( billkod == 0 )
		strcpy( puffer, "???" );
	else
		sprintf( puffer, "Key code: %d", billkod );
	return puffer;
}

void help( void ) {
	szoveglista szl;

	int x1 = 40;
	int x2 = 250; // (port) wider first column: the button names are longer
	int yo = 80;
	int dy = 32;
	// (port) The current bindings of player A, with PSP button names:
	szl.addszoveg_kozep( "Current controls:", 320, 20 );

	// Names are copied into the list, so a local buffer is enough:
	char nev[8][30];
	jatekosopciok* o = &State->opciok1;

	szl.addszoveg( billnev_help( o->billgaz, nev[0] ), x1, yo );
	szl.addszoveg( "- Accelerate", x2, yo );

	szl.addszoveg( billnev_help( o->billfek, nev[1] ), x1, yo+dy );
	szl.addszoveg( "- Block Wheels", x2, yo+dy );

	szl.addszoveg( billnev_help( o->billugras2, nev[2] ), x1, yo+dy*2 );
	szl.addszoveg( "- Rotate AntiClockwise", x2, yo+dy*2 );

	szl.addszoveg( billnev_help( o->billugras1, nev[3] ), x1, yo+dy*3 );
	szl.addszoveg( "- Rotate Clockwise", x2, yo+dy*3 );

	szl.addszoveg( billnev_help( o->billfordul, nev[4] ), x1, yo+dy*4 );
	szl.addszoveg( "- Turn Around", x2, yo+dy*4 );

	szl.addszoveg( billnev_help( o->billview, nev[5] ), x1, yo+dy*5 );
	szl.addszoveg( "- View Box Toggle", x2, yo+dy*5 );

	szl.addszoveg( billnev_help( o->billtime, nev[6] ), x1, yo+dy*6 );
	szl.addszoveg( "- Time Display Toggle", x2, yo+dy*6 );

	szl.addszoveg_kozep( "After you have eaten all the fruits,", 320, yo+dy*9 );
	szl.addszoveg_kozep( "touch the flower!", 320, yo+dy*10 );

	mk_emptychar();
	while( 1 ) {
		if( mk_kbhit() ) {
			int c = mk_getextchar();
			if( c == MK_ESC || c == MK_ENTER )
				return;
		}
		szl.kirajzol();
	}
}

// Egyben allithato:
void options( void ) {
	int origsecondary = State->secondarybuffer;

	int kurrens = 0;
	while( 1 ) {
		valaszt2 val;
		val.kur = kurrens;
		val.egykepen = 11;
		val.x0 = 72;
		val.x0_tab = 390;
		val.y0 = 77;
		val.dy = 36;
		val.escelheto = 1;
		strcpy( val.cim, "Options" );

		int vantagis = 0;
		strcpy( Rubrikak[0], "Play mode:" );
		if( State->single ) {
			strcpy( Rubrikak_tab[0], "Single Player" );
		}
		else {
			strcpy( Rubrikak_tab[0], "Multiplayer" );
			vantagis = 1;
		}

		if( vantagis ) {
			strcpy( Rubrikak[1], "Flag Tag:" );
			if( State->tag )
				strcpy( Rubrikak_tab[1], "On" );
			else
				strcpy( Rubrikak_tab[1], "Off" );
		}

		// Jatekos valasztas:
		sprintf( Rubrikak[1+vantagis], "Player A:" );
		sprintf( Rubrikak_tab[1+vantagis], State->jatekosa );

		sprintf( Rubrikak[2+vantagis], "Player B:" );
		sprintf( Rubrikak_tab[2+vantagis], State->jatekosb );

		strcpy( Rubrikak[3+vantagis], "Sound:" );
		if( State->soundon )
			sprintf( Rubrikak_tab[3+vantagis], "Enabled" );
		else
			sprintf( Rubrikak_tab[3+vantagis], "Disabled" );

		strcpy( Rubrikak[4+vantagis], "Sound Optimization:" );
		if( State->secondarybuffer )
			sprintf( Rubrikak_tab[4+vantagis], "Compatibility" );
		else
			sprintf( Rubrikak_tab[4+vantagis], "Best Quality" );

		sprintf( Rubrikak[5+vantagis], "Animated Menus:" );
		if( State->animatedmenus )
			sprintf( Rubrikak_tab[5+vantagis], "Yes" );
		else
			sprintf( Rubrikak_tab[5+vantagis], "No" );

		sprintf( Rubrikak[6+vantagis], "Video Detail:" );
		if( State->highquality )
			sprintf( Rubrikak_tab[6+vantagis], "High" );
		else
			sprintf( Rubrikak_tab[6+vantagis], "Low" );

		sprintf( Rubrikak[7+vantagis], "Animated Objects:" );
		if( State->animatedobjects )
			sprintf( Rubrikak_tab[7+vantagis], "Yes" );
		else
			sprintf( Rubrikak_tab[7+vantagis], "No" );

		sprintf( Rubrikak[8+vantagis], "Swap Bikes:" );
		if( State->aplayerabike )
			sprintf( Rubrikak_tab[8+vantagis], "No" );
		else
			sprintf( Rubrikak_tab[8+vantagis], "Yes" );

		sprintf( Rubrikak[9+vantagis], "Customize Controls ..." );
		sprintf( Rubrikak_tab[9+vantagis], "" );


		val.bead( 10+vantagis, 1 );

		kurrens = val.valassz();

		if( kurrens < 0 ) {
			//State->save_aaa(); majd programbol valo kilepeskor kiirjuk
			// (port) Saved now: on the PSP the game is often left with HOME
			State->save();
			if( origsecondary != State->secondarybuffer && Hangmarmegy ) {
				// Kiirunk egy dialogust, hogy ujra kell inditani ehhez:
				szoveglista szl;
				szl.addszoveg_kozep( "You modified the sound buffer", 320, 140 );
				szl.addszoveg_kozep( "optimization! As you already", 320, 190 );
				szl.addszoveg_kozep( "used the sound in this session,", 320, 240 );
				szl.addszoveg_kozep( "this will only take effect", 320, 290 );
				szl.addszoveg_kozep( "if you restart the game.", 320, 340 );
				mk_emptychar();
				while( 1 ) {
					if( mk_kbhit() ) {
						int c = mk_getextchar();
						if( c == MK_ESC || c == MK_ENTER )
							break;
					}
					szl.kirajzol();
				}
			}
			return;
		}

		if( kurrens == 0 )
			State->single = !State->single;

		if( kurrens == 1 && vantagis )
			State->tag = !State->tag;

		if( vantagis )
			kurrens -= 1;

		if( kurrens == 1 )
			jatekosvalasztas( 1, 1 );

		if( kurrens == 2 )
			jatekosvalasztas( 0, 1 );

		if( kurrens == 3 )
			State->soundon = !State->soundon;

		if( kurrens == 4 )
			State->secondarybuffer = !State->secondarybuffer;

		if( kurrens == 5 )
			State->animatedmenus = !State->animatedmenus;

		if( kurrens == 6 ) {
			State->highquality = !State->highquality;
			invalidate_ptop();
		}

		if( kurrens == 7 )
			State->animatedobjects = !State->animatedobjects;

		if( kurrens == 8 )
			State->aplayerabike = !State->aplayerabike;

		if( kurrens == 9 )
			customize();


		if( vantagis ) // Visszaalitjuk eredeti helyzetebe
			kurrens += 1;
	}
}


