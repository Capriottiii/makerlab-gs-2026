#include "stub.h"
unsigned long g_ms=0; float g_dist=150; bool g_noEcho=false; int g_gasRaw=410;
bool g_wifi=true; int g_apiCode=200; bool g_dhtOk=true; float g_dhtT=22.1f,g_dhtH=87.5f;
int g_pin[64]; SerialC Serial; WiFiC WiFi; String g_last;
void pinMode(int,int){}
void digitalWrite(int p,int v){
  int old=g_pin[p]; g_pin[p]=v;
  if(p==23&&v==1) printf("[t=%6.1fs] [buzzer] pulso\n",g_ms/1000.0);
  if((p==25||p==26||p==27)&&old!=v){ printf("[t=%6.1fs] [led] V=%d A=%d R=%d\n",g_ms/1000.0,g_pin[25],g_pin[26],g_pin[27]); }
}
#include SKETCH
static void run(int sec){ unsigned long end=g_ms+sec*1000UL; while(g_ms<end) loop(); }
static void gas(float pct){ g_gasRaw=(int)(pct/100.0f*4095); }
int main(int argc,char**argv){
  int t=atoi(argv[1]); gas(10); if(t==16) g_dhtOk=false;
  setup();
  switch(t){
   case 1: g_dist=150; run(70); break;
   case 2: g_dist=66; run(40); break;
   case 3: g_dist=30; run(40); break;
   case 4: run(120); break;
   case 5: g_dist=150; run(20); printf("--- onda: 30 cm por 10 s ---\n"); g_dist=30; run(10); printf("--- volta a 150 cm ---\n"); g_dist=150; run(40); break;
   case 6: run(40); break;
   case 7: g_dist=30; gas(85); run(40); break;
   case 8: g_dist=30; run(75); break;
   case 9: g_dist=66; run(30); printf("--- sobe para 30 cm ---\n"); g_dist=30; run(30); break;
   case 11: g_dist=30; g_apiCode=-1; run(40); break;
   case 12: g_wifi=false; g_dist=30; run(60); break;
   case 13: g_apiCode=-1; g_dist=30; run(40); printf("--- API volta, nivel normal ---\n"); g_apiCode=200; g_dist=150; run(20); break;
   case 14: g_noEcho=true; run(20); break;
   case 16: g_dist=150; run(40); printf("ULTIMO JSON: %s", g_last.c_str()); putchar(10); break;
   case 15: g_dhtOk=false; g_dist=150; run(40); printf("ULTIMO JSON: %s\n",g_last.c_str()); break;
  }
  if(t==1||t==3) printf("ULTIMO JSON: %s\n",g_last.c_str());
}
