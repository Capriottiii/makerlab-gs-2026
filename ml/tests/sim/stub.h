#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <string>
#include <cmath>
#include <ctime>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
typedef unsigned char byte;
extern unsigned long g_ms;
extern float g_dist; extern bool g_noEcho; extern int g_gasRaw;
extern bool g_wifi; extern int g_apiCode; extern bool g_dhtOk; extern float g_dhtT,g_dhtH;
extern int g_pin[64];
inline unsigned long millis(){return g_ms;}
inline unsigned long micros(){return g_ms*1000UL;}
inline void delay(unsigned long m){g_ms+=m;}
inline void delayMicroseconds(unsigned){}
void pinMode(int,int);
void digitalWrite(int,int);
inline int digitalRead(int){return 0;}
inline int analogRead(int){return g_gasRaw;}
inline void analogReadResolution(int){}
inline unsigned long pulseIn(int,int,unsigned long){ if(g_noEcho) return 0; return (unsigned long)(g_dist*2.0f/0.0343f);}
inline void randomSeed(unsigned long s){srand((unsigned)s);}
inline long random(long a,long b){return a+rand()%(b-a);}
inline uint32_t esp_random(){return 12345;}
class String {
 public: std::string s;
  String(){} String(const char*c):s(c){} String(const std::string&c):s(c){}
  bool startsWith(const char*p)const{return s.rfind(p,0)==0;}
  const char*c_str()const{return s.c_str();}
};
inline String operator+(const char*a,const String&b){return String(std::string(a)+b.s);}
struct SerialC{
  void begin(int){}
  void print(const char*c){printf("%s",c);}
  void println(const char*c){printf("[t=%6.1fs] %s\n",g_ms/1000.0,c);}
  void println(const String&c){println(c.c_str());}
  void println(){printf("\n");}
  void printf(const char*f,...){ va_list a; va_start(a,f); char b[1024]; vsnprintf(b,sizeof b,f,a); va_end(a);
    std::string t(b); if(!t.empty()&&t.back()=='\n'){t.pop_back(); ::printf("[t=%6.1fs] %s\n",g_ms/1000.0,t.c_str());} else ::printf("%s",t.c_str()); }
};
extern SerialC Serial;
#define WL_CONNECTED 3
struct WiFiC{ int status(){return g_wifi?3:0;} void begin(const char*,const char*,int){} void reconnect(){} };
extern WiFiC WiFi;
inline void configTime(long,long,const char*){}
inline bool getLocalTime(struct tm*t,int){ time_t x=1794604503+g_ms/1000; *t=*gmtime(&x); return true;}
struct WiFiClientSecure{ void setInsecure(){} };
struct HTTPClient{
  void begin(WiFiClientSecure&,const char*){} void begin(const char*){}
  void setTimeout(int){} void addHeader(const char*,const char*){}
  int POST(const String&b){ extern String g_last; g_last=b; return g_apiCode; }
  void end(){}
};
struct DHT{ DHT(int,int){} void begin(){} float readHumidity(){return g_dhtOk?g_dhtH:NAN;} float readTemperature(){return g_dhtOk?g_dhtT:NAN;} };
