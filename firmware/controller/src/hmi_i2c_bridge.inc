// VALVER SMART DISPENSER — wired I2C bridge.
// Included after dispatch() in main.cpp. All motor interlocks remain in dispatch().
// 9 byte request: 0x56, 1, sequence, operation, axis, argument, value(lo,hi), xor.
// 32 byte response: 0x56,1,ackSeq,ackResult,error,flags,marks,axis,
// positions Z1/Z2, rest Z1/Z2, dispense Z1/Z2 (all int32 LE).
// Error responses contain a short rejection reason in the final 16 bytes.
// No Wi-Fi. I2C callbacks only queue/copy; motion is always serviced in loop().
namespace {
constexpr uint8_t HMI_ADDR=0x32, HMI_VERSION=1, HMI_SDA=11, HMI_SCL=12;
constexpr uint8_t HMI_QUEUE_SIZE=8;
struct HmiWireCommand { uint8_t p[9]; };
HmiWireCommand hmiQueue[HMI_QUEUE_SIZE];
uint8_t hmiHead=0, hmiTail=0;
portMUX_TYPE hmiMux=portMUX_INITIALIZER_UNLOCKED;
uint8_t hmiReport[32]={0x56,HMI_VERSION};
uint8_t hmiAckSeq=0,hmiAckResult=0,hmiError=0;
char hmiReason[17]={0};
uint8_t hmiLastProcessedStartSeq=0;
bool hmiHasProcessedStart=false;
uint8_t hmiLastSavedSeq=0;
bool hmiHasSavedSequence=false;
uint32_t hmiLastSavedAt=0;
uint32_t hmiLastProcessedStartMs=0;
uint32_t hmiDroppedCommands=0;
uint32_t hmiDuplicateStarts=0;
void hmiRx(int /*count*/) {
  uint8_t data[9]={0}; uint8_t len=0;
  while(Wire.available()) {
    const uint8_t c=(uint8_t)Wire.read();
    if(len<sizeof(data))data[len]=c;
    if(len<255)len++;
  }
  if(len!=sizeof(data)||data[0]!=0x56||data[1]!=HMI_VERSION)return;
  uint8_t crc=0; for(uint8_t i=0;i<8;i++)crc^=data[i];
  if(crc!=data[8])return;
  portENTER_CRITICAL(&hmiMux);
  // STOP has priority over any queued movement commands.
  // When the queue is full, non-emergency commands are dropped,
  // but the safety STOP must still be accepted.
  const uint8_t operation=(uint8_t)(data[3]&0x7F);
  const bool isStop=(operation==2 || operation==5);
  if(isStop)hmiTail=hmiHead;
  uint8_t next=(uint8_t)((hmiHead+1)%HMI_QUEUE_SIZE);
  // When stationary, do not let idle heartbeat chatter evict a confirmed
  // height-save or calibration request. Active motion never uses this path.
  if(next==hmiTail && (data[3]&0x80) && !trial.active &&
     (hmiQueue[hmiTail].p[3]&0x7F)==7){
    // Evict ONLY a replaceable idle heartbeat, never STOP or a save.
    hmiTail=(uint8_t)((hmiTail+1)%HMI_QUEUE_SIZE);
    next=(uint8_t)((hmiHead+1)%HMI_QUEUE_SIZE);
  }
  if(next!=hmiTail) {
    memcpy(hmiQueue[hmiHead].p,data,sizeof(data));
    hmiHead=next;
  }else {
    hmiDroppedCommands++;
  }
  portEXIT_CRITICAL(&hmiMux);
}
void hmiTx() {
  uint8_t snapshot[32];
  portENTER_CRITICAL(&hmiMux);
  memcpy(snapshot,hmiReport,sizeof(snapshot));
  portEXIT_CRITICAL(&hmiMux);
  Wire.write(snapshot,sizeof(snapshot));
}
void hmiI32(uint8_t* dest,int32_t number) {
  uint32_t value=(uint32_t)number;
  for(uint8_t i=0;i<4;i++)dest[i]=(uint8_t)(value>>(i*8));
}
void hmiRefresh() {
  uint8_t output[32]={0x56,HMI_VERSION};
  output[2]=hmiAckSeq; output[3]=hmiAckResult;
  // In addition to the last command error (bit 0), expose session-fresh
  // calibration markers so HMI can safely reconcile delayed NVS replies.
  output[4]=(hmiError&1)|(heights[1].restFresh?2:0)|
       (heights[1].dispenseFresh?4:0)|(heights[2].restFresh?8:0)|
       (heights[2].dispenseFresh?16:0);
  output[5]=(trial.active?1:0)|(trial.automatic?2:0)|
      (holdingAxis[1]?4:0)|(holdingAxis[2]?8:0)|
      (heights[1].referenced?16:0)|(heights[2].referenced?32:0)|
      (stall[1].uart?64:0)|(stall[2].uart?128:0);
  output[6]=(heights[1].restSet?1:0)|(heights[1].dispenseSet?2:0)|
      (heights[2].restSet?4:0)|(heights[2].dispenseSet?8:0)|
      (flashReady?16:0);
  output[7]=trial.active?trial.axis:armedAxis;
  hmiI32(output+8,heights[1].position);
  hmiI32(output+12,heights[2].position);
  hmiI32(output+16,heights[1].rest);
  hmiI32(output+20,heights[2].rest);
  hmiI32(output+24,heights[1].dispense);
  hmiI32(output+28,heights[2].dispense);
  if(hmiAckResult==2) {
    memset(output+16,0,16);
    strncpy((char*)(output+16),hmiReason,15);
  }
  portENTER_CRITICAL(&hmiMux);
  memcpy(hmiReport,output,sizeof(output));
  portEXIT_CRITICAL(&hmiMux);
}
void hmiExecute(const HmiWireCommand& request) {
  const uint8_t* p=request.p;
  // Bit 7 requests an acknowledged command. Heartbeats/other
  // fire-and-forget messages never overwrite a pending confirmation.
  const bool wantsAck=(p[3]&0x80)!=0;
  const uint8_t op=p[3]&0x7F,axis=p[4],arg=p[5];
  const uint16_t value=(uint16_t)p[6]|((uint16_t)p[7]<<8);
  const unsigned long id=p[2];
  // A retransmission uses the original transaction sequence. An already
  // processed HOLD_START must NEVER energize a motor a second time.
  if(wantsAck && op==4 && hmiHasProcessedStart &&
     hmiLastProcessedStartSeq==p[2] &&
     (uint32_t)(millis()-hmiLastProcessedStartMs)<1200){
    hmiDuplicateStarts++;
    return;
  }
  // SAVE_Z_HEIGHT is retried with the same sequence if flash/I2C delays
  // its ACK. Never execute the same transaction twice: a late duplicate
  // must not overwrite a height after the operator has moved the nozzle.
  if(wantsAck && op==10 && hmiHasSavedSequence &&
     hmiLastSavedSeq==p[2] &&
     (uint32_t)(millis()-hmiLastSavedAt)<9000UL){
     return; // Keep the existing latest ACK; do not roll it backwards.
  }
  char json[192]={0};
  switch(op) {
    case 1: snprintf(json,sizeof(json),"{\"id\":%lu,\"cmd\":\"PING\"}",id);break;
    case 2: snprintf(json,sizeof(json),"{\"id\":%lu,\"cmd\":\"DISABLE_CHANNELS\"}",id);break;
    case 3: snprintf(json,sizeof(json),"{\"id\":%lu,\"cmd\":\"PREPARE_TEST\",\"axis\":%u,\"confirm\":1}",id,axis);break;
    case 4: snprintf(json,sizeof(json),"{\"id\":%lu,\"cmd\":\"HOLD_START\",\"axis\":%u,\"up\":%u}",id,axis,arg);break;
    case 5: snprintf(json,sizeof(json),"{\"id\":%lu,\"cmd\":\"HOLD_STOP\"}",id);break;
    case 6: snprintf(json,sizeof(json),"{\"id\":%lu,\"cmd\":\"HOLD_HEARTBEAT\"}",id);break;
    case 7: snprintf(json,sizeof(json),"{\"id\":%lu,\"cmd\":\"HOLD_IDLE_HEARTBEAT\"}",id);break;
    case 8: snprintf(json,sizeof(json),"{\"id\":%lu,\"cmd\":\"SET_HOLD_SPEED\",\"speed_sps\":%u}",id,value);break;
    case 9: snprintf(json,sizeof(json),"{\"id\":%lu,\"cmd\":\"BEGIN_Z_CALIBRATION\",\"axis\":%u}",id,axis);break;
    case 10:snprintf(json,sizeof(json),"{\"id\":%lu,\"cmd\":\"SAVE_Z_HEIGHT\",\"axis\":%u,\"kind\":%u}",id,axis,arg);break;
    case 11:snprintf(json,sizeof(json),"{\"id\":%lu,\"cmd\":\"COPY_Z1_HEIGHTS_TO_Z2\"}",id);break;
    case 12:snprintf(json,sizeof(json),"{\"id\":%lu,\"cmd\":\"REFERENCE_Z_REST\",\"axis\":%u,\"confirm\":1}",id,axis);break;
    case 13:snprintf(json,sizeof(json),"{\"id\":%lu,\"cmd\":\"GO_TO_Z_HEIGHT\",\"axis\":%u,\"kind\":%u,\"confirm\":1}",id,axis,arg);break;
    case 14:snprintf(json,sizeof(json),"{\"id\":%lu,\"cmd\":\"GET_DRIVER_TEST\"}",id);break;
    default:
      if(wantsAck) {
        hmiAckSeq=p[2];hmiAckResult=2;hmiError=1;
        strncpy(hmiReason,"OPERACION_INVALIDA",16);
      }
      return;
  }
  hmiDispatching=true;
  hmiDispatchRejected=false;
  hmiDispatchError[0]=0;
  dispatch(json); // reuse the USB controller's exact safeguards
  hmiDispatching=false;
  // Publish transaction ACK immediately, before any optional logging.
  if(wantsAck) {
    hmiAckSeq=p[2]; hmiAckResult=hmiDispatchRejected?2:1;
    hmiError=hmiDispatchRejected?1:0;
    if(hmiDispatchRejected) {
      strncpy(hmiReason,hmiDispatchError,16);
      hmiReason[16]=0;
    } else hmiReason[0]=0;
  }
  if(wantsAck && op==10){
    hmiHasSavedSequence=true;
    hmiLastSavedSeq=p[2];
    hmiLastSavedAt=millis();
  }
  if(wantsAck && op==4) {
    hmiHasProcessedStart=true;
    hmiLastProcessedStartSeq=p[2];
    hmiLastProcessedStartMs=millis();
  }
  hmiRefresh(); // publish before the next I2C master read
  // No Serial.printf here: USB logging must not delay the stepper deadman.
}
void hmiService() {
  HmiWireCommand job{};
  bool pending=false;
  portENTER_CRITICAL(&hmiMux);
  if(hmiTail!=hmiHead) {
    job=hmiQueue[hmiTail];
    hmiTail=(uint8_t)((hmiTail+1)%HMI_QUEUE_SIZE);
    pending=true;
  }
  portEXIT_CRITICAL(&hmiMux);
  if(pending)hmiExecute(job);
  // Periodic UART recovery is owned by serviceTmcAutoLink() in the
  // motor loop. Avoid double probing the TMC drivers on the I2C path.
  hmiRefresh();
}
void hmiInit() {
  Wire.onReceive(hmiRx);
  Wire.onRequest(hmiTx);
  const bool started=Wire.begin((uint8_t)HMI_ADDR,HMI_SDA,HMI_SCL,100000);
  Serial.printf("[I2C] VALVER motor bridge 0x%02X SDA=%u SCL=%u %s\n",
    HMI_ADDR,HMI_SDA,HMI_SCL,started?"READY":"FAILED");
  hmiRefresh();
}
} // namespace
