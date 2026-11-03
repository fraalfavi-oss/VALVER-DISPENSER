#include <Arduino.h>
#include <cstring>
#include <cstdlib>
#include <TMCStepper.h>
#include "config/SystemConfig.h"
#include "config/HardwareConfig.h"

// VALVER live jog commissioning. TMC2209 EN is active LOW.
// Single-axis continuous jog with USB heartbeat deadman.
// The driver CURRENT must be set for the actual motor before using this firmware.
namespace {
constexpr uint8_t Z1_STEP=4,Z1_DIR=5,Z1_EN=6;
constexpr uint8_t Z2_STEP=7,Z2_DIR=15,Z2_EN=16;
constexpr uint32_t HEARTBEAT_TIMEOUT_MS=350;
// Proposed per-driver UART wiring; see installation instructions. No motor-current writes.
constexpr uint8_t UART_Z1_RX=17, UART_Z1_TX=18;
constexpr uint8_t UART_Z2_RX=8, UART_Z2_TX=9;
constexpr uint32_t UART_BAUD=115200;
constexpr uint16_t STALL_MIN_SPEED_SPS=400; // below this sensorless detection is not used
constexpr uint32_t STALL_POLL_MS=30, STALL_START_IGNORE_MS=400;
HardwareSerial stallSerialZ1(1),stallSerialZ2(2);
TMC2209Stepper stallDriverZ1(&stallSerialZ1,0.11f,0);
TMC2209Stepper stallDriverZ2(&stallSerialZ2,0.11f,0);
struct StallAxis {
  bool uart=false,enabled=false;
  uint8_t threshold=40,lowReadings=0;
  uint16_t result=0;
};
StallAxis stall[3];
uint32_t stallLastPoll=0;
TMC2209Stepper &stallDriver(uint8_t axis){return axis==1?stallDriverZ1:stallDriverZ2;}
bool probeStall(uint8_t axis){
  // 0x21 is the TMC2209 IOIN version code. No response means no valid feedback.
  bool ok=stallDriver(axis).version()==0x21;
  stall[axis].uart=ok;
  if(!ok)stall[axis].enabled=false;
  return ok;
}
constexpr uint16_t MIN_SPEED_SPS=20, MAX_SPEED_SPS=1600;
constexpr float JOG_ACCEL_SPS2=600.0f; // smooth start, avoid instantaneous high-speed step changes
uint16_t speedSps=160;
uint32_t stepIntervalUs=1000000UL/160;
struct JogState {
  bool active=false;
  uint8_t axis=0;
  bool up=false;
  uint32_t lastHeartbeat=0,nextStepUs=0,started=0;
  uint32_t pulses=0;
  float actualSpeedSps=0.0f;
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
bool readSpeed(const char *json,unsigned long &value){
 const char *p=strstr(json,"\"speed_sps\"");
 if(!p || !(p=strchr(p+11,':')))return false;
 ++p;while(*p==' '||*p=='\t')++p;
 if(*p<'0'||*p>'9')return false;
 char *end=nullptr;value=strtoul(p,&end,10);
 return end!=p && (*end==','||*end=='}'||*end==' ');
}
bool readUIntField(const char *json,const char *key,unsigned long &value){
 char needle[40];snprintf(needle,sizeof(needle),"\"%s\"",key);
 const char *p=strstr(json,needle);
 if(!p || !(p=strchr(p+strlen(needle),':')))return false;
 ++p;while(*p==' '||*p=='\t')++p;
 if(*p<'0'||*p>'9')return false;
 char *end=nullptr;value=strtoul(p,&end,10);
 return end!=p && (*end==','||*end=='}'||*end==' ');
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
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"device\":\"VALVER SMART DISPENSER\",\"firmware\":\"0.5.0-stallguard-uart\",\"mode\":\"HOLD_MOTOR_JOG\"}\n",id);
 }else if(strcmp(cmd,"GET_STATUS")==0 || strcmp(cmd,"GET_READINESS")==0){
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"state\":\"HOLD_MOTOR_JOG\",\"motor_motion_ready\":true,\"motors_enabled\":%s,\"drivers_enabled\":%s,\"jog_active\":%s,\"selected_axis\":%u,\"speed_sps\":%u,\"speed_min_sps\":%u,\"speed_max_sps\":%u,\"limits_verified_by_firmware\":false}\n",id,jog.active?"true":"false",jog.active?"true":"false",jog.active?"true":"false",selectedAxis,speedSps,MIN_SPEED_SPS,MAX_SPEED_SPS);
 }else if(strcmp(cmd,"SET_SPEED")==0){
   if(jog.active){replyError(id,"STOP_BEFORE_SPEED_CHANGE");return;}
   unsigned long requested=0;
   if(!readSpeed(data,requested)||requested<MIN_SPEED_SPS||requested>MAX_SPEED_SPS){replyError(id,"INVALID_SPEED");return;}
   speedSps=(uint16_t)requested;stepIntervalUs=1000000UL/speedSps;
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"speed_sps\":%u}\n",id,speedSps);
 }else if(strcmp(cmd,"SET_STALL_SENSITIVITY")==0){
   if(jog.active){replyError(id,"STOP_BEFORE_CONFIG");return;}
   unsigned long axis=0,threshold=0;
   if(!readUIntField(data,"axis",axis)||!readUIntField(data,"threshold",threshold)||axis<1||axis>2||threshold>255){replyError(id,"BAD_STALL_SETTING");return;}
   stall[axis].threshold=(uint8_t)threshold;
   if(stall[axis].uart)stallDriver(axis).SGTHRS(stall[axis].threshold);
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"axis\":%lu,\"threshold\":%lu,\"uart_ready\":%s}\n",id,axis,threshold,stall[axis].uart?"true":"false");
 }else if(strcmp(cmd,"ENABLE_STALL")==0){
   if(jog.active){replyError(id,"STOP_BEFORE_CONFIG");return;}
   unsigned long axis=0,enabled=0;
   if(!readUIntField(data,"axis",axis)||!readUIntField(data,"enabled",enabled)||axis<1||axis>2||enabled>1){replyError(id,"BAD_STALL_SETTING");return;}
   if(enabled){
     if(!probeStall((uint8_t)axis)){replyError(id,"UART_NOT_CONNECTED");return;}
     auto &d=stallDriver((uint8_t)axis);
     d.pdn_disable(true);
     d.en_spreadCycle(false); // StallGuard4 requires StealthChop
     d.TCOOLTHRS(0xFFFFF);
     d.SGTHRS(stall[axis].threshold);
   }
   stall[axis].enabled=enabled!=0;
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"axis\":%lu,\"stall_enabled\":%s}\n",id,axis,stall[axis].enabled?"true":"false");
 }else if(strcmp(cmd,"GET_SENSORS")==0){
   if(!jog.active){probeStall(1);probeStall(2);}
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"optical\":{\"configured\":false},\"stallguard\":{\"model\":\"TMC2209 StallGuard4\",\"z1_uart\":%s,\"z2_uart\":%s,\"z1_enabled\":%s,\"z2_enabled\":%s,\"z1_threshold\":%u,\"z2_threshold\":%u,\"z1_result\":%u,\"z2_result\":%u,\"min_speed_sps\":%u},\"calibration_enabled\":false}\n",
   id,stall[1].uart?"true":"false",stall[2].uart?"true":"false",stall[1].enabled?"true":"false",stall[2].enabled?"true":"false",
   stall[1].threshold,stall[2].threshold,stall[1].result,stall[2].result,STALL_MIN_SPEED_SPS);
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
   jog.axis=axis;jog.up=up;jog.lastHeartbeat=millis();jog.started=millis();stall[axis].lowReadings=0;
   jog.nextStepUs=micros()+1500;jog.pulses=0;jog.id=id;
   jog.actualSpeedSps=(float)(speedSps<80?speedSps:80);
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
void serviceStall(){
 if(!jog.active || !stall[jog.axis].enabled)return;
 const uint32_t now=millis();
 const uint8_t axis=jog.axis;
 if(jog.actualSpeedSps<STALL_MIN_SPEED_SPS || now-jog.started<STALL_START_IGNORE_MS)return;
 if(now-stallLastPoll<STALL_POLL_MS)return;
 stallLastPoll=now;
 if(!probeStall(axis)){
   const unsigned long id=jog.id;
   stopJog();
   Serial.printf("{\"id\":%lu,\"status\":\"DONE\",\"reason\":\"STALL_UART_LOST\",\"axis\":%u,\"drivers_enabled\":false}\n",id,axis);
   return;
 }
 StallAxis &s=stall[axis];
 s.result=stallDriver(axis).SG_RESULT();
 if(s.result<=2U*s.threshold)++s.lowReadings;
 else s.lowReadings=0;
 if(s.lowReadings>=2){
   const unsigned long id=jog.id;
   const uint16_t result=s.result;
   stopJog();
   Serial.printf("{\"id\":%lu,\"status\":\"DONE\",\"reason\":\"STALL_DETECTED\",\"axis\":%u,\"sg_result\":%u,\"drivers_enabled\":false}\n",id,axis,result);
 }
}
void serviceJog(){
 if(!jog.active)return;
 const uint32_t ms=millis();
 if(ms-jog.lastHeartbeat>HEARTBEAT_TIMEOUT_MS){
   unsigned long id=jog.id;const uint32_t pulses=jog.pulses;
   stopJog();
   Serial.printf("{\"id\":%lu,\"status\":\"DONE\",\"mode\":\"MOTOR_JOG\",\"pulses\":%lu,\"reason\":\"HEARTBEAT_LOST\",\"drivers_enabled\":false}\n",id,(unsigned long)pulses);
   return;
 }
 const uint32_t us=micros();
 if((int32_t)(us-jog.nextStepUs)>=0){
   uint8_t pin=jog.axis==1?Z1_STEP:Z2_STEP;
   digitalWrite(pin,HIGH);delayMicroseconds(5);digitalWrite(pin,LOW);
   ++jog.pulses;
   if(jog.actualSpeedSps<(float)speedSps){
     jog.actualSpeedSps += JOG_ACCEL_SPS2/jog.actualSpeedSps;
     if(jog.actualSpeedSps>(float)speedSps)jog.actualSpeedSps=(float)speedSps;
   }
   jog.nextStepUs=micros()+(uint32_t)(1000000.0f/jog.actualSpeedSps);
 }
}
}
void setup(){
 stallSerialZ1.begin(UART_BAUD,SERIAL_8N1,UART_Z1_RX,UART_Z1_TX);
 stallSerialZ2.begin(UART_BAUD,SERIAL_8N1,UART_Z2_RX,UART_Z2_TX);
 stallSerialZ1.setTimeout(15);
 stallSerialZ2.setTimeout(15);
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
 serviceStall();
}
