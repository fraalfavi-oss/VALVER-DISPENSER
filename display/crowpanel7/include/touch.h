#ifndef VALVER_CROWPANEL_GT911_H
#define VALVER_CROWPANEL_GT911_H

// CrowPanel 7" V3.0: GT911 touch on Wire1 GPIO19/20.
// Robust init: leave PCA9557/RST/INT pins untouched, recognize both GT911
// addresses, poll conservatively and report I2C failures on CH340 serial.
#include <Arduino.h>
#include <Wire.h>

constexpr uint8_t TOUCH_SDA=19, TOUCH_SCL=20;
constexpr uint8_t GT911_ADDRESS_A=0x5D, GT911_ADDRESS_B=0x14;
constexpr uint16_t GT911_ID_REG=0x8140, GT911_POINT_INFO=0x814E;
constexpr uint16_t GT911_FIRST_POINT=0x814F;

int touch_last_x=0, touch_last_y=0;
bool touch_is_pressed=false, touch_available=false;
uint8_t touch_address=0;
uint32_t touch_last_event_ms=0, touch_reads=0, touch_failures=0;

static bool gt911_read_addr(uint8_t address,uint16_t reg,uint8_t *data,size_t size) {
 if(address==0||size==0)return false;
 Wire1.beginTransmission(address);
 Wire1.write((uint8_t)(reg>>8));
 Wire1.write((uint8_t)reg);
 if(Wire1.endTransmission(false)!=0)return false;
 if(Wire1.requestFrom(address,size)!=(int)size)return false;
 for(size_t i=0;i<size;i++)data[i]=Wire1.read();
 return true;
}
static bool gt911_write_byte(uint16_t reg,uint8_t value){
 if(!touch_available)return false;
 Wire1.beginTransmission(touch_address);
 Wire1.write((uint8_t)(reg>>8));
 Wire1.write((uint8_t)reg);
 Wire1.write(value);
 return Wire1.endTransmission()==0;
}
static bool gt911_probe(uint8_t candidate){
 // Try product ID, not a full bus scan: an I2C scan can upset GT911 boot.
 uint8_t id[4]={0};
 for(uint8_t tries=0;tries<2;tries++){
  if(gt911_read_addr(candidate,GT911_ID_REG,id,sizeof(id))){
   Serial.printf("[TOUCH] GT911 en 0x%02X ID=%02X %02X %02X %02X\n",
      candidate,id[0],id[1],id[2],id[3]);
   touch_address=candidate;
   touch_available=true;
   return true;
  }
  delay(15);
 }
 return false;
}
bool touch_init(){
 touch_available=false;
 touch_is_pressed=false;
 touch_address=0;
 // Do NOT reset or write PCA9557 here. On this board that can hold the
 // GT911 in reset; its hardware pull-up already performs power-on release.
 delay(150);
 if(!gt911_probe(GT911_ADDRESS_A)&&!gt911_probe(GT911_ADDRESS_B)){
  Serial.println("[TOUCH] GT911 no responde en 0x5D ni 0x14, SDA=19 SCL=20.");
  Serial.println("[TOUCH] Revisar revision CrowPanel V3.0 y pines compartidos USB.");
  return false;
 }
 Serial.printf("[TOUCH] GT911 LISTO: 0x%02X (I2C 100kHz)\n",touch_address);
 return true;
}
bool touch_touched(){
 static uint32_t last_sample=0,last_log=0;
 const uint32_t now=millis();
 if(now-last_sample<25)return touch_is_pressed;
 last_sample=now;
 if(!touch_available)return false; // Do not block display refresh with I2C retries.

 uint8_t status=0;
 if(!gt911_read_addr(touch_address,GT911_POINT_INFO,&status,1)){
  touch_failures++;
  touch_is_pressed=false;
  if(now-last_log>3000){
   last_log=now;
   Serial.printf("[TOUCH] I2C lectura fallida, addr=0x%02X, errores=%lu\n",
                 touch_address,(unsigned long)touch_failures);
  }
  return false;
 }
 touch_reads++;
 // GT911 publishes changes on status bit7; no new sample means keep touch
 // briefly to avoid phantom up/down transitions while finger stays pressed.
 if((status&0x80)==0){
  if(now-touch_last_event_ms>90)touch_is_pressed=false;
  return touch_is_pressed;
 }
 const uint8_t count=status&0x0F;
 bool pressed=(count>0&&count<=5);
 if(pressed){
  uint8_t point[7]={0};
  if(gt911_read_addr(touch_address,GT911_FIRST_POINT,point,sizeof(point))){
   const uint16_t rx=(uint16_t)point[1]|((uint16_t)point[2]<<8);
   const uint16_t ry=(uint16_t)point[3]|((uint16_t)point[4]<<8);
   if(rx<800&&ry<480){
    touch_last_x=rx;
    touch_last_y=ry;
    touch_last_event_ms=now;
   }else pressed=false;
  }else pressed=false;
 }
 touch_is_pressed=pressed;
 if(!gt911_write_byte(GT911_POINT_INFO,0))touch_failures++;
 return touch_is_pressed;
}
#endif
