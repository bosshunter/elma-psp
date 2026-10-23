
// Egy szegmensben tarolja kepeket, maximalis meretei 256-nal kissebbek, es
// sorok pont 256 byte-onkent kovetkeznek!

class kiskep {
public:
	unsigned char lyuk;
	unsigned char* tomb;     // (port) 16-byte aligned, so the GPU can use it as a texture
	unsigned char* foglalt;  // (port) the allocation tomb is in
	int xsize, ysize;
	long hossz;
	// Ha megadjuk ppic-t, akkor azt o deleteli:
	kiskep( char* pic8nev, pic8* ppic = NULL );
	~kiskep( void );
};