#include	"all.h"

kiskep::kiskep( char* pic8nev, pic8* ppic ) {
	tomb = NULL;
	foglalt = NULL;
	if( !ppic ) {
		{ ppic = NEW(pic8*) pic8( pic8nev ); }
	}

	lyuk = ppic->gpixel( 0, 0 );

	xsize = ppic->getxsize();
	ysize = ppic->getysize();
	if( xsize > 255 || ysize > 255 )
		hiba( "kiskepben size > 255!" );
	hossz = ysize*256l;
	// (port) 16-byte aligned (a GPU texture must start on 16 bytes):
	{ foglalt = NEW(unsigned char*) unsigned char[unsigned(hossz)+16]; }
	if( !foglalt )
		uzenet( "memory!" );
	tomb = (unsigned char*)(((uintptr_t)foglalt + 15) & ~(uintptr_t)15);
	for( int y = 0; y < ysize; y++ )
		for( int x = 0; x < xsize; x++ )
			tomb[y*256+x] = ppic->gpixel( x, y );

	DELETE ppic; }
}

kiskep::~kiskep( void ) {
	if( foglalt )
		DELETE foglalt; } // (port) tomb is inside it
	Port_kiskepverzio++; // (port) the GPU's copies of the pictures are made again
}
