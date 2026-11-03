#include <Arduino.h>
#include <cstring>
#include <cstdlib>
#include "config/SystemConfig.h"
#include "config/HardwareConfig.h"

// VALVER live jog commissioning. TMC2209 EN is active LOW.
// Motion is limited to one axis, with deadman heartbeat and step budget.
// The driver CURRENT must be set for the actual motor before using this firmware.
namespace {
constexpr uint8_t Z1_STEP=4,Z1_DIR=5,Z1_EN=6;
constexpr uint8_t Z2_STEP=7,Z2_DIR=15,Z2_EN=16;
constexpr uint32_t HEARTBEAT_TIMEOUT_MS=350;
constexpr uint32_t MAX_JOG_MS=2000;
constexpr uint32_t STEP_INTERVAL_US=12000; // 83.3 microsteps/s, commissioning only
constexpr uint32_t MAX_JOG_PULSES=160;
struct JogState {
  bool active=false;
  uint8_t axis=0;
  bool up=false;
  uint32_t started=0,lastHeartbeat=0,nextStepUs=0;
  uint32_t pulses=0;
  unsigned long id=0;
} jog;
uint8_t selectedAxis=0;
char incoming[256];
size_t incomingLength=0;
bool tooLong=false;
void replyError(unsigned long id,const char* code) {
 Serial.printf("{\"id\":%lu,\"status\":\"ERROR\",\"code\":\"%s\"}\n",id,code);
}
bool readId(const char* json,unsigned long &id) {
 const char *p=strstr(json,"\"id\"");
 if(!p || !(p=strchr(p+4,':')))return false;
 ++p;while(*p==' '||*p=='\t')++p;
 if(*p<'0'||*p>'9')return false;
 char *end=nullptr;id=strtoul(p,&end,10);
 while(*end==' '||*end=='\t')++end;
 return *end==','||*end=='}';
}
bool readCommand(const char* json,char *out,size_t cap) {
 const char *p=strstr(json,"\"cmd\"");
 if(!p || !(p=strchr(p+5,':')))return false;
 ++p;while(*p==' '||*p=='\t')++p;
 if(*p++!='"')return false;
 size_t n=0;
 while(*p && *p!='"') {
   if(*p=='\\'||n+1>=cap)return false;
   out[n++]=*p++;
 }
 if(*p!='"'||n==0)return false;
 out[n]=0;return true;
}
void disableDrivers(){
 digitalWrite(Z1_EN,HIGH);digitalWrite(Z2_EN,HIGH);
 digitalWrite(Z1_STEP,LOW);digitalWrite(Z2_STEP,LOW);
}
void stopJog(){
 jog.active=false;
 disableDrivers();
}
void replySelection(unsigned long id){
 Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"selected_axis\":%u,\"armed\":false,\"drivers_enabled\":%s}\n",
 id,selectedAxis,jog.active?"true":"false");
}
void dispatch(const char* data) {
 unsigned long id=0;char cmd[54];
 if(!readId(data,id)||!readCommand(data,cmd,sizeof(cmd))){replyError(0,"INVALID_REQUEST");return;}
 if(strcmp(cmd,"PING")==0){
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"reply\":\"PONG\"}\n",id);
 }else if(strcmp(cmd,"GET_INFO")==0){
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"device\":\"VALVER SMART DISPENSER\",\"firmware\":\"0.2.0-live-jog\",\"mode\":\"LIMITED_MOTOR_JOG\"}\n",id);
 }else if(strcmp(cmd,"GET_STATUS")==0 || strcmp(cmd,"GET_READINESS")==0){
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"state\":\"LIMITED_MOTOR_JOG\",\"motor_motion_ready\":true,\"motors_enabled\":%s,\"drivers_enabled\":%s,\"jog_active\":%s,\"selected_axis\":%u,\"max_jog_ms\":%lu,\"max_jog_pulses\":%lu,\"limits_verified_by_firmware\":false}\n",
     id,jog.active?"true":"false",jog.active?"true":"false",jog.active?"true":"false",selectedAxis,
     (unsigned long)MAX_JOG_MS,(unsigned long)MAX_JOG_PULSES);
 }else if(strcmp(cmd,"GET_SENSORS")==0){
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"optical\":{\"model\":\"EE-SX671\",\"configured\":false,\"triggered\":null},\"stallguard\":{\"model\":\"TMC2209 StallGuard4\",\"z1_uart\":false,\"z2_uart\":false},\"calibration_enabled\":false}\n",id);
 }else if(strcmp(cmd,"SELECT_Z1")==0 || strcmp(cmd,"SELECT_Z2")==0){
   stopJog();selectedAxis=strcmp(cmd,"SELECT_Z1")==0?1:2;replySelection(id);
 }else if(strcmp(cmd,"DISABLE_CHANNELS")==0||strcmp(cmd,"STOP_TEST")==0||strcmp(cmd,"JOG_GPIO_STOP")==0||strcmp(cmd,"JOG_STOP")==0){
   stopJog();selectedAxis=0;
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"state\":\"STOPPED\",\"drivers_enabled\":false}\n",id);
 }else if(strcmp(cmd,"JOG_MOTOR_START_Z1_UP")==0||strcmp(cmd,"JOG_MOTOR_START_Z1_DOWN")==0||
         strcmp(cmd,"JOG_MOTOR_START_Z2_UP")==0||strcmp(cmd,"JOG_MOTOR_START_Z2_DOWN")==0){
   if(jog.active){replyError(id,"BUSY");return;}
   uint8_t axis=strstr(cmd,"Z1")?1:2;
   if(selectedAxis!=axis){replyError(id,"CHANNEL_LOCKED");return;}
   const bool up=strstr(cmd,"_UP")!=nullptr;
   disableDrivers();
   digitalWrite(axis==1?Z1_DIR:Z2_DIR,up?HIGH:LOW);
   delayMicroseconds(20); // DIR setup prior to driver enable/STEP
   jog.axis=axis;jog.up=up;jog.started=millis();jog.lastHeartbeat=millis();
   jog.nextStepUs=micros()+1500;jog.pulses=0;jog.id=id;
   jog.active=true;
   digitalWrite(axis==1?Z1_EN:Z2_EN,LOW);
   Serial.printf("{\"id\":%lu,\"status\":\"ACCEPTED\",\"mode\":\"MOTOR_JOG\",\"axis\":\"Z%u\",\"direction\":\"%s\",\"drivers_enabled\":true}\n",id,axis,up?"UP":"DOWN");
 }else if(strcmp(cmd,"JOG_MOTOR_HEARTBEAT")==0){
   if(!jog.active){replyError(id,"NO_JOG");return;}
   jog.lastHeartbeat=millis();
 }else if(strcmp(cmd,"JOG_MOTOR_STOP")==0){
   stopJog();
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"state\":\"JOG_STOPPED\",\"drivers_enabled\":false}\n",id);
 }else if(strcmp(cmd,"CALIBRATE_TIP")==0||strcmp(cmd,"CALIBRATE_TABLE")==0||
          strcmp(cmd,"MOVE_TO_REST")==0||strcmp(cmd,"MOVE_TO_DISPENSE")==0){
   replyError(id,"SENSOR_REFERENCE_NOT_CONFIGURED");
 }else{
   replyError(id,"UNKNOWN_COMMAND");
 }
}
void serviceJog(){
 if(!jog.active)return;
 const uint32_t ms=millis();
 if(ms-jog.lastHeartbeat>HEARTBEAT_TIMEOUT_MS||ms-jog.started>=MAX_JOG_MS||jog.pulses>=MAX_JOG_PULSES){
   unsigned long id=jog.id;const uint32_t pulses=jog.pulses;
   stopJog();
   Serial.printf("{\"id\":%lu,\"status\":\"DONE\",\"mode\":\"MOTOR_JOG\",\"pulses\":%lu,\"reason\":\"WATCHDOG_OR_LIMIT\",\"drivers_enabled\":false}\n",id,(unsigned long)pulses);
   return;
 }
 const uint32_t us=micros();
 if((int32_t)(us-jog.nextStepUs)>=0){
   uint8_t pin=jog.axis==1?Z1_STEP:Z2_STEP;
   digitalWrite(pin,HIGH);delayMicroseconds(5);digitalWrite(pin,LOW);
   ++jog.pulses;
   jog.nextStepUs=micros()+STEP_INTERVAL_US;
 }
}
}
void setup(){
 digitalWrite(Z1_EN,HIGH);digitalWrite(Z2_EN,HIGH);
 digitalWrite(Z1_STEP,LOW);digitalWrite(Z2_STEP,LOW);
 digitalWrite(Z1_DIR,LOW);digitalWrite(Z2_DIR,LOW);
 pinMode(Z1_EN,OUTPUT);pinMode(Z2_EN,OUTPUT);
 pinMode(Z1_STEP,OUTPUT);pinMode(Z2_STEP,OUTPUT);
 pinMode(Z1_DIR,OUTPUT);pinMode(Z2_DIR,OUTPUT);
 disableDrivers();
 Serial.begin(valver::kDefaultBaudRate);
}
void loop(){
 while(Serial.available()>0){
   char c=(char)Serial.read();
   if(c=='\n'){
     if(tooLong)replyError(0,"MESSAGE_TOO_LONG");
     else if(incomingLength){incoming[incomingLength]=0;dispatch(incoming);}
     incomingLength=0;tooLong=false;
   }else if(c!='\r'&&!tooLong){
     if(incomingLength<sizeof(incoming)-1)incoming[incomingLength++]=c;
     else tooLong=true;
   }
 }
 serviceJog();
}
