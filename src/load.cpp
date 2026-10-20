#include	"all.h"
void betoltecseteket( void );

int Volttopsave = 0;

static char Bentlevelnev[20] = "";
// (port) The lev subfolder Bentlevelnev was loaded from (the same name can be
// in several folders):
static char Bentmappa[100] = "";
static int masmappa( void ) {
	return strcmp( Bentmappa, Levalmappa ) != 0;
}

// Beolvas egy level-t ha kell (nev max 12 karakter).
// Torli es kitolti Ptop, Plgr, Pszak, ecset mutatokat.
// Ha belso file-ban is megvan palya onnan veszi:
// Hamissal ter vissza, ha hibas topologiaju level:
int floadlevel_p( char* nev ) {
	if( !nev )
		hiba( "floadlevel_p-ben !nev!" );
	if( strlen( nev ) > 12 || !nev[0] )
		hiba( "floadlevel_p-ben strlen( nev ) > 12 || !nev[0]!: ", nev );
	// (port) The shareware version cannot play a level saved by the
	// registered one. The PC game quit with a message; here the message is
	// shown and the caller goes back (Ptop stays the level loaded before):
	if( port_csakregisztralt( nev ) ) {
		szoveglista szl;
		szl.addszoveg_kozep( "Sorry, this level file can only be", 320, 170 );
		szl.addszoveg_kozep( "loaded into the registered version!", 320, 220 );
		szl.addszoveg_kozep( nev, 320, 290 );
		mk_emptychar();
		while( 1 ) {
			if( mk_kbhit() ) {
				int c = mk_getextchar();
				if( c == MK_ESC || c == MK_ENTER )
					break;
			}
			szl.kirajzol();
		}
		return 0;
	}
	if( Volttopsave || !Ptop || !Pszak || strcmpi( nev, Bentlevelnev ) != 0 ||
															masmappa() ) {
		Volttopsave = 0;
		// Ujra be kell olvasni palyat:
		strcpy( Bentlevelnev, nev );
		strcpy( Bentmappa, Levalmappa ); // (port)
		if( Ptop )
			delete Ptop;
		Ptop = new topol( nev );
		if( Ptop->hibas ) {
			// Kiir hiba uzenetet:
			szoveglista szl;
			szl.addszoveg_kozep( "Level file has some topology errors!", 320, 190 );
			szl.addszoveg_kozep( "Use the editor to fix them!", 		320, 240 );
			mk_emptychar();
			while( 1 ) {
				if( mk_kbhit() ) {
					int c = mk_getextchar();
					if( c == MK_ESC || c == MK_ENTER )
						break;
				}
				szl.kirajzol();
			}

			delete Ptop;
			Ptop = NULL;
			return 0;
		}
		loadlgrfile( Ptop->lgrnev );
		Ptop->pic_selejtez( Plgr );

		if( Pszak )
			delete Pszak;
		Pszak = new szakaszok( Ptop );
		if( Fejsugar > Pmot1->kor2.sugar )
			Pszak->rendez( Fejsugar );
		else
			Pszak->rendez( Pmot1->kor2.sugar );

		betoltecseteket();
	}
	return 1;
}


// Torli es kitolti Ptop, Plgr, mutatokat.
// Pszak, ecset mutatokat torli:
// Ha belso file-ban is megvan palya onnan veszi:
// Igaz ha file nem volt lezarva, kulonben new palya hamissal:
// (port) -1: not loaded at all (a registered level in the shareware
// version); the level that was open stays open.
int floadlevel_e( char* nev, int forceload ) {
	int levoltzarva = 0;

	if( !nev )
		hiba( "floadlevel_e-ben !nev!" );
	if( strlen( nev ) > 12 || !nev[0] )
		hiba( "floadlevel_e-ben strlen( nev ) > 12 || !nev[0]!: ", nev );
	// (port) The PC game quit here with a message; now the editor stays where
	// it was:
	if( port_csakregisztralt( nev ) ) {
		dialog( "Sorry, this level file can only be loaded into the registered version!", nev );
		if( !Ptop ) {
			Ptop = new topol;
			Bentlevelnev[0] = 0;
			loadlgrfile( Ptop->lgrnev );
		}
		return -1;
	}
	if( forceload || Volttopsave || !Ptop || Ptop->lezart ||
							strcmpi( nev, Bentlevelnev ) != 0 || masmappa() ) {
		Volttopsave = 0;
		// Ujra be kell olvasni palyat:
		strcpy( Bentlevelnev, nev );
		strcpy( Bentmappa, Levalmappa ); // (port)
		if( Ptop )
			delete Ptop;
		if( strcmpi( nev, "_uj_topol_" ) == 0 ) {
			Ptop = new topol;
			Bentlevelnev[0] = 0;
		}
		else
			Ptop = new topol( nev );

		// (port) The shareware version cannot edit locked levels (as the
		// original): a message, then an empty new level:
		if( Sarvari && Ptop->lezart ) {
			levoltzarva = 1;
			char szoveg[80];
			sprintf( szoveg, "The file '%s' is locked, you cannot edit it!", nev );
			dialog( szoveg );
			delete Ptop;
			Ptop = new topol;
			Bentlevelnev[0] = 0;
		}
		// (port) The registered version opens locked levels, with a warning.
		// The lock is dropped, so a plain Save writes a normal (editable)
		// file; SAVE LOCKED still locks it again:
		if( Ptop->lezart ) {
			char szoveg[80];
			sprintf( szoveg, "The level '%s' is marked as locked.", nev );
			dialog( szoveg, "If you save it normally, it will be saved unlocked." );
			Ptop->lezart = 0;
		}
		/*if( Ptop->lezart ) {
			levoltzarva = 1;

			char szoveg[80];
			sprintf( szoveg, "The file '%s' is locked, you cannot edit it!",
												State->editfilenev );

			dialog( szoveg );
			delete Ptop;
			Ptop = new topol;
		}*/

		loadlgrfile( Ptop->lgrnev );
		if( Ptop->pic_selejtez( Plgr ) )
			dialogselejtez();
	}
	Ptop->checklevelforsarvari();

	if( Pszak ) // ervenytelenites
		delete Pszak;
	Pszak = NULL;

	// Ha vannak benne idok, figyelmeztetes:
	if( Ptop->idok.singleidok.idokszama > 0 ||
		Ptop->idok.multiidok.idokszama > 0 )
		dialog( "Warning!",
				"The level file you are opening has some best times.",
				"If you save this level file, these times will be erased!" );

	return !levoltzarva;
}


// Opciok minibike-ja hasznalja:
void invalidate_ptop( void ) {
	if( Ptop ) {
		delete Ptop;
		Ptop = 0;
	}
}

void dialogselejtez( void ) {
dialog( "The LGR file has changed since the last edition of this level and",
		"some parts (pictures, textures or border polygons) of the level",
		"must have been deleted!",
		"",
		"!!!!IMPORTANT!!!!",
		"If you do not want to loose these parts, do not save this level",
		"on its original name!" );
}

