#ifndef VALVER_CROWPANEL_GT911_H
#define VALVER_CROWPANEL_GT911_H

// Elecrow CrowPanel V3.0 GT911, exact official PCA9557 0x00->0x01->0x02 boot sequence.
// All I2C devices use Wire1 on GPIO19(SDA), GPIO20(SCL).
// No motor GPIO, UART or movement commands in this driver.
#include <Arduino.h>
#include <Wire.h>

constexpr uint8_t TOUCH_SDA=19, TOUCH_SCL=20;
constexpr uint8_t PCA9557_ADDRESS=0x18;
constexpr uint8_t PCA9557_OUTPUT=0x01, PCA9557_CONFIG=0x03;
constexpr uint8_t GT911_ADDRESS_A=0x5D, GT911_ADDRESS_B=0x14;
constexpr uint16_t GT911_ID_REG=0x8140, GT911_POINT_INFO=0x814E;
constexpr uint16_t GT911_FIRST_POINT=0x814F;

int touch_last_x=0,touch_last_y=0;
bool touch_is_pressed=false,touch_available=false;
uint8_t touch_address=0;
uint32_t touch_reads=0,touch_failures=0,touch_last_event_ms=0;

// Ping first: avoids repeated ESP-IDF I2C error 259 for missing addresses.
static bool touch_i2c_ack(uint8_t address){
 Wire1.beginTransmission(address);
 return Wire1.endTransmission()==0;
}

static bool touch_pca_write(uint8_t reg,uint8_t value){
 Wire1.beginTransmission(PCA9557_ADDRESS);
 Wire1.write(reg);
 Wire1.write(value);
 return Wire1.endTransmission()==0;
}

static bool touch_pca_read(uint8_t reg,uint8_t &value){
 Wire1.beginTransmission(PCA9557_ADDRESS);
 Wire1.write(reg);
 if(Wire1.endTransmission(false)!=0)return false;
 if(Wire1.requestFrom(PCA9557_ADDRESS,(uint8_t)1)!=1)return false;
 value=(uint8_t)Wire1.read();
 return true;
}

static bool touch_gt911_read(uint8_t address,uint16_t reg,uint8_t *data,size_t count){
 if(address==0||count==0)return false;
 Wire1.beginTransmission(address);
 Wire1.write((uint8_t)(reg>>8));
 Wire1.write((uint8_t)reg);
 if(Wire1.endTransmission(false)!=0)return false;
 if(Wire1.requestFrom(address,count)!=count)return false;
 for(size_t i=0;i<count;i++)data[i]=(uint8_t)Wire1.read();
 return true;
}
static bool touch_gt911_clear(){
 Wire1.beginTransmission(touch_address);
 Wire1.write((uint8_t)(GT911_POINT_INFO>>8));
 Wire1.write((uint8_t)GT911_POINT_INFO);
 Wire1.write((uint8_t)0);
 return Wire1.endTransmission()==0;
}

bool touch_init(){
 touch_available=false;touch_is_pressed=false;touch_address=0;
 Serial.printf("[I2C] GPIO19(SDA)=%d GPIO20(SCL)=%d\n",
               digitalRead(TOUCH_SDA),digitalRead(TOUCH_SCL));
 const bool pca=touch_i2c_ack(PCA9557_ADDRESS);
 Serial.printf("[I2C] PCA9557 0x18: %s\n",pca?"OK":"SIN RESPUESTA");

 if(pca){
  // Elecrow V3.0 official init uses ALL 8 expander pins as OUTPUTS
  // during GT911 power/reset sequencing, then IO1 as INPUT.
  // Previous code left pins IO2..IO7 as inputs (PCA CFG 0xFC/0xFE),
  // which is not equivalent to the working official sequence.
  const bool stageOutput=touch_pca_write(PCA9557_OUTPUT,0x00);
  const bool stageConfig=touch_pca_write(PCA9557_CONFIG,0x00);
  delay(20);
  const bool stageRelease=touch_pca_write(PCA9557_OUTPUT,0x01);
  delay(100);
  const bool stageInput=touch_pca_write(PCA9557_CONFIG,0x02);
  delay(60);
  uint8_t config=0,inputs=0,outputs=0;
  const bool cfg_ok=touch_pca_read(PCA9557_CONFIG,config);
  const bool in_ok=touch_pca_read(0x00,inputs);
  const bool out_ok=touch_pca_read(PCA9557_OUTPUT,outputs);
  Serial.printf("[TOUCH] Elecrow secuencia completa: %s\n",
    (stageOutput&&stageConfig&&stageRelease&&stageInput)?"I2C OK":"ERROR DE ESCRITURA");
  if(cfg_ok&&in_ok&&out_ok){
   Serial.printf("[I2C] PCA9557 FINAL: CFG=0x%02X ENTRADAS=0x%02X SALIDAS=0x%02X\n",
       config,inputs,outputs);
   if(config!=0x02||outputs!=0x01){
    Serial.println("[TOUCH] ALERTA: registros PCA no coinciden con CFG=02 OUTPUT=01");
   }
  }else{
   Serial.println("[TOUCH] ERROR: no se pudo leer el PCA9557 tras secuencia");
  }
 }else{
  Serial.println("[TOUCH] PCA9557 ausente, no se cambia ninguna salida");
  delay(150);
 }

 const uint8_t candidates[2]={GT911_ADDRESS_A,GT911_ADDRESS_B};
 for(const uint8_t address:candidates){
  if(!touch_i2c_ack(address)){
   Serial.printf("[I2C] GT911 0x%02X: SIN RESPUESTA\n",address);
   continue;
  }
  Serial.printf("[I2C] GT911 0x%02X: ACK\n",address);
  uint8_t id[4]={0};
  if(touch_gt911_read(address,GT911_ID_REG,id,sizeof(id))){
   touch_address=address;touch_available=true;
   Serial.printf("[TOUCH] GT911 LISTO: 0x%02X ID=%c%c%c%c\n",
     address,id[0],id[1],id[2],id[3]);
   return true;
  }
  Serial.printf("[TOUCH] 0x%02X responde pero no permite leer ID\n",address);
 }
 Serial.println("[TOUCH] SIN RESPUESTA. Verificar PCA9557 y revision fisica de CrowPanel.");
 return false;
}

bool touch_touched(){
 static uint32_t last_poll=0,last_log=0;
 const uint32_t now=millis();
 if(!touch_available)return false;
 if(now-last_poll<25)return touch_is_pressed;
 last_poll=now;
 uint8_t status=0;
 if(!touch_gt911_read(touch_address,GT911_POINT_INFO,&status,1)){
  touch_failures++;touch_is_pressed=false;
  if(now-last_log>=3000){
   last_log=now;
   Serial.printf("[TOUCH] Fallo de lectura GT911: %lu\n",
      (unsigned long)touch_failures);
  }
  return false;
 }
 touch_reads++;
 if(!(status&0x80)){
  if(now-touch_last_event_ms>100)touch_is_pressed=false;
  return touch_is_pressed;
 }
 const uint8_t contacts=(uint8_t)(status&0x0F);
 bool pressed=contacts>0&&contacts<=5;
 if(pressed){
  uint8_t point[7]={0};
  if(touch_gt911_read(touch_address,GT911_FIRST_POINT,point,sizeof(point))){
   const uint16_t x=(uint16_t)point[1]|((uint16_t)point[2]<<8);
   const uint16_t y=(uint16_t)point[3]|((uint16_t)point[4]<<8);
   if(x<800&&y<480){
    touch_last_x=x;touch_last_y=y;touch_last_event_ms=now;
   }else pressed=false;
  }else pressed=false;
 }
 touch_is_pressed=pressed;
 if(!touch_gt911_clear())touch_failures++;
 return touch_is_pressed;
}
#endif
