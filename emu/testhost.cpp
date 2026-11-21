// Windows-Test-Host: exportiert die NT-API wie nt_emu (--export-all-symbols), lädt sary_shagan.dll
// über die Brücke und lässt es laufen (unter Wine: wine64 testhost.exe)
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <distingnt/api.h>
extern "C" {
static float work[4096];
const _NT_globals NT_globals = { 48000, 32, work, sizeof(work), 0, 0 };
uint8_t NT_screen[128*64];
static int draws = 0, params = 0;
int16_t vals[64];
uint32_t NT_getCpuCycleCount(void){ return 42; }
int32_t NT_algorithmIndex(const _NT_algorithm*){ return 0; }
uint32_t NT_parameterOffset(void){ return 0; }
void NT_setParameterFromUi(uint32_t, uint32_t p, int16_t v){ vals[p]=v; ++params; }
void NT_setParameterFromAudio(uint32_t, uint32_t p, int16_t v){ vals[p]=v; ++params; }
void NT_drawText(int x,int y,const char* s,int,_NT_textAlignment,_NT_textSize){ if(draws++<3) printf("  draw text '%s' at %d,%d\n", s, x, y); }
void NT_drawShapeI(_NT_shape,int,int,int,int,int){ ++draws; }
int NT_intToString(char* b,int32_t v){ return sprintf(b,"%d",(int)v); }
int NT_floatToString(char* b,float v,int d){ return sprintf(b,"%.*f",d,v); }
}
int main(){
	HMODULE h = LoadLibraryA("sary_shagan.dll");
	if(!h){ printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
	typedef uintptr_t (*PE)(_NT_selector,uint32_t);
	PE pe = (PE)GetProcAddress(h,"pluginEntry");
	printf("pluginEntry %p, API version %u\n", (void*)pe, (unsigned)pe(kNT_selector_version,0));
	const _NT_factory* f = (const _NT_factory*)pe(kNT_selector_factoryInfo,0);
	printf("factory: %s - %s\n", f->name, f->description);
	_NT_algorithmRequirements req; f->calculateRequirements(req,NULL);
	std::vector<uint8_t> sram(req.sram + 64), dram(req.dram + 64), dtc(req.dtc + 64);
	_NT_algorithmMemoryPtrs ptrs = { sram.data(), dram.data(), dtc.data(), 0 };
	_NT_algorithm* a = f->construct(ptrs, req, NULL);
	for(unsigned i=0;i<req.numParameters;++i) vals[i]=a->parameters[i].def;
	a->v = vals; a->vIncludingCommon = vals;
	for(unsigned i=0;i<req.numParameters;++i) f->parameterChanged(a,i);
	vals[7] = 1000; f->parameterChanged(a,7);	// Dry/Wet 100 %: nur Hall
	std::vector<float> bus(64*32); double e = 0;
	for(int blk=0; blk<3000; ++blk){
		std::fill(bus.begin(),bus.end(),0.f);
		if(blk==0) bus[0] = 5.f;
		f->step(a, bus.data(), 8);
		for(int i=0;i<32;++i) e += bus[12*32+i]*bus[12*32+i];
	}
	printf("%u Parameter, %u KB DRAM, Hallenergie nach Impuls %.3f\n", (unsigned)req.numParameters, (unsigned)(req.dram/1024), e);
	printf("hasCustomUi 0x%x\n", (unsigned)f->hasCustomUi(a));
	_NT_float3 pots; f->setupUi(a, pots);
	_NT_uiData ui = {}; ui.controls = kNT_potC; ui.pots[0] = pots[0]; ui.pots[2] = pots[2];
	for(int k=0;k<=20;++k){ ui.pots[1] = pots[1] + 0.01f*k; f->customUi(a, ui); }
	printf("customUi: Size = %d (vorher 500), Parameteraufrufe am Host %d\n", vals[4], params);
	f->draw(a); printf("draw(): %d Zeichenaufrufe\n", draws);
	printf(e > 0.01 && vals[4] > 500 && draws > 10 ? "OK\n" : "FEHLER\n");
	return 0;
}
