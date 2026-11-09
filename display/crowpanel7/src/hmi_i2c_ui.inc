// VALVER HMI wired I2C. All motor operations are validated by controller dispatch().
static constexpr uint8_t MOTOR_I2C=0x32;
static bool motorOnline=false,held=false,moving=false,autoRunning=false,holdOwned=false;
static uint8_t txSeq=0,waitingSeq=0,waitingOp=0,heldAxis=0,heldUp=0;
static uint8_t motorFlags=0,motorMarks=0,motorFresh=0;
static int32_t zPos[2]={0,0},zRest[2]={0,0},zDisp[2]={0,0};
static uint8_t previousTmcReady=0;
static uint32_t lastLink=0,lastPoll=0,lastBeat=0,waitingSince=0,lastStatusDraw=0;
static uint8_t lastRemoteAck=0,lastRemoteStatus=0;
static uint32_t badReadCount=0;
static uint8_t lastCriticalPacket[9]={0};
static uint8_t startRetryCount=0;
static uint32_t startRetryAt=0;
static int32_t startPosition=0;
static bool startPending=false;
static uint32_t startSentAt=0;
static bool startUnconfirmedNotice=false;
// Paired dispense control is a single operator action with two interlocked
// sequential strokes: retract the OTHER needle, then dispense the SELECTED.
// Never command both needles into the work zone at the same time.
static uint8_t pairPhase=0,pairChosen=0,pairOther=0;
static uint32_t pairPhaseStarted=0;
static bool wizardSwitchingAxis=false;
static uint8_t pendingSaveAxis=0,pendingSaveKind=0;
static int32_t pendingSavePosition=0;
static lv_obj_t *connectionText=nullptr,*driverText=nullptr;
static lv_obj_t *axisText[2]={nullptr,nullptr},*positionText[2]={nullptr,nullptr};
static int32_t readInt32(const uint8_t* p){
 uint32_t v=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
 return (int32_t)v;
}
static bool sendMotor(uint8_t op,uint8_t axis=0,uint8_t arg=0,uint16_t value=0,bool ack=false){
 if(!motorOnline && op!=1 && op!=2)return false;
 // The high bit requests an ACK; periodic heartbeats never invalidate it.
 uint8_t p[9]={0x56,1,++txSeq,(uint8_t)(op|(ack?0x80:0)),axis,arg,
               (uint8_t)value,(uint8_t)(value>>8),0};
 for(int i=0;i<8;i++)p[8]^=p[i];
 Wire1.beginTransmission(MOTOR_I2C);
 Wire1.write(p,9);
 if(Wire1.endTransmission()!=0){motorOnline=false;return false;}
 if(ack){
   waitingSeq=p[2];waitingOp=op;waitingSince=millis();
   if(op==4||op==10){
     memcpy(lastCriticalPacket,p,sizeof(lastCriticalPacket));
     startRetryCount=0;startRetryAt=millis();
   }
 }
 return true;
}
static void motorStop(){
 held=false;moving=false;autoRunning=false;holdOwned=false;startPending=false;
 startUnconfirmedNotice=false;waitingOp=0;pairPhase=0;wizardSwitchingAxis=false;
 show_notice(sendMotor(2)?"PARADA enviada":"SIN ENLACE: comprobar parada fisica");
}
static void motorRelease(){
 const bool needStop=held||startPending||moving||(motorFlags&1)||waitingOp==3||waitingOp==4;
 held=false;startPending=false;startUnconfirmedNotice=false;
 if(needStop){
  // Acknowledge HOLD_STOP before accepting another press. This also works
  // when the finger is released while PREPARE/HOLD_START is still pending.
  if(!sendMotor(5,0,0,0,true)){
   waitingOp=0;
   sendMotor(2); // Safe fallback: disable if STOP could not be confirmed.
   show_notice("Error de parada: controlador deshabilitado");
  }
  moving=false;
 }
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
  ax=(uint8_t)(wizardMode+1);up=a==WIZ_UP;
 }
 if(!ax)return;
 if(!motorOnline){show_notice("I2C SIN ENLACE: movimiento bloqueado");return;}
 if(pairPhase){show_notice("Cambio de agujas en curso");return;}
 if(waitingOp){show_notice("Esperando confirmacion del controlador");return;}
 if((motorFlags&1)||autoRunning){show_notice("Movimiento anterior aun activo");return;}
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
// Each needle has exactly two calibration steps. The controller persists
// each height to NVS BEFORE acknowledging SAVE_Z_HEIGHT.
static void startCalibration(uint8_t mode){
 if(!motorOnline || waitingOp || held || pairPhase || (motorFlags&1)){
   show_notice("Termina el movimiento antes de calibrar");
   return;
 }
 wizardMode=mode;wizardStep=0;wizardRunning=true;
 wizardSwitchingAxis=false;pendingSaveAxis=0;pairPhase=0;
 wizard_render();
 if(!sendMotor(9,(uint8_t)(mode+1),0,0,true)){
   wizardRunning=false;
   wizard_render();
   show_notice("Controlador no disponible");
 }
}
static void heightSaved(){
 if(pendingSaveAxis==0)return;
 const uint8_t axis=pendingSaveAxis,kind=pendingSaveKind;
 pendingSaveAxis=0;pendingSaveKind=0;
 if(wizardRunning && axis==wizardMode+1 &&
    kind==(wizardStep==0?2:1) && wizardStep<2){
   wizardStep++;
   if(wizardStep>=2){
      // Save completed on COM5: return immediately to the Z1/Z2 menu.
      // Never force the user into another calibration or CONTROL Z.
      wizardRunning=false;
      wizardStep=0;
      wizard_render();
      show_notice(axis==1?"Z1: alturas guardadas.":"Z2: alturas guardadas.");
   }else{
      wizard_render();
      show_notice(axis==1?"Z1: dispensacion guardada.":"Z2: dispensacion guardada.");
   }
 }
}
void on_action(lv_event_t* e){
 const intptr_t a=reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
 taps++;if(counterLabel)lv_label_set_text_fmt(counterLabel,"TOQUES DETECTADOS: %lu",(unsigned long)taps);
 if(a>=NAV0&&a<=NAV3){show_page((uint8_t)(a-NAV0));return;}
 if(a==STOP){motorStop();return;}
 if(a==CONNECT){
  if(waitingOp){show_notice("Prueba I2C pendiente: no enviar otra orden");return;}
  if(sendMotor(1,0,0,0,true))
    show_notice("Verificando confirmacion PING/PONG por cable...");
  else show_notice("PING: escritura I2C fallida");
  return;
 }
 if(a==Z1_UP||a==Z1_DOWN||a==Z2_UP||a==Z2_DOWN||a==WIZ_UP||a==WIZ_DOWN)return;
 if(a==Z1_REST||a==Z1_DISP||a==Z2_REST||a==Z2_DISP){
  const uint8_t ax=(a==Z1_REST||a==Z1_DISP)?1:2;
  const uint8_t kind=(a==Z1_REST||a==Z2_REST)?1:2;
  if(!motorOnline || waitingOp || pairPhase || held || (motorFlags&1)){
    show_notice("Controlador ocupado o sin enlace");return;
  }
  if(kind==2){
    // Both independent calibrations, known origins and holding torque are
    // mandatory before any coordinated dispense request.
    if((motorFlags&0x3C)!=0x3C || (motorMarks&0x0F)!=0x0F){
      if((motorMarks&0x0F)==0x0F)
        show_notice("Alturas guardadas; posicion sin referencia fiable. Recalibrar.");
      else
        show_notice("Falta guardar dispensacion/reposo en Z1 o Z2");
      return;
    }
    pairChosen=ax;pairOther=ax==1?2:1;pairPhase=1;
    pairPhaseStarted=millis();
    if(!sendMotor(13,pairOther,1,0,true)){
      pairPhase=0;show_notice("No se pudo solicitar reposo opuesto");
    }else show_notice("Control cruzado: retirando aguja contraria");
    return;
  }
  if(!(motorFlags&(ax==1?16:32)) || !(motorFlags&(ax==1?4:8))){
    if((motorMarks&(ax==1?3:12))==(ax==1?3:12))
      show_notice("Alturas guardadas, pero referencia perdida: recalibrar eje");
    else
      show_notice("Guardar DISPENSACION y REPOSO de este eje");
    return;
  }
  if(!sendMotor(13,ax,kind,0,true))show_notice("No se pudo solicitar reposo");
  else show_notice("Desplazamiento a reposo solicitado");
  return;
 }
 if(a==PICK_Z1||a==PICK_Z2||a==WIZ_NEXT||a==WIZ_RESTART||a==BEGIN){
    const uint8_t mode=(a==PICK_Z2||a==WIZ_NEXT)?1:
          (a==WIZ_RESTART?wizardMode:0);
    startCalibration(mode);
    return;
 }
 if(a==WIZ_CANCEL){
    motorRelease();wizardRunning=false;wizardStep=0;
    pendingSaveAxis=0;wizard_render();return;
 }
 if(a==WIZ_OK){
  if(!wizardRunning||waitingOp||held||(motorFlags&1)){show_notice("Suelta el motor antes de guardar");return;}
  if(wizardStep>=2)return;
  const uint8_t ax=(uint8_t)(wizardMode+1);
  const uint8_t kind=(wizardStep==0)?2:1;
  if(!(motorFlags&(ax==1?4:8))){
    show_notice("Eje sin retencion: mover, soltar y guardar");return;
  }
  pendingSaveAxis=ax;pendingSaveKind=kind;
  pendingSavePosition=zPos[ax-1];
  if(!sendMotor(10,ax,kind,0,true))
    show_notice("No se pudo enviar guardado");
  else
    show_notice("Guardando altura en controlador...");
  return;
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
  badReadCount++;
  // Flash/NVS can suspend the I2C reply briefly during height saves.
  // Grace applies only to stationary configuration, never to motion.
  const bool savingConfig=(waitingOp==8||waitingOp==10||waitingOp==11);
  const uint32_t linkGraceMs=savingConfig?3000UL:500UL;
  if((uint32_t)(now-lastLink)>linkGraceMs&&motorOnline){
   motorOnline=false;held=false;moving=false;autoRunning=false;holdOwned=false;startPending=false;pairPhase=0;
   startUnconfirmedNotice=false;waitingOp=0;
   if(connectionText)lv_label_set_text(connectionText,"I2C: SIN ENLACE");
   if(driverText)lv_label_set_text(driverText,"DESCONECTADO");
   show_notice("Enlace perdido: movimiento bloqueado");
  }
  return;
 }
 const bool wasOnline=motorOnline;
 motorOnline=true;lastLink=now;motorFlags=rx[5];motorMarks=rx[6];motorFresh=rx[4];
 zPos[0]=readInt32(rx+8);zPos[1]=readInt32(rx+12);
 zRest[0]=readInt32(rx+16);zRest[1]=readInt32(rx+20);
 zDisp[0]=readInt32(rx+24);zDisp[1]=readInt32(rx+28);
 const uint8_t currentTmcReady=(uint8_t)(motorFlags&0xC0);
 if(currentTmcReady!=previousTmcReady){
   previousTmcReady=currentTmcReady;
   if(currentTmcReady==0xC0)
     show_notice("TMC2209 Z1/Z2: UART enlazada, listo para verificar");
   else
     show_notice("Esperando alimentacion y UART de los TMC2209");
 }
 lastRemoteAck=rx[2];lastRemoteStatus=rx[3];
 // Confirm the axis is actually running via the motor-controller state,
 // never by accepting an unverified write as proof of movement.
 if(startPending && held && (motorFlags&1) && !(motorFlags&2) &&
    rx[7]==heldAxis){
   startPending=false;startUnconfirmedNotice=false;moving=true;holdOwned=true;
   Serial.printf("[I2C] MANUAL active axis=%u position=%ld\n",
     (unsigned)heldAxis,(long)readInt32(rx+8+4*(heldAxis-1)));
   show_notice("Eje activo; soltar para detener");
 }
 if(waitingOp&&rx[2]==waitingSeq){
  const uint8_t op=waitingOp;waitingOp=0;
  if(op==1){
     if(rx[3]==1)show_notice("PING/PONG I2C: confirmacion recibida");
     else show_notice("PING I2C: respuesta rechazada");
   }
   if(rx[3]==2){
   // Reject and keep the current wizard step; no unverified save proceeds.
   if(pairPhase)pairPhase=0;
   if(op==10)pendingSaveAxis=0;
   if((op==10||op==11||op==12) &&
      strncmp((const char*)(rx+16),"HOLD_MOTOR_WHIL",15)==0)
     show_notice("Eje sin retencion: mover, soltar y volver a guardar");
   else{
     char msg[70];
     snprintf(msg,sizeof(msg),"ORDEN RECHAZADA: %.15s",(char*)(rx+16));
     show_notice(msg);
   }
   held=false;moving=false;
  }else if(rx[3]==1){
   if(op==3&&held){
     // HOLD_START is issued once while the finger remains down. The
     // controller validates it; the HMI does NOT wait for its delayed ACK.
     // A live deadman heartbeat is sent while pressed, and the live axis
     // state is checked before showing the motor as moving.
     startPosition=readInt32(rx+8+4*(heldAxis-1));
     startPending=true;
     startUnconfirmedNotice=false;
     startSentAt=millis();
     if(!sendMotor(4,heldAxis,heldUp,0,false)){
       startPending=false;held=false;
       sendMotor(2);
       show_notice("Fallo de escritura al arrancar: PARADA");
     }
    }
   else if(op==4){if(held){moving=true;holdOwned=true;}else sendMotor(5);}
   else if(op==9){
     show_notice(wizardMode==0?"Calibrando Z1: dispensacion.":"Calibrando Z2: dispensacion.");
   }
   else if(op==10){
     heightSaved();
   }else if(op==5){moving=false;show_notice("Movimiento detenido, eje retenido");}
   else if(op==13){
      autoRunning=(motorFlags&1)!=0;
      holdOwned=true;
      if(pairPhase==1){pairPhase=2;pairPhaseStarted=now;}
      else if(pairPhase==3){pairPhase=4;pairPhaseStarted=now;}
      else show_notice("Desplazamiento autorizado");
   }
  }
 }
 // SAVE_Z_HEIGHT is already durable before the motor controller updates
 // its I2C report. Reconcile a delayed ACK directly from stored NVS-backed
 // heights. No extra operator step or duplicate save is needed.
 if(waitingOp==10 && pendingSaveAxis>=1 && pendingSaveAxis<=2){
   const uint8_t ax=pendingSaveAxis,k=pendingSaveKind;
   const uint8_t fresh=(ax==1?(k==1?2:4):(k==1?8:16));
   const int32_t stored=(k==1?zRest[ax-1]:zDisp[ax-1]);
   if((motorFresh&fresh) && stored==pendingSavePosition &&
       (motorMarks&(ax==1?(k==1?1:2):(k==1?4:8)))){
     waitingOp=0;
     heightSaved();
   }
 }
 // A coordinated dispensing action is ONE touch command, but two safe
 // consecutive moves: first retract the other nozzle completely.
 if(pairPhase==2 || pairPhase==4){
   const uint8_t ax=(pairPhase==2)?pairOther:pairChosen;
   const bool referenced=(motorFlags&(ax==1?16:32))!=0;
   const bool holding=(motorFlags&(ax==1?4:8))!=0;
   const int32_t target=(pairPhase==2)?zRest[ax-1]:zDisp[ax-1];
   if(!(motorFlags&1)){
      if(referenced && holding && zPos[ax-1]==target){
        if(pairPhase==2){
          pairPhase=3;pairPhaseStarted=now;
          if(!sendMotor(13,pairChosen,2,0,true)){
             pairPhase=0;autoRunning=false;
             show_notice("Fallo al enviar DISPENSACION: comprobar enlace");
          }else show_notice("Aguja contraria en reposo. Bajando seleccionada.");
        }else{
          pairPhase=0;autoRunning=false;
          show_notice("Cambio completado: una dispensando, otra en reposo");
        }
      }else{
        pairPhase=0;autoRunning=false;
        show_notice("Cambio interrumpido: posicion/referencia no confirmada");
      }
   }else if(!(motorFlags&2)){
     pairPhase=0;autoRunning=false;
     sendMotor(2);
     show_notice("Movimiento inesperado: controlador detenido");
   }
 }
 if(pairPhase && (uint32_t)(now-pairPhaseStarted)>65000UL){
   pairPhase=0;autoRunning=false;
   sendMotor(2);
   show_notice("Tiempo de cambio excedido: parada del controlador");
 }
 if(now-lastStatusDraw>=350){
  lastStatusDraw=now;
  if(connectionText)lv_label_set_text(connectionText,"I2C: CONECTADO 0x32");
  if(driverText)lv_label_set_text(driverText,"CONECTADO");
  for(int i=0;i<2;i++){
   if(axisText[i])lv_label_set_text(axisText[i],
      (motorFlags&(i?128:64))?"TMC2209: UART OK":"TMC2209: ESPERANDO VM/UART");
   // Keep the control surface clean. Position counters remain inside
   // controller telemetry for motion safety, never on REST/DISP buttons.
   if(positionText[i])
      lv_label_set_text(positionText[i],
        (motorMarks&(i?12:3))==(i?12:3)
            ?"ALTURAS GUARDADAS":"ALTURAS POR CALIBRAR");
  }
 }
 if(!wasOnline && (motorFlags&0xC0)!=0xC0)
   show_notice("I2C conectada. Esperando que arranquen los drivers.");
 // Manual movement is held only by continuous touch and watchdog beats.
 // This is independent of the lagging reply sequence on HOLD_START.
 if(held && (startPending||moving) && (uint32_t)(now-lastBeat)>=90){
    sendMotor(6);lastBeat=now;
 }
 // The I2C slave can report the previous state while a TMC2209 is
 // starting. Do not issue a false disable because one status was delayed.
 // This HMI remains hold-to-run: releasing the finger sends HOLD_STOP,
 // the motor ESP32 watchdog stops in 350 ms without heartbeats,
 // and loss of the I2C link still locks new movements.
 if(startPending && held && !startUnconfirmedNotice &&
    (uint32_t)(now-startSentAt)>650){
    startUnconfirmedNotice=true;
    show_notice("Arranque pendiente de confirmar; soltar para detener");
 }
 // Retry a stationary height save with the SAME sequence if the previous
 // command was lost in an I2C/NVS transition. This is idempotent only while
 // the axis remains held at its captured position.
 if(waitingOp==10 && startRetryCount<2 &&
    (uint32_t)(now-startRetryAt)>1800 &&
    pendingSaveAxis>=1 && pendingSaveAxis<=2 &&
    !(motorFlags&1) &&
    (motorFlags&(pendingSaveAxis==1?4:8)) &&
    zPos[pendingSaveAxis-1]==pendingSavePosition){
     Wire1.beginTransmission(MOTOR_I2C);
     Wire1.write(lastCriticalPacket,sizeof(lastCriticalPacket));
     const uint8_t rc=Wire1.endTransmission();
     startRetryAt=now;startRetryCount++;
     Serial.printf("[VALVER I2C] SAVE retry %u seq=%u result=%u\n",
         (unsigned)startRetryCount,(unsigned)waitingSeq,(unsigned)rc);
 }
 // Height saves and NVS copies can be slower than motion commands.
 // A missing configuration ACK must NOT trigger DISABLE_CHANNELS:
 // that would invalidate the physical Z reference mid-calibration.
 const bool configCommand=(waitingOp==8||waitingOp==9||
      waitingOp==10||waitingOp==11||waitingOp==12);
 const uint32_t ackTimeoutMs=configCommand?6500UL:850UL;
 if(waitingOp && (uint32_t)(now-waitingSince)>ackTimeoutMs){
  const uint8_t timedOutOp=waitingOp,timedOutSeq=waitingSeq;
  waitingOp=0;
  if(configCommand){
    // Never advance the calibration wizard without confirmation.
    // Operator may retry the current OK/save step once the link is healthy.
    char msg[120];
    snprintf(msg,sizeof(msg),
     "Altura no guardada op=%u seq=%u; pulsa OK de nuevo",
     (unsigned)timedOutOp,(unsigned)timedOutSeq);
    show_notice(msg);
  }else{
    held=false;moving=false;autoRunning=false;holdOwned=false;startPending=false;pairPhase=0;
    sendMotor(2);
    char details[140];
    snprintf(details,sizeof(details),
     "ACK movimiento agotado op=%u seq=%u recibido=%u estado=%u fail=%lu",
     (unsigned)timedOutOp,(unsigned)timedOutSeq,
     (unsigned)lastRemoteAck,(unsigned)lastRemoteStatus,
     (unsigned long)badReadCount);
    show_notice(details);
  }
 }
 // Feed the motor-controller deadman using ACTUAL telemetry, not the
 // screen's transient 'holdOwned' flag. This remains valid when navigating
 // CALIBRAR -> CONTROL Z, after each save, and when calibrating the other
 // axis. Never invent a reference: only COM5 can report it.
 if((motorFlags&1) && (held||startPending||moving||autoRunning||pairPhase)){
   if((uint32_t)(now-lastBeat)>=90){sendMotor(6);lastBeat=now;}
 }else if(!(motorFlags&1) && (motorFlags&12)){
   if((uint32_t)(now-lastBeat)>=150){sendMotor(7);lastBeat=now;}
 }
 if(!(motorFlags&1)){
    if(moving && held && !startPending) {
      held=false;holdOwned=false;
      show_notice("Motor detenido por controlador: revisar watchdog");
    }
    moving=false;autoRunning=false;
  }
}
