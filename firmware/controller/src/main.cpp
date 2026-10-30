#include <Arduino.h>
#include <cstring>
#include <cstdlib>
#include "config/SystemConfig.h"
#include "config/HardwareConfig.h"

// VALVER bench diagnostics only. No motor power, no driver enable.
// EN is active LOW on a TMC2209: HIGH = disabled.
namespace {
constexpr uint8_t Z1_STEP=4, Z1_DIR=5, Z1_EN=6;
constexpr uint8_t Z2_STEP=7, Z2_DIR=15, Z2_EN=16;
constexpr uint32_t TEST_PERIOD_MS=100;
constexpr uint8_t TEST_PULSES=12;
char incoming[256];
size_t incomingLength=0;
bool tooLong=false;
struct TestState { bool active=false; uint8_t axis=0; uint8_t pulses=0; unsigned long id=0; uint32_t nextMs=0; };
TestState test;
uint8_t selectedAxis=0;
bool armed=false;
uint32_t armedAt=0;
constexpr uint32_t ARM_TIMEOUT_MS=15000;

void errorReply(unsigned long id,const char* code) {
  Serial.printf("{\"id\":%lu,\"status\":\"ERROR\",\"code\":\"%s\"}\n",id,code);
}
bool readId(const char* json,unsigned long& id) {
  const char* p=strstr(json,"\"id\"");
  if(!p || !(p=strchr(p+4,':')))return false;
  ++p; while(*p==' '||*p=='\t')++p;
  if(*p<'0'||*p>'9')return false;
  char* end=nullptr;
  id=strtoul(p,&end,10);
  while(*end==' '||*end=='\t')++end;
  return *end==','||*end=='}';
}
bool readCommand(const char* json,char* out,size_t cap) {
  const char* p=strstr(json,"\"cmd\"");
  if(!p || !(p=strchr(p+5,':')))return false;
  ++p;while(*p==' '||*p=='\t')++p;
  if(*p++!='"')return false;
  size_t n=0;
  while(*p && *p!='"'){
    if(*p=='\\'||n+1>=cap)return false;
    out[n++]=*p++;
  }
  if(*p!='"'||n==0)return false;
  out[n]=0;return true;
}
void stopTest() {
  test.active=false;
  digitalWrite(Z1_STEP,LOW);
  digitalWrite(Z2_STEP,LOW);
  digitalWrite(Z1_EN,HIGH);
  digitalWrite(Z2_EN,HIGH);
}
void disarm() {
  armed=false;
  stopTest();
}
void replySelection(unsigned long id) {
  Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"selected_axis\":%u,\"armed\":%s,\"drivers_enabled\":false}\n",id,selectedAxis,armed?"true":"false");
}
void dispatch(const char* data) {
  unsigned long id=0;
  char cmd[48];
  if(!readId(data,id)||!readCommand(data,cmd,sizeof(cmd))){
    errorReply(0,"INVALID_REQUEST");return;
  }
  if(strcmp(cmd,"PING")==0){
    Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"reply\":\"PONG\"}\n",id);
  } else if(strcmp(cmd,"GET_INFO")==0){
    Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"device\":\"VALVER SMART DISPENSER\",\"firmware\":\"%s\",\"mode\":\"GPIO_BENCH_TEST\"}\n",id,valver::kFirmwareVersion);
  } else if(strcmp(cmd,"SELECT_Z1")==0 || strcmp(cmd,"SELECT_Z2")==0){
    disarm();
    selectedAxis=strcmp(cmd,"SELECT_Z1")==0?1:2;
    replySelection(id);
  } else if(strcmp(cmd,"DISABLE_CHANNELS")==0){
    disarm(); selectedAxis=0; replySelection(id);
  } else if(strcmp(cmd,"ARM_TEST")==0){
    if(test.active){errorReply(id,"BUSY");return;}
    if(selectedAxis==0){errorReply(id,"NO_CHANNEL");return;}
    armed=true;armedAt=millis();replySelection(id);
  } else if(strcmp(cmd,"DISARM_TEST")==0){
    disarm();replySelection(id);
  } else if(strcmp(cmd,"GET_STATUS")==0){
    Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"state\":\"GPIO_TEST_ONLY\",\"motors_enabled\":false,\"vm_required\":false,\"drivers_enabled\":false,\"test_active\":%s}\n",id,test.active?"true":"false");
    replySelection(id);
  } else if(strcmp(cmd,"STOP_TEST")==0){
    disarm(); selectedAxis=0;
    Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"state\":\"STOPPED\"}\n",id);
  } else if(strcmp(cmd,"TEST_Z1_UP")==0 || strcmp(cmd,"TEST_Z1_DOWN")==0 ||
            strcmp(cmd,"TEST_Z2_UP")==0 || strcmp(cmd,"TEST_Z2_DOWN")==0){
    if(test.active){errorReply(id,"BUSY");return;}
    if(!armed || millis()-armedAt>ARM_TIMEOUT_MS){disarm();errorReply(id,"NOT_ARMED");return;}
    const uint8_t requested=strstr(cmd,"Z1")?1:2;
    if(requested!=selectedAxis){errorReply(id,"CHANNEL_LOCKED");return;}
    const bool first=(cmd[5]=='Z' && cmd[6]=='1');
    const bool up=strstr(cmd,"_UP")!=nullptr;
    test.axis=first?1:2;
    test.pulses=0;
    test.id=id;
    test.active=true;
    armed=false; // Require explicit new authorization for each subsequent test.
    test.nextMs=millis();
    // Physical movement is inhibited: EN stays HIGH at all times.
    digitalWrite(first?Z1_DIR:Z2_DIR,up?HIGH:LOW);
    Serial.printf("{\"id\":%lu,\"status\":\"ACCEPTED\",\"test\":\"GPIO_ONLY\",\"axis\":\"Z%d\",\"direction\":\"%s\"}\n",id,test.axis,up?"UP":"DOWN");
  } else {
    errorReply(id,"MOTORS_DISABLED");
  }
}
void serviceTest(){
  if(!test.active || (int32_t)(millis()-test.nextMs)<0)return;
  const uint8_t pin=test.axis==1?Z1_STEP:Z2_STEP;
  digitalWrite(pin,HIGH);
  delayMicroseconds(30);
  digitalWrite(pin,LOW);
  ++test.pulses;
  test.nextMs=millis()+TEST_PERIOD_MS;
  if(test.pulses>=TEST_PULSES){
    const unsigned long id=test.id;
    stopTest();
    Serial.printf("{\"id\":%lu,\"status\":\"DONE\",\"test\":\"GPIO_ONLY\",\"pulses\":%u,\"drivers_enabled\":false}\n",id,TEST_PULSES);
  }
}
}
void setup() {
  // Set output latches to safe states before enabling GPIO output modes.
  digitalWrite(Z1_EN,HIGH); digitalWrite(Z2_EN,HIGH);
  digitalWrite(Z1_STEP,LOW); digitalWrite(Z2_STEP,LOW);
  digitalWrite(Z1_DIR,LOW); digitalWrite(Z2_DIR,LOW);
  pinMode(Z1_EN,OUTPUT); pinMode(Z2_EN,OUTPUT);
  pinMode(Z1_STEP,OUTPUT); pinMode(Z2_STEP,OUTPUT);
  pinMode(Z1_DIR,OUTPUT); pinMode(Z2_DIR,OUTPUT);
  stopTest();
  Serial.begin(valver::kDefaultBaudRate);
}
void loop() {
  if(armed && millis()-armedAt>ARM_TIMEOUT_MS) disarm();
  while(Serial.available()>0){
    const char c=(char)Serial.read();
    if(c=='\n'){
      if(tooLong)errorReply(0,"MESSAGE_TOO_LONG");
      else if(incomingLength){incoming[incomingLength]=0;dispatch(incoming);}
      incomingLength=0;tooLong=false;
    }else if(c!='\r' && !tooLong){
      if(incomingLength<sizeof(incoming)-1)incoming[incomingLength++]=c;
      else tooLong=true;
    }
  }
  serviceTest();
}
