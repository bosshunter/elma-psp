

void dobozki( pic8* ppic8, kiskep* pkiskep, vekt2 u, vekt2 v, vekt2 r );

extern int Affinitas;
extern int Dobozgpu; // (port) see doboz.cpp
extern double Dobozgpux, Dobozgpuy;
// kozepet es iranyt majd o megszorozza arany-okkal:
void setaffinitas( vekt2 kozep, vekt2 irany, double nagysag,
			double aranyx, double aranyy );



