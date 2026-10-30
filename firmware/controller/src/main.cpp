#include <Arduino.h>
#include <cstring>
#include <cstdlib>
#include "config/SystemConfig.h"
#include "config/HardwareConfig.h"

// USB serial test protocol. Actuator GPIOs are NEVER initialized here.
namespace {
char line[256];
size_t used=0;
bool overlong=false;

bool idField(const char* input, unsigned long& id) {
  const char* p=strstr(input,"\"id\"");
  if(!p || !(p=strchr(p+4,':'))) return false;
  ++p; while(*p==' ' || *p=='\t') ++p;
  if(*p<'0'||*p>'9') return false;
  char* end=nullptr; id=strtoul(p,&end,10);
  while(*end==' ' || *end=='\t') ++end;
  return *end==','||*end=='}';
}
bool commandField(const char* input,char* cmd,size_t cap) {
  const char* p=strstr(input,"\"cmd\"");
  if(!p || !(p=strchr(p+5,':'))) return false;
  ++p; while(*p==' '||*p=='\t') ++p;
  if(*p++!='"') return false;
  size_t n=0;
  while(*p && *p!='"') {
    if(*p=='\\'||n+1>=cap) return false;
    cmd[n++]=*p++;
  }
  if(*p!='"'||n==0) return false;
  cmd[n]=0; return true;
}
void error(unsigned long id,const char* code) {
  Serial.printf("{\"id\":%lu,\"status\":\"ERROR\",\"code\":\"%s\"}\n",id,code);
}
void dispatch(const char* input) {
  unsigned long id=0;char cmd[40];
  if(!idField(input,id)||!commandField(input,cmd,sizeof(cmd))) {
    error(0,"INVALID_REQUEST");return;
  }
  if(strcmp(cmd,"PING")==0) {
    Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"reply\":\"PONG\"}\n",id);
  } else if(strcmp(cmd,"GET_STATUS")==0) {
    Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"state\":\"SAFE_IDLE\",\"motors_enabled\":false,\"z1_homed\":false,\"z2_homed\":false}\n",id);
  } else if(strcmp(cmd,"GET_INFO")==0) {
    Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"device\":\"VALVER SMART DISPENSER\",\"firmware\":\"%s\"}\n",id,valver::kFirmwareVersion);
  } else {
    error(id,"MOTORS_DISABLED");
  }
}
}
void setup() {
  Serial.begin(valver::kDefaultBaudRate);
}
void loop() {
  while(Serial.available()>0) {
    const char c=static_cast<char>(Serial.read());
    if(c=='\n') {
      if(overlong) error(0,"MESSAGE_TOO_LONG");
      else if(used) {line[used]=0;dispatch(line);}
      used=0;overlong=false;
    } else if(c!='\r' && !overlong) {
      if(used<sizeof(line)-1) line[used++]=c;
      else overlong=true;
    }
  }
}
