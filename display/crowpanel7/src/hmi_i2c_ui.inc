// VALVER HMI wired I2C. All motor operations are validated by controller dispatch().
static constexpr uint8_t MOTOR_I2C=0x32;
static bool motorOnline=false,held=false,moving=false,holdOwned=false;
static uint8_t txSeq=0,waitingSeq=0,waitingOp=0,heldAxis=0,heldUp=0;
static uint8_t motorFlags=0;
static uint32_t lastLink=0,lastPoll=0,lastBeat=0;
static lv_obj_t *connectionText=nullptr,*driverText=nullptr;
static lv_obj_t *axisText[2]={nullptr,nullptr},*positionText[2]={nullptr,nullptr};
static int32_t readInt32(const uint8_t* p){
 uint32_t v=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
 return (int32_t)v;
}
static bool sendMotor(uint8_t op,uint8_t axis=0,uint8_t arg=0,uint16_t value=0,bool ack=false){
 if(!motorOnline && op!=1 && op!=2)return false;
 uint8_t p[9]={0x56,1,++txSeq,op,axis,arg,(uint8_t)value,(uint8_t)(value>>8),0};
 for(int i=0;i<8;i++)p[8]^=p[i];
 Wire1.beginTransmission(MOTOR_I2C);
 Wire1.write(p,9);
 if(Wire1.endTransmission()!=0){motorOnline=false;return false;}
 if(ack){waitingSeq=p[2];waitingOp=op;}
 return true;
}
static void motorStop(){
 held=false;moving=false;holdOwned=false;waitingOp=0;
 show_notice(sendMotor(2)?"PARADA enviada":"SIN ENLACE: comprobar parada fisica");
}
static void motorRelease(){
 held=false;
 if(moving||(motorFlags&1)){sendMotor(5);moving=false;}
}
void motorPress(lv_event_t *e){
 const auto ev=lv_event_get_code(e);
 if(ev==LV_EVENT_RELEASED||ev==LV_EVENT_PRESS_LOST){motorRelease();return;}
 if(ev!=LV_EVENT_PRESSED)return;
 const intptr_t a=reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
 uint8_t ax=0,up=0;
 if(a==Z1_UP||a==Z1_DOWN){ax=1;up=a==Z1_UP;}
 if(a==Z2_UP||a==Z2_DOWN){ax=2;up=a==Z2_UP;}
 if(a==WIZ_UP||a==WIZ_DOWN){
  if(!wizardRunning)return;
  ax=(wizardMode==1||(wizardMode==2&&wizardStep==2))?2:1;up=a==WIZ_UP;
 }
 if(!ax)return;
 if(!motorOnline||waitingOp||(motorFlags&1)){show_notice("Movimiento bloqueado: controlador ocupado/sin enlace");return;}
 held=true;heldAxis=ax;heldUp=up;
 if(!sendMotor(3,ax,0,0,true)){held=false;show_notice("Fallo al preparar eje");}
}
void on_speed(lv_event_t* e){
 const int n=lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
 lv_label_set_text_fmt(speedLabel,"Velocidad: %d micropasos/s",n);
 if(lv_event_get_code(e)==LV_EVENT_RELEASED){
  if(!sendMotor(8,0,0,(uint16_t)n,true))show_notice("Velocidad no aplicada");
 }
}
void on_action(lv_event_t* e){
 const intptr_t a=reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
 taps++;if(counterLabel)lv_label_set_text_fmt(counterLabel,"TOQUES DETECTADOS: %lu",(unsigned long)taps);
 if(a>=NAV0&&a<=NAV3){show_page((uint8_t)(a-NAV0));return;}
 if(a==STOP){motorStop();return;}
 if(a==CONNECT){sendMotor(1);return;}
 if(a==Z1_UP||a==Z1_DOWN||a==Z2_UP||a==Z2_DOWN||a==WIZ_UP||a==WIZ_DOWN)return;
 if(a==Z1_REST||a==Z1_DISP||a==Z2_REST||a==Z2_DISP){
  uint8_t ax=(a==Z1_REST||a==Z1_DISP)?1:2;
  uint8_t kind=(a==Z1_REST||a==Z2_REST)?1:2;
  if(!motorOnline||!(motorFlags&(ax==1?16:32))||waitingOp){show_notice("Origen sin validar o controlador ocupado");return;}
  if(!sendMotor(13,ax,kind,0,true))show_notice("Error al enviar desplazamiento");
  else show_notice("Desplazamiento solicitado");return;
 }
 if(a>=PICK_Z1&&a<=PICK_BOTH){wizardMode=(uint8_t)(a-PICK_Z1);wizard_render();return;}
 if(a==BEGIN||a==WIZ_RESTART){
  if(!motorOnline||waitingOp){show_notice("Sin controlador disponible");return;}
  wizardRunning=true;wizardStep=0;
  if(!sendMotor(9,wizardMode==1?2:1,0,0,true))wizardRunning=false;
  wizard_render();return;
 }
 if(a==WIZ_CANCEL){motorRelease();wizardRunning=false;wizardStep=0;wizard_render();return;}
 if(a==WIZ_OK){
  if(!wizardRunning||waitingOp||held||(motorFlags&1)){show_notice("Suelta el motor antes de guardar");return;}
  const uint8_t ax=(wizardMode==1||(wizardMode==2&&wizardStep==2))?2:1;
  const bool ok=wizardMode==2&&wizardStep==2?
    sendMotor(12,2,0,0,true):sendMotor(10,ax,wizardStep==0?2:1,0,true);
  if(!ok)show_notice("No se pudo guardar la posicion");return;
 }
 if(a==WIZ_HOME){show_page(0);return;}
 if(a==ORIGIN_TEST)show_notice("Busqueda automatica pendiente de validacion StallGuard4");
}
static void pollMotors(){
 const uint32_t now=millis();if(now-lastPoll<70)return;lastPoll=now;
 uint8_t rx[32]={0};const uint8_t count=Wire1.requestFrom((uint8_t)MOTOR_I2C,(uint8_t)32);
 if(count==32){for(int i=0;i<32;i++)rx[i]=Wire1.read();}
 else while(Wire1.available())Wire1.read();
 if(count!=32||rx[0]!=0x56||rx[1]!=1){
  if(now-lastLink>500&&motorOnline){
   motorOnline=false;held=false;moving=false;holdOwned=false;waitingOp=0;
   if(connectionText)lv_label_set_text(connectionText,"I2C: SIN ENLACE");
   if(driverText)lv_label_set_text(driverText,"DESCONECTADO");
   show_notice("Enlace perdido: movimiento bloqueado");
  }
  return;
 }
 const bool wasOnline=motorOnline;
 motorOnline=true;lastLink=now;motorFlags=rx[5];
 if(waitingOp&&rx[2]==waitingSeq){
  const uint8_t op=waitingOp;waitingOp=0;
  if(rx[3]==2){
   char msg[70];snprintf(msg,sizeof(msg),"ORDEN RECHAZADA: %.15s",(char*)(rx+16));
   held=false;moving=false;show_notice(msg);
  }else if(rx[3]==1){
   if(op==3&&held){if(!sendMotor(4,heldAxis,heldUp,0,true))held=false;}
   else if(op==4){if(held){moving=true;holdOwned=true;}else sendMotor(5);}
   else if(op==10){
    if(wizardMode==2&&wizardStep==1)sendMotor(11,0,0,0,true);
    else{wizardStep++;wizard_render();show_notice("Posicion guardada");}
   }else if(op==11||op==12){wizardStep++;wizard_render();show_notice("Referencia registrada");}
   else if(op==13){holdOwned=true;show_notice("Movimiento aceptado");}
  }
 }
 if(now%400<70){
  if(connectionText)lv_label_set_text(connectionText,"I2C: CONECTADO 0x32");
  if(driverText)lv_label_set_text(driverText,"CONECTADO");
  for(int i=0;i<2;i++){
   if(axisText[i])lv_label_set_text(axisText[i],(motorFlags&(i?128:64))?"TMC2209: UART OK":"TMC2209: SIN RESPUESTA");
   if(positionText[i])lv_label_set_text_fmt(positionText[i],"Z%d: %ld | %s",i+1,
      (long)readInt32(rx+8+4*i),(motorFlags&(i?32:16))?"REFERENCIADO":"SIN ORIGEN");
  }
 }
 if(!wasOnline)show_notice("Controlador conectado por cable I2C");
 if((motorFlags&1)&&held&&moving&&now-lastBeat>=90){sendMotor(6);lastBeat=now;}
 else if(holdOwned&&(motorFlags&12)&&now-lastBeat>=150){sendMotor(7);lastBeat=now;}
 if(!(motorFlags&1))moving=false;
}
