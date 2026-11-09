#include <Arduino.h>
#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>
#include <Wire.h>
#include "valver_ui.h"

// Real wired interface. Motor commands and safety checks reside in controller.
namespace {
constexpr uint32_t BG=0x091522, BASE=0x112033, PANEL=0x19354B, CELL=0x10283C;
constexpr uint32_t BLUE=0x285A7A, GREEN=0x177B67, RED=0x99374D, BORDER=0x37627B;
constexpr uint32_t TEXT=0xEDF7FF, SUB=0xA8C5D6, CYAN=0x85D9F6;

lv_obj_t *pages[4]={nullptr,nullptr,nullptr,nullptr};
lv_obj_t *navBtn[4]={nullptr,nullptr,nullptr,nullptr};
lv_obj_t *notice=nullptr,*counterLabel=nullptr,*speedLabel=nullptr;
lv_obj_t *touchTop=nullptr,*touchDiag=nullptr,*touchCoords=nullptr;
lv_obj_t *wizardPick=nullptr,*wizardWork=nullptr,*wizardDone=nullptr;
lv_obj_t *wizardChoice[3]={nullptr,nullptr,nullptr};
lv_obj_t *wizardStepLabel=nullptr,*wizardHelp=nullptr,*wizardProgress=nullptr;
uint8_t currentPage=0, wizardMode=0, wizardStep=0;
uint32_t taps=0, lastTick=0;
bool wizardRunning=false;

enum Action : intptr_t {
 NAV0=1,NAV1=2,NAV2=3,NAV3=4,
 CONNECT=10,STOP=11,
 Z1_UP=20,Z1_DOWN=21,Z1_REST=22,Z1_DISP=23,
 Z2_UP=24,Z2_DOWN=25,Z2_REST=26,Z2_DISP=27,
 PICK_Z1=40,PICK_Z2=41,PICK_BOTH=42,BEGIN=43,
 WIZ_UP=44,WIZ_DOWN=45,WIZ_OK=46,WIZ_CANCEL=47,WIZ_RESTART=48,WIZ_HOME=49,
 ORIGIN_TEST=60
};

void bg_style(lv_obj_t* o, uint32_t color, int radius=0) {
 lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
 lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
 lv_obj_set_style_radius(o, radius, 0);
 lv_obj_set_style_border_width(o, 0, 0);
 lv_obj_set_style_shadow_width(o, 0, 0);
 lv_obj_set_style_pad_all(o, 0, 0);
 lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}
lv_obj_t* panel(lv_obj_t* parent,int x,int y,int w,int h,uint32_t c=CELL) {
 lv_obj_t* o=lv_obj_create(parent);
 lv_obj_set_pos(o,x,y);
 lv_obj_set_size(o,w,h);
 bg_style(o,c,8);
 lv_obj_set_style_border_width(o,1,0);
 lv_obj_set_style_border_color(o,lv_color_hex(BORDER),0);
 return o;
}
lv_obj_t* label(lv_obj_t* parent,int x,int y,int w,const char* str,
                uint32_t color=TEXT,bool large=false){
 lv_obj_t* l=lv_label_create(parent);
 lv_label_set_text(l,str);
 lv_obj_set_pos(l,x,y);
 lv_obj_set_width(l,w);
 lv_obj_set_style_text_color(l,lv_color_hex(color),0);
 lv_obj_set_style_text_font(l,large?&lv_font_montserrat_20:&lv_font_montserrat_14,0);
 lv_obj_remove_flag(l,LV_OBJ_FLAG_SCROLLABLE);
 return l;
}
void on_action(lv_event_t* e);
void motorPress(lv_event_t* e);
lv_obj_t* btn(lv_obj_t* parent,int x,int y,int w,int h,const char* str,
              uint32_t color,intptr_t action){
 lv_obj_t* b=lv_button_create(parent);
 lv_obj_set_pos(b,x,y);
 lv_obj_set_size(b,w,h);
 bg_style(b,color,7);
 lv_obj_set_style_border_width(b,1,0);
 lv_obj_set_style_border_color(b,lv_color_hex(BORDER),0);
 lv_obj_set_style_pad_all(b,3,0);
 lv_obj_t* l=lv_label_create(b);
 lv_label_set_text(l,str);
 lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0);
 lv_obj_set_style_text_color(l,lv_color_hex(TEXT),0);
 lv_obj_center(l);
 lv_obj_add_event_cb(b,on_action,LV_EVENT_CLICKED,reinterpret_cast<void*>(action));
 if(action==Z1_UP||action==Z1_DOWN||action==Z2_UP||action==Z2_DOWN||
    action==WIZ_UP||action==WIZ_DOWN){
  lv_obj_add_event_cb(b,motorPress,LV_EVENT_PRESSED,reinterpret_cast<void*>(action));
  lv_obj_add_event_cb(b,motorPress,LV_EVENT_RELEASED,reinterpret_cast<void*>(action));
  lv_obj_add_event_cb(b,motorPress,LV_EVENT_PRESS_LOST,reinterpret_cast<void*>(action));
 }
 return b;
}
void show_notice(const char* msg){
 if(notice)lv_label_set_text(notice,msg);
 Serial.printf("[VALVER HMI] %s\n",msg);
}
void show_page(uint8_t index){
 if(index>3)return;
 currentPage=index;
 for(uint8_t i=0;i<4;i++){
  if(i==index)lv_obj_remove_flag(pages[i],LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(pages[i],LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_color(navBtn[i],lv_color_hex(i==index?BLUE:PANEL),0);
 }
}
void wizard_render(){
 lv_obj_add_flag(wizardPick,LV_OBJ_FLAG_HIDDEN);
 lv_obj_add_flag(wizardWork,LV_OBJ_FLAG_HIDDEN);
 lv_obj_add_flag(wizardDone,LV_OBJ_FLAG_HIDDEN);
 if(!wizardRunning){
  lv_obj_remove_flag(wizardPick,LV_OBJ_FLAG_HIDDEN);
  for(uint8_t i=0;i<3;i++)
    lv_obj_set_style_bg_color(wizardChoice[i],lv_color_hex(wizardMode==i?GREEN:BLUE),0);
  return;
 }
 if(wizardStep >= (wizardMode==2?4:2)){
  lv_obj_remove_flag(wizardDone,LV_OBJ_FLAG_HIDDEN);
  return;
 }
 lv_obj_remove_flag(wizardWork,LV_OBJ_FLAG_HIDDEN);
 const bool z2=(wizardMode==1)||(wizardMode==2&&wizardStep>=2);
 const char* axis=z2?"Z2":"Z1";
 const char* place=(wizardStep%2==0)?"DISPENSACION":"REPOSO";
 char title[65],prog[40],help[160];
 snprintf(title,sizeof(title),"%s - %s",axis,place);
 snprintf(prog,sizeof(prog),"PASO %u DE %u",wizardStep+1,(wizardMode==2?4:2));
 snprintf(help,sizeof(help),
          "Situar %s en %s con SUBIR / BAJAR.\nSoltar y pulsar OK para guardar.",axis,place);
 lv_label_set_text(wizardStepLabel,title);
 lv_label_set_text(wizardProgress,prog);
 lv_label_set_text(wizardHelp,help);
}
#include "hmi_i2c_ui.inc"
void make_header(lv_obj_t* root){
 lv_obj_t* top=panel(root,0,0,800,53,0x071827);
 lv_obj_set_style_radius(top,0,0);
 label(top,14,12,230,"VALVER",TEXT,true);
 label(top,137,17,330,"SMART DISPENSER  |  PANTALLA 7 PULGADAS",SUB);
 touchTop=label(top,591,18,205,"TACTIL: comprobando",CYAN);
 lv_obj_t* side=panel(root,0,55,130,425,PANEL);
 lv_obj_set_style_radius(side,0,0);
 navBtn[0]=btn(side,6,9,118,51,"CONTROL Z",BLUE,NAV0);
 navBtn[1]=btn(side,6,69,118,51,"CALIBRAR",BLUE,NAV1);
 navBtn[2]=btn(side,6,129,118,51,"BUSCAR ORIGEN",BLUE,NAV2);
 navBtn[3]=btn(side,6,189,118,51,"DIAGNOSTICO",BLUE,NAV3);
 label(side,9,300,113,"ESP32 PANTALLA",CYAN);
 label(side,9,328,113,"MOTORES:",SUB);
 driverText=label(side,9,350,113,"SIN ENLACE",SUB);
 lv_obj_t* toolbar=panel(root,134,55,666,48,BASE);
 lv_obj_set_style_radius(toolbar,0,0);
 btn(toolbar,8,4,133,38,"VERIFICAR ENLACE",BLUE,CONNECT);
 connectionText=label(toolbar,150,14,291,"I2C: SIN ENLACE",SUB);
 btn(toolbar,520,4,137,38,"PARADA",RED,STOP);
 lv_obj_t* foot=panel(root,134,442,666,38,BASE);
 lv_obj_set_style_radius(foot,0,0);
 notice=label(foot,10,10,642,"Conectando controlador de motores...",CYAN);
}
void make_home(){
 lv_obj_t* p=pages[0];
 for(uint8_t i=0;i<2;i++){
  const int x=(i==0)?5:336;
  lv_obj_t* card=panel(p,x,4,325,229,CELL);
  label(card,12,11,288,i==0?"Z1  |  CONDUCTIVO":"Z2  |  ESTRUCTURAL",TEXT,true);
  axisText[i]=label(card,12,41,287,"TMC2209: verificando",SUB);
  btn(card,12,72,146,48,"SUBIR",BLUE,i==0?Z1_UP:Z2_UP);
  btn(card,168,72,145,48,"BAJAR",BLUE,i==0?Z1_DOWN:Z2_DOWN);
  btn(card,12,132,146,48,"IR A REPOSO",GREEN,i==0?Z1_REST:Z2_REST);
  btn(card,168,132,145,48,"DISPENSACION",GREEN,i==0?Z1_DISP:Z2_DISP);
  positionText[i]=label(card,12,197,300,"Posicion: sin referencia",SUB);
 }
 lv_obj_t* speed=panel(p,5,244,656,84,PANEL);
 speedLabel=label(speed,14,6,600,"Velocidad: 1600 micropasos/s",TEXT);
 lv_obj_t* sl=lv_slider_create(speed);
 lv_obj_set_pos(sl,19,43);
 lv_obj_set_size(sl,612,12);
 lv_slider_set_range(sl,80,8000);
 lv_slider_set_value(sl,1600,LV_ANIM_OFF);
 lv_obj_add_event_cb(sl,on_speed,LV_EVENT_VALUE_CHANGED,nullptr);
 lv_obj_add_event_cb(sl,on_speed,LV_EVENT_RELEASED,nullptr);
}
void make_calibration(){
 lv_obj_t* p=pages[1];
 wizardPick=panel(p,5,4,655,330,PANEL);
 label(wizardPick,16,12,613,"INICIO DEL SISTEMA",CYAN,true);
 label(wizardPick,16,56,613,"Selecciona los motores para configurar las alturas:",SUB);
 wizardChoice[0]=btn(wizardPick,16,94,197,60,"Z1",GREEN,PICK_Z1);
 wizardChoice[1]=btn(wizardPick,229,94,197,60,"Z2",BLUE,PICK_Z2);
 wizardChoice[2]=btn(wizardPick,442,94,197,60,"AMBOS",BLUE,PICK_BOTH);
 btn(wizardPick,16,176,623,59,"INICIAR SISTEMA",GREEN,BEGIN);
 label(wizardPick,16,254,621,"AMBOS: 4 posiciones independientes (Z1 y Z2).",SUB);
 label(wizardPick,16,276,621,"El controlador confirmara cada altura guardada.",SUB);
 wizardWork=panel(p,5,4,655,330,PANEL);
 wizardStepLabel=label(wizardWork,16,13,450,"Z1 - DISPENSACION",CYAN,true);
 wizardProgress=label(wizardWork,481,17,160,"PASO 1 DE 2",SUB);
 wizardHelp=label(wizardWork,16,64,625,"Situa la aguja y pulsa OK.",TEXT);
 btn(wizardWork,16,140,304,59,"SUBIR",BLUE,WIZ_UP);
 btn(wizardWork,335,140,304,59,"BAJAR",BLUE,WIZ_DOWN);
 btn(wizardWork,16,223,190,57,"CANCELAR",RED,WIZ_CANCEL);
 btn(wizardWork,221,223,418,57,"OK  -  GUARDAR POSICION",GREEN,WIZ_OK);
 wizardDone=panel(p,5,4,655,330,PANEL);
 label(wizardDone,20,30,618,"CONFIGURACION COMPLETADA",CYAN,true);
 label(wizardDone,20,105,614,"Calibracion registrada en el controlador.",TEXT);
 label(wizardDone,20,139,614,"Control cruzado: primero retirar, luego dispensar.",SUB);
 btn(wizardDone,20,217,294,61,"REPETIR",BLUE,WIZ_RESTART);
 btn(wizardDone,331,217,304,61,"CONTROL Z",GREEN,WIZ_HOME);
 wizard_render();
}
void make_origin(){
 lv_obj_t* p=pages[2];
 lv_obj_t* a=panel(p,5,4,655,330,PANEL);
 label(a,16,18,619,"BUSQUEDA DE ORIGEN  |  StallGuard4",CYAN,true);
 label(a,16,66,625,"La deteccion de origen requiere validacion mecanica.",TEXT);
 label(a,16,101,625,"StallGuard4 no sustituye los limites de seguridad.",SUB);
 btn(a,16,157,623,60,"VER DISPONIBILIDAD",BLUE,ORIGIN_TEST);
 label(a,16,248,623,"Busqueda automatica bloqueada hasta validar StallGuard4.",SUB);
}
void make_diag(){
 lv_obj_t* p=pages[3];
 lv_obj_t* a=panel(p,5,4,655,330,PANEL);
 label(a,16,14,615,"DIAGNOSTICO DE PANTALLA",CYAN,true);
 label(a,16,59,618,"PANTALLA RGB: 800 x 480  |  LVGL 9.1",TEXT);
 label(a,16,93,618,"TACTIL GT911: prueba tocando los controles.",TEXT);
 label(a,16,127,618,"COMUNICACION: I2C cable 0x32",SUB);
 label(a,16,161,618,"PROTECCIONES: gestionadas por ESP32 Z",SUB);
 touchDiag=label(a,16,196,620,"GT911: comprobando direccion I2C",CYAN);
 touchCoords=label(a,16,258,620,"ULTIMO TOQUE: ninguno",SUB);
 counterLabel=label(a,16,228,617,"TOQUES DETECTADOS: 0",CYAN,true);
}
} // namespace

void valver_ui_init(){
 lv_obj_t* root=lv_screen_active();
 bg_style(root,BG,0);
 make_header(root);
 for(uint8_t i=0;i<4;i++){
  pages[i]=panel(root,134,104,666,338,BASE);
  lv_obj_set_style_border_width(pages[i],0,0);
  lv_obj_set_style_radius(pages[i],0,0);
 }
 make_home();
 make_calibration();
 make_origin();
 make_diag();
 show_page(0);
 show_notice("VALVER SMART DISPENSER: comprobando controlador...");
}
void valver_ui_set_touch_status(uint8_t i2c_address){
 char str[100];
 if(i2c_address){
  snprintf(str,sizeof(str),"TACTIL: OK 0x%02X",i2c_address);
  if(touchTop)lv_label_set_text(touchTop,str);
  snprintf(str,sizeof(str),"GT911: detectado en I2C 0x%02X - listo",i2c_address);
  if(touchDiag)lv_label_set_text(touchDiag,str);
 }else{
  if(touchTop)lv_label_set_text(touchTop,"TACTIL: SIN RESPUESTA");
  if(touchDiag)lv_label_set_text(touchDiag,"GT911 NO DETECTADO en 0x5D / 0x14");
 }
}
void valver_ui_note_touch(int x,int y){
 if(touchCoords){
  char msg[70];
  snprintf(msg,sizeof(msg),"ULTIMO TOQUE: X=%d  Y=%d",x,y);
  lv_label_set_text(touchCoords,msg);
 }
}
void valver_ui_tick(){
 pollMotors();
 lastTick=millis();
}
