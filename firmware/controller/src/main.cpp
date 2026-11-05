#include <Arduino.h>
#include <cstring>
#include <cstdlib>
#include <TMCStepper.h>
#include <Adafruit_NeoPixel.h>
#include "driver/ledc.h"
#include "config/SystemConfig.h"
#include "config/HardwareConfig.h"

// VALVER live jog commissioning. TMC2209 EN is active LOW.
// Single-axis continuous jog with USB heartbeat deadman.
// The driver CURRENT must be set for the actual motor before using this firmware.
namespace {
// USB-only cold commissioning. Neither motor movement nor TMC UART is permitted.
// A reviewed firmware change is required before any powered tests.
constexpr bool COLD_INSPECTION_ONLY=false; // UART diagnostic only; legacy jog forbidden
// Use only the onboard addressable RGB LED. No user pin selection and no extra wiring.
// Respect the Arduino board definition when available (normally GPIO48).
#if defined(RGB_BUILTIN)
constexpr uint8_t RGB_INTERNAL_PIN=RGB_BUILTIN;
#else
constexpr uint8_t RGB_INTERNAL_PIN=48;
#endif
uint8_t rgbDisplayedAxis=255,rgbFrame=0;
uint32_t rgbLastMs=0;
Adafruit_NeoPixel rgbLed(1,RGB_INTERNAL_PIN,NEO_GRB+NEO_KHZ800);
void rgbOff(){
 rgbLed.clear();
 rgbLed.show();
 rgbDisplayedAxis=0;
}
constexpr uint8_t Z1_STEP=4,Z1_DIR=5,Z1_EN=6;
constexpr uint8_t Z2_STEP=7,Z2_DIR=15,Z2_EN=16;
constexpr uint32_t HEARTBEAT_TIMEOUT_MS=350;
// Proposed per-driver UART wiring; see installation instructions. No motor-current writes.
constexpr uint8_t UART_Z1_RX=17, UART_Z1_TX=18;
constexpr uint8_t UART_Z2_RX=10, UART_Z2_TX=9; // GPIO8 reserved for optical sensor
constexpr uint32_t UART_BAUD=115200;
constexpr uint16_t STALL_MIN_SPEED_SPS=400; // below this sensorless detection is not used
constexpr uint32_t STALL_POLL_MS=30, STALL_START_IGNORE_MS=400;
HardwareSerial stallSerialZ1(1),stallSerialZ2(2);
TMC2209Stepper stallDriverZ1(&stallSerialZ1,0.11f,0);
TMC2209Stepper stallDriverZ2(&stallSerialZ2,0.11f,0);
struct StallAxis {
  bool uart=false,enabled=false,monitoring=false;
  uint8_t threshold=40,lowReadings=0;
  uint16_t result=0;
};
StallAxis stall[3];
uint32_t stallLastPoll=0,stallLastTelemetry=0;
TMC2209Stepper &stallDriver(uint8_t axis){return axis==1?stallDriverZ1:stallDriverZ2;}
bool probeStall(uint8_t axis){
  // 0x21 is the TMC2209 IOIN version code. No response means no valid feedback.
  bool ok=stallDriver(axis).version()==0x21;
  stall[axis].uart=ok;
  if(!ok){stall[axis].enabled=false;stall[axis].monitoring=false;}
  return ok;
}
// One identical, centralised StallGuard4 setup for both TMC2209 axes.
// No current, hold-current, microstep or motor-motion changes.
bool configureStallGuardAxis(uint8_t axis){
  if(!probeStall(axis))return false;
  TMC2209Stepper &d=stallDriver(axis);
  d.pdn_disable(true);
  d.en_spreadCycle(false); // enable StealthChop, required by SG4
  d.TCOOLTHRS(0xFFFFF);
  d.SGTHRS(stall[axis].threshold);
  // Verify UART write-back while the motor is stopped.
  return d.pdn_disable() && !d.en_spreadCycle()
      && d.TCOOLTHRS()==0xFFFFF && d.SGTHRS()==stall[axis].threshold;
}
struct DriverDiag { uint32_t gconf=0, chopconf=0, iholdIrun=0, pwmconf=0, tstep=0; uint16_t sg=0; bool ok=false; };
DriverDiag diagnostics[3];
// Read-only register comparison. This deliberately never copies driver current settings.
DriverDiag readDriverDiag(uint8_t axis){
  DriverDiag d{};
  auto &driver=stallDriver(axis);
  d.ok=probeStall(axis);
  if(!d.ok)return d;
  d.gconf=driver.GCONF();
  d.chopconf=driver.CHOPCONF();
  d.iholdIrun=driver.IHOLD_IRUN();
  d.pwmconf=driver.PWMCONF();
  d.tstep=driver.TSTEP();
  d.sg=driver.SG_RESULT();
  return d;
}
constexpr uint16_t MIN_SPEED_SPS=20, MAX_SPEED_SPS=1600;
constexpr float JOG_ACCEL_SPS2=600.0f;
constexpr uint32_t SPEED_RAMP_PERIOD_MS=20;
// LEDC hardware PWM runs independently of UART polling and the main loop.
constexpr ledc_mode_t STEP_MODE=LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t STEP_TIMER_Z1=LEDC_TIMER_0, STEP_TIMER_Z2=LEDC_TIMER_1;
constexpr ledc_channel_t STEP_CHANNEL_Z1=LEDC_CHANNEL_0, STEP_CHANNEL_Z2=LEDC_CHANNEL_1;
bool stepClockReady=false;
ledc_channel_t stepChannel(uint8_t axis){return axis==1?STEP_CHANNEL_Z1:STEP_CHANNEL_Z2;}
ledc_timer_t stepTimer(uint8_t axis){return axis==1?STEP_TIMER_Z1:STEP_TIMER_Z2;}
bool configureStepClock(){
  for(uint8_t axis=1;axis<=2;axis++){
    ledc_timer_config_t timer={};
    timer.speed_mode=STEP_MODE;
    timer.duty_resolution=LEDC_TIMER_13_BIT; // 80 Hz at APB/XTAL clock needs enough divider resolution
    timer.timer_num=stepTimer(axis);
    timer.freq_hz=80;
    timer.clk_cfg=LEDC_AUTO_CLK; // compatible with ESP-IDF bundled by PlatformIO
    if(ledc_timer_config(&timer)!=ESP_OK)return false;
    ledc_channel_config_t ch={};
    ch.gpio_num=axis==1?Z1_STEP:Z2_STEP;
    ch.speed_mode=STEP_MODE;
    ch.channel=stepChannel(axis);
    ch.intr_type=LEDC_INTR_DISABLE;
    ch.timer_sel=stepTimer(axis);
    ch.duty=0;
    ch.hpoint=0;
    if(ledc_channel_config(&ch)!=ESP_OK)return false;
  }
  return true;
}
void stopStepClock(){
  ledc_set_duty(STEP_MODE,STEP_CHANNEL_Z1,0);
  ledc_update_duty(STEP_MODE,STEP_CHANNEL_Z1);
  ledc_set_duty(STEP_MODE,STEP_CHANNEL_Z2,0);
  ledc_update_duty(STEP_MODE,STEP_CHANNEL_Z2);
}
bool setStepClock(uint8_t axis,uint16_t hz){
  return ledc_set_freq(STEP_MODE,stepTimer(axis),hz)==ESP_OK; // ESP_OK is zero; previous check inverted successful calls
}
void startStepClock(uint8_t axis){
  ledc_set_duty(STEP_MODE,stepChannel(axis),4096); // 50% of 13-bit PWM
  ledc_update_duty(STEP_MODE,stepChannel(axis));
}
uint16_t speedSps=160;
uint32_t stepIntervalUs=1000000UL/160;
struct JogState {
  bool active=false;
  uint8_t axis=0;
  bool up=false;
  uint32_t lastHeartbeat=0,started=0,lastRampMs=0;
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
 if(stepClockReady)stopStepClock();
}
void stopJog(){
 jog.active=false;
 disableDrivers();
}

struct Trial {bool active=false,high=false;uint8_t axis=0;uint32_t count=0;unsigned long id=0;uint32_t began=0,edge=0,heartbeat=0,lastRampMs=0;float actualSps=400.0f;} trial;
uint8_t armedAxis=0;
uint16_t armedMa=0;
uint32_t armedAt=0;
constexpr uint16_t HOLD_RMS_MIN_MA=100,HOLD_RMS_MAX_MA=800;
constexpr uint16_t HOLD_SPEED_MIN_SPS=80,HOLD_SPEED_MAX_SPS=8000,HOLD_START_SPS=400;
constexpr float HOLD_ACCEL_SPS2=6000.0f;
uint16_t holdTargetSps=1600; // Default is useful travel speed; HMI slider can select up to 8000.
constexpr uint32_t HOLD_MAX_MS=10000,HOLD_HEARTBEAT_TIMEOUT_MS=350;
void abortTrial(){
 digitalWrite(Z1_STEP,LOW);digitalWrite(Z2_STEP,LOW);
 disableDrivers();
 digitalWrite(Z1_DIR,LOW);digitalWrite(Z2_DIR,LOW);
 trial.active=false;trial.high=false;armedAxis=0;armedMa=0;armedAt=0;
}
void serviceRgb(){
 const uint8_t axis=trial.active?trial.axis:0;
 const uint32_t now=millis();
 if(axis==rgbDisplayedAxis&&(axis==0||now-rgbLastMs<180))return;
 rgbFrame=(axis!=rgbDisplayedAxis)?0:(uint8_t)((rgbFrame+1)%6);
 rgbDisplayedAxis=axis;rgbLastMs=now;
 if(axis==1){
   static const uint8_t g[6]={1,8,20,34,20,8};
   static const uint8_t b[6]={50,78,112,165,125,82};
   rgbLed.setPixelColor(0,0,g[rgbFrame],b[rgbFrame]);
 }else if(axis==2){
   static const uint8_t r[6]={62,95,135,175,125,82};
   static const uint8_t g[6]={0,0,3,8,3,0};
   rgbLed.setPixelColor(0,r[rgbFrame],g[rgbFrame],0);
 }else{
   rgbLed.clear();
 }
 rgbLed.show();
}
void serviceTrial(){
 if(!trial.active)return;
 const uint32_t t=micros(),ms=millis();
 if((uint32_t)(ms-trial.heartbeat)>HOLD_HEARTBEAT_TIMEOUT_MS ||
    (uint32_t)(t-trial.began)>HOLD_MAX_MS*1000UL){
   const unsigned long id=trial.id;const uint32_t steps=trial.count;
   abortTrial();
   Serial.printf(R"json({"id":%lu,"status":"DONE","reason":"HOLD_TIMEOUT_OR_LINK_LOST","pulses":%lu,"drivers_enabled":false})json" "\n",id,(unsigned long)steps);
   return;
 }
 if((uint32_t)(ms-trial.lastRampMs)>=20){
   const uint32_t elapsed=ms-trial.lastRampMs;
   trial.lastRampMs=ms;
   if(trial.actualSps<(float)holdTargetSps){
     trial.actualSps+=HOLD_ACCEL_SPS2*((float)elapsed/1000.0f);
     if(trial.actualSps>holdTargetSps)trial.actualSps=holdTargetSps;
   }else if(trial.actualSps>(float)holdTargetSps){
     trial.actualSps-=HOLD_ACCEL_SPS2*((float)elapsed/1000.0f);
     if(trial.actualSps<holdTargetSps)trial.actualSps=holdTargetSps;
   }
 }
 const uint32_t stepPeriodUs=1000000UL/(uint32_t)trial.actualSps;
 const uint8_t step=trial.axis==1?Z1_STEP:Z2_STEP;
 if(!trial.high && (uint32_t)(t-trial.edge)>=stepPeriodUs){
   digitalWrite(step,HIGH);trial.high=true;trial.edge=t;
 }else if(trial.high && (uint32_t)(t-trial.edge)>=20){
   digitalWrite(step,LOW);trial.high=false;trial.count++;
 }
}
bool coldCommandAllowed(const char* cmd){
 const char* const allowed[]={
   "PING","GET_INFO","GET_STATUS","GET_READINESS","GET_SENSORS",
   "GET_COLD_CHECK","COLD_SELF_TEST","SELECT_Z1","SELECT_Z2",
   "DISABLE_CHANNELS","STOP_TEST","JOG_GPIO_STOP","JOG_STOP","JOG_MOTOR_STOP",
   "GET_DRIVER_TEST","PREPARE_TEST","HOLD_START","HOLD_HEARTBEAT","SET_HOLD_SPEED"
 };
 for(const char* name:allowed)if(strcmp(cmd,name)==0)return true;
 return false;
}
// Local readback only. This does NOT validate external power, polarity or wiring.
void replyColdCheck(unsigned long id){
 if(trial.active){replyError(id,"TEST_RUNNING");return;}
 disableDrivers();
 const bool en1=digitalRead(Z1_EN)==HIGH,en2=digitalRead(Z2_EN)==HIGH;
 const bool st1=digitalRead(Z1_STEP)==LOW,st2=digitalRead(Z2_STEP)==LOW;
 const bool dir1=digitalRead(Z1_DIR)==LOW,dir2=digitalRead(Z2_DIR)==LOW;
 const bool safe=en1&&en2&&st1&&st2&&dir1&&dir2&&!jog.active&&!stepClockReady;
 Serial.printf(R"json({"id":%lu,"status":"OK","cold_check":{"mode":"HOLD_MOTOR_TEST","cold_lock":true,"gpio_local_safe":%s,"z1":{"en_gpio":6,"en_high":%s,"step_gpio":4,"step_low":%s,"dir_gpio":5,"dir_low":%s},"z2":{"en_gpio":16,"en_high":%s,"step_gpio":7,"step_low":%s,"dir_gpio":15,"dir_low":%s},"uart_tested":false,"wiring_verified":false,"vm_polarity_verified":false,"coils_verified":false}})json" "\n",
    id,safe?"true":"false",en1?"true":"false",st1?"true":"false",dir1?"true":"false",
    en2?"true":"false",st2?"true":"false",dir2?"true":"false");
}
void replySelection(unsigned long id){
 Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"selected_axis\":%u,\"armed\":false,\"drivers_enabled\":%s}\n",
 id,selectedAxis,jog.active?"true":"false");
}
void dispatch(const char* data) {
 unsigned long id=0;char cmd[54];
 if(!readId(data,id)||!readCommand(data,cmd,sizeof(cmd))){replyError(0,"INVALID_REQUEST");return;}
 if(!coldCommandAllowed(cmd)){replyError(id,"TEST_MODE_LEGACY_COMMAND_LOCKED");return;}
 if(strcmp(cmd,"SET_HOLD_SPEED")==0){
   if(trial.active){replyError(id,"STOP_BEFORE_SPEED_CHANGE");return;}
   unsigned long requested=0;
   if(!readUIntField(data,"speed_sps",requested)||
      requested<HOLD_SPEED_MIN_SPS||requested>HOLD_SPEED_MAX_SPS){
     replyError(id,"INVALID_HOLD_SPEED");return;
   }
   holdTargetSps=(uint16_t)requested;
   Serial.printf(R"json({"id":%lu,"status":"OK","hold_speed_sps":%u,"hold_min_sps":%u,"hold_max_sps":%u})json" "\n",
     id,holdTargetSps,HOLD_SPEED_MIN_SPS,HOLD_SPEED_MAX_SPS);
 }else if(strcmp(cmd,"GET_DRIVER_TEST")==0){
   if(trial.active){replyError(id,"TEST_RUNNING");return;}
   const bool z1=probeStall(1),z2=probeStall(2);
   Serial.printf(R"json({"id":%lu,"status":"OK","driver_test":{"z1_uart":%s,"z2_uart":%s,"power_and_motor_wiring_verified":false}})json" "\n",
     id,z1?"true":"false",z2?"true":"false");
 }else if(strcmp(cmd,"PREPARE_TEST")==0){
   if(trial.active){replyError(id,"TEST_RUNNING");return;}
   abortTrial();
   unsigned long axis=0,rated=0,confirm=0;
   if(!readUIntField(data,"axis",axis)||!readUIntField(data,"rated_ma",rated)||
      !readUIntField(data,"confirm",confirm)||axis<1||axis>2||
      rated<150||rated>2000||confirm!=1){replyError(id,"MOTOR_RATED_CURRENT_REQUIRED");return;}
   if(!probeStall((uint8_t)axis)){replyError(id,"UART_NOT_READY_NO_MOTION");return;}
   TMC2209Stepper &d=stallDriver((uint8_t)axis);
   const uint8_t before=d.IFCNT();
   d.pdn_disable(true);
   d.I_scale_analog(false);
   d.mstep_reg_select(true); // Force microstep setting from UART, not MS1/MS2 jumpers.
   d.toff(3);
   d.microsteps(16);
   d.en_spreadCycle(false);
   d.pwm_autoscale(true);
   // Use 90% of the motor's declared per-phase rated current (mA RMS).
   // Never run a TMC2209 unregulated: hard ceiling is a conservative 800 mA RMS.
   uint16_t target=(uint16_t)((rated*9UL)/10UL);
   if(target<HOLD_RMS_MIN_MA)target=HOLD_RMS_MIN_MA;
   if(target>HOLD_RMS_MAX_MA)target=HOLD_RMS_MAX_MA;
   d.rms_current(target);
   const uint8_t after=d.IFCNT();
   if(!probeStall((uint8_t)axis)||after==before||!d.pdn_disable()||d.I_scale_analog()||
      !d.mstep_reg_select()||d.toff()!=3||d.microsteps()!=16||
      d.rms_current()>rated||d.rms_current()>HOLD_RMS_MAX_MA){
     abortTrial();replyError(id,"UART_CURRENT_READBACK_FAILED");return;
   }
   armedAxis=(uint8_t)axis;armedMa=target;armedAt=millis();
   Serial.printf(R"json({"id":%lu,"status":"OK","test_prepared":true,"axis":%lu,"rms_target_ma":%u,"hold_max_ms":%lu,"drivers_enabled":false})json" "\n",id,axis,target,(unsigned long)HOLD_MAX_MS);
 }else if(strcmp(cmd,"HOLD_START")==0){
   unsigned long axis=0,up=0;
   if(!readUIntField(data,"axis",axis)||!readUIntField(data,"up",up)||axis<1||axis>2||up>1){replyError(id,"BAD_TEST_AXIS");return;}
   if(trial.active){replyError(id,"TEST_RUNNING");return;}
   if(!armedAt||armedAxis!=axis||(uint32_t)(millis()-armedAt)>10000){
     abortTrial();replyError(id,"PREPARE_EXPIRED");return;
   }
   if(!probeStall((uint8_t)axis)){abortTrial();replyError(id,"UART_LOST_NO_MOTION");return;}
   TMC2209Stepper &d=stallDriver((uint8_t)axis);
   if(d.drv_err()||d.toff()!=3||d.rms_current()>HOLD_RMS_MAX_MA){abortTrial();replyError(id,"DRIVER_FAULT_NO_MOTION");return;}
   disableDrivers();
   // Correct both axes' vertical orientation in software; do not swap motor coils.
   digitalWrite(axis==1?Z1_DIR:Z2_DIR,up?LOW:HIGH);
   digitalWrite(axis==1?Z1_STEP:Z2_STEP,LOW);delayMicroseconds(30);
   trial.active=true;trial.high=false;trial.axis=(uint8_t)axis;trial.count=0;trial.id=id;
   trial.began=micros();trial.actualSps=(float)(holdTargetSps<HOLD_START_SPS?holdTargetSps:HOLD_START_SPS);
   trial.edge=trial.began-(1000000UL/(uint32_t)trial.actualSps);
   trial.heartbeat=millis();trial.lastRampMs=trial.heartbeat;
   digitalWrite(axis==1?Z1_EN:Z2_EN,LOW);
   Serial.printf(R"json({"id":%lu,"status":"ACCEPTED","hold_running":true,"axis":%lu,"rms_target_ma":%u,"hold_speed_sps":%u,"drivers_enabled":true})json" "\n",id,axis,armedMa,holdTargetSps);
 }else if(strcmp(cmd,"HOLD_HEARTBEAT")==0){
   if(trial.active)trial.heartbeat=millis();
 }else if(strcmp(cmd,"GET_COLD_CHECK")==0 || strcmp(cmd,"COLD_SELF_TEST")==0){
   replyColdCheck(id);
 }else if(strcmp(cmd,"PING")==0){
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"reply\":\"PONG\"}\n",id);
 }else if(strcmp(cmd,"GET_INFO")==0){
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"device\":\"VALVER SMART DISPENSER\",\"firmware\":\"0.9.7-fast-z\",\"mode\":\"HOLD_MOTOR_TEST\"}\n",id);
 }else if(strcmp(cmd,"GET_STATUS")==0 || strcmp(cmd,"GET_READINESS")==0){
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"state\":\"HOLD_MOTOR_TEST\",\"motor_motion_ready\":%s,\"motors_enabled\":%s,\"drivers_enabled\":%s,\"jog_active\":%s,\"selected_axis\":%u,\"hold_speed_sps\":%u,\"hold_min_sps\":%u,\"hold_max_sps\":%u,\"speed_sps\":%u,\"speed_min_sps\":%u,\"speed_max_sps\":%u,\"limits_verified_by_firmware\":false}\n",id,stepClockReady?"true":"false",trial.active?"true":"false",trial.active?"true":"false",trial.active?"true":"false",selectedAxis,holdTargetSps,HOLD_SPEED_MIN_SPS,HOLD_SPEED_MAX_SPS,speedSps,MIN_SPEED_SPS,MAX_SPEED_SPS);
 }else if(strcmp(cmd,"SET_SPEED")==0){
   if(jog.active){replyError(id,"STOP_BEFORE_SPEED_CHANGE");return;}
   unsigned long requested=0;
   if(!readSpeed(data,requested)||requested<MIN_SPEED_SPS||requested>MAX_SPEED_SPS){replyError(id,"INVALID_SPEED");return;}
   speedSps=(uint16_t)requested;stepIntervalUs=1000000UL/speedSps;
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"speed_sps\":%u}\n",id,speedSps);
 }else if(strcmp(cmd,"DIAG_TMC")==0){
   if(jog.active){replyError(id,"STOP_BEFORE_DIAG");return;}
   for(uint8_t a=1;a<=2;a++)diagnostics[a]=readDriverDiag(a);
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"driver_diag\":{\"z1\":{\"uart\":%s,\"gconf\":%lu,\"chopconf\":%lu,\"ihold_irun\":%lu,\"pwmconf\":%lu,\"tstep\":%lu,\"sg_result\":%u},\"z2\":{\"uart\":%s,\"gconf\":%lu,\"chopconf\":%lu,\"ihold_irun\":%lu,\"pwmconf\":%lu,\"tstep\":%lu,\"sg_result\":%u}}}\n",
     id,
     diagnostics[1].ok?"true":"false",(unsigned long)diagnostics[1].gconf,(unsigned long)diagnostics[1].chopconf,(unsigned long)diagnostics[1].iholdIrun,(unsigned long)diagnostics[1].pwmconf,(unsigned long)diagnostics[1].tstep,diagnostics[1].sg,
     diagnostics[2].ok?"true":"false",(unsigned long)diagnostics[2].gconf,(unsigned long)diagnostics[2].chopconf,(unsigned long)diagnostics[2].iholdIrun,(unsigned long)diagnostics[2].pwmconf,(unsigned long)diagnostics[2].tstep,diagnostics[2].sg);
 }else if(strcmp(cmd,"MONITOR_STALL")==0){
   if(jog.active){replyError(id,"STOP_BEFORE_CONFIG");return;}
   unsigned long axis=0;
   if(!readUIntField(data,"axis",axis)||axis<1||axis>2){replyError(id,"BAD_AXIS");return;}
   if(!configureStallGuardAxis((uint8_t)axis)){
     stall[axis].monitoring=false;stall[axis].enabled=false;
     replyError(id,"STALLGUARD_CONFIG_READBACK_FAILED");return;
   }
   stall[axis].monitoring=true;  // Monitoring only: no automatic force stop
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"axis\":%lu,\"monitoring\":true,\"stall_enabled\":%s}\n",id,axis,stall[axis].enabled?"true":"false");
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
     if(!configureStallGuardAxis((uint8_t)axis)){
       stall[axis].enabled=false;
       replyError(id,"STALLGUARD_CONFIG_READBACK_FAILED");return;
     }
     stall[axis].monitoring=true;
   }
   stall[axis].enabled=enabled!=0;
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"axis\":%lu,\"stall_enabled\":%s}\n",id,axis,stall[axis].enabled?"true":"false");
 }else if(strcmp(cmd,"GET_SENSORS")==0){
   if(!COLD_INSPECTION_ONLY && !jog.active){probeStall(1);probeStall(2);}
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"optical\":{\"configured\":false},\"stallguard\":{\"model\":\"TMC2209 StallGuard4\",\"z1_uart\":%s,\"z2_uart\":%s,\"z1_enabled\":%s,\"z2_enabled\":%s,\"z1_monitoring\":%s,\"z2_monitoring\":%s,\"z1_threshold\":%u,\"z2_threshold\":%u,\"z1_result\":%u,\"z2_result\":%u,\"min_speed_sps\":%u},\"calibration_enabled\":false}\n",
   id,stall[1].uart?"true":"false",stall[2].uart?"true":"false",stall[1].enabled?"true":"false",stall[2].enabled?"true":"false",
   stall[1].monitoring?"true":"false",stall[2].monitoring?"true":"false",
   stall[1].threshold,stall[2].threshold,stall[1].result,stall[2].result,STALL_MIN_SPEED_SPS);
 }else if(strcmp(cmd,"SELECT_Z1")==0 || strcmp(cmd,"SELECT_Z2")==0){
   stopJog();selectedAxis=strcmp(cmd,"SELECT_Z1")==0?1:2;replySelection(id);
 }else if(strcmp(cmd,"DISABLE_CHANNELS")==0||strcmp(cmd,"STOP_TEST")==0||strcmp(cmd,"JOG_GPIO_STOP")==0||strcmp(cmd,"JOG_STOP")==0){
   abortTrial();stopJog();selectedAxis=0;
   Serial.printf("{\"id\":%lu,\"status\":\"OK\",\"state\":\"STOPPED\",\"drivers_enabled\":false}\n",id);
 }else if(strcmp(cmd,"JOG_MOTOR_START_Z1_UP")==0||strcmp(cmd,"JOG_MOTOR_START_Z1_DOWN")==0||
         strcmp(cmd,"JOG_MOTOR_START_Z2_UP")==0||strcmp(cmd,"JOG_MOTOR_START_Z2_DOWN")==0){
   if(jog.active){replyError(id,"BUSY");return;}
   if(!stepClockReady){replyError(id,"STEP_CLOCK_NOT_READY");return;}
   uint8_t axis=strstr(cmd,"Z1")?1:2;
   if(selectedAxis!=axis){replyError(id,"CHANNEL_LOCKED");return;}
   const bool up=strstr(cmd,"_UP")!=nullptr;
   disableDrivers();
   digitalWrite(axis==1?Z1_DIR:Z2_DIR,up?HIGH:LOW);
   delayMicroseconds(20); // DIR setup prior to driver enable/STEP
   jog.axis=axis;jog.up=up;jog.lastHeartbeat=millis();jog.started=millis();stall[axis].lowReadings=0;
   jog.pulses=0;jog.id=id;
   jog.actualSpeedSps=(float)(speedSps<80?speedSps:80);
   jog.lastRampMs=millis();
   if(!setStepClock(axis,(uint16_t)jog.actualSpeedSps)){replyError(id,"STEP_CLOCK_CONFIG_FAILED");return;}
   jog.active=true;
   digitalWrite(axis==1?Z1_EN:Z2_EN,LOW);
   startStepClock(axis);
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
 if(!jog.active || !stall[jog.axis].uart || !stall[jog.axis].monitoring)return; // live telemetry independent of force-stop
 const uint32_t now=millis();
 const uint8_t axis=jog.axis;
 if(jog.actualSpeedSps<STALL_MIN_SPEED_SPS || jog.actualSpeedSps+5.0f<(float)speedSps || now-jog.started<STALL_START_IGNORE_MS)return; // read only at stable usable speed
 if(now-stallLastPoll<STALL_POLL_MS)return;
 stallLastPoll=now;
 const bool stoppingOnLoad=stall[axis].enabled;
 if(!probeStall(axis)){
   if(!stoppingOnLoad)return;
   const unsigned long id=jog.id;
   stopJog();
   Serial.printf("{\"id\":%lu,\"status\":\"DONE\",\"reason\":\"STALL_UART_LOST\",\"axis\":%u,\"drivers_enabled\":false}\n",id,axis);
   return;
 }
 StallAxis &s=stall[axis];
 s.result=stallDriver(axis).SG_RESULT();
 if(now-stallLastTelemetry>=250){
   const uint32_t z1Tstep=axis==1?stallDriverZ1.TSTEP():0; // Z1-only diagnostic, rate-limited to telemetry
   stallLastTelemetry=now;
   Serial.printf("{\"status\":\"SAMPLE\",\"axis\":%u,\"sg_result\":%u,\"threshold\":%u,\"speed_sps\":%u,\"direction\":\"%s\",\"millis\":%lu,\"tstep\":%lu}\n",axis,s.result,s.threshold,(unsigned)jog.actualSpeedSps,jog.up?"UP":"DOWN",(unsigned long)now,(unsigned long)z1Tstep);
 }
 if(s.result<=2U*s.threshold){if(s.lowReadings<3)s.lowReadings++;}
 else s.lowReadings=0;
 if(s.enabled && s.lowReadings>=2){
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
   unsigned long id=jog.id;
   stopJog();
   Serial.printf("{\"id\":%lu,\"status\":\"DONE\",\"mode\":\"MOTOR_JOG\",\"reason\":\"HEARTBEAT_LOST\",\"drivers_enabled\":false}\n",id);
   return;
 }
 if(ms-jog.lastRampMs>=SPEED_RAMP_PERIOD_MS){
   const uint32_t elapsed=ms-jog.lastRampMs;
   jog.lastRampMs=ms;
   if(jog.actualSpeedSps<(float)speedSps){
     jog.actualSpeedSps+=JOG_ACCEL_SPS2*((float)elapsed/1000.0f);
     if(jog.actualSpeedSps>(float)speedSps)jog.actualSpeedSps=(float)speedSps;
     setStepClock(jog.axis,(uint16_t)jog.actualSpeedSps);
   }
 }
}
}
void setup(){
 // Set EN high (driver disabled), STEP/DIR low before touching other peripherals.
 digitalWrite(Z1_EN,HIGH);digitalWrite(Z2_EN,HIGH);
 digitalWrite(Z1_STEP,LOW);digitalWrite(Z2_STEP,LOW);
 digitalWrite(Z1_DIR,LOW);digitalWrite(Z2_DIR,LOW);
 pinMode(Z1_EN,OUTPUT);pinMode(Z2_EN,OUTPUT);
 pinMode(Z1_STEP,OUTPUT);pinMode(Z2_STEP,OUTPUT);
 pinMode(Z1_DIR,OUTPUT);pinMode(Z2_DIR,OUTPUT);
 disableDrivers();
 if(!COLD_INSPECTION_ONLY){
   stallSerialZ1.begin(UART_BAUD,SERIAL_8N1,UART_Z1_RX,UART_Z1_TX);
   stallSerialZ2.begin(UART_BAUD,SERIAL_8N1,UART_Z2_RX,UART_Z2_TX);
   stallSerialZ1.setTimeout(15);
   stallSerialZ2.setTimeout(15);
   stepClockReady=false; // no legacy PWM step clock
 }else{
   // UART pins left as inputs: no traffic into unpowered TMC2209.
   pinMode(UART_Z1_RX,INPUT);pinMode(UART_Z1_TX,INPUT);
   pinMode(UART_Z2_RX,INPUT);pinMode(UART_Z2_TX,INPUT);
   stepClockReady=false; // no PWM STEP timer configured
 }
 disableDrivers();
 rgbLed.begin();
 rgbLed.setBrightness(55);
 rgbOff();
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
 serviceTrial();
 serviceRgb();
 // Legacy motion service disabled.
}
