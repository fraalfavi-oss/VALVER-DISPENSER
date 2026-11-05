let port=null,reader=null,writer=null,connected=false,closing=false,rx='',counter=1,pingOK=false,verified=false,testing=false,selected=0,jogHeld=false,jogTimer=null,txQueue=Promise.resolve(),autoSelected=false,stallData={z1_uart:false,z2_uart:false,z1_enabled:false,z2_enabled:false,z1_threshold:40,z2_threshold:40},monitorRequested={1:false,2:false},loadSamples=[],loadBaselineSamples=[],loadBaseline=null,loadSuggestedThreshold=null,loadGains={1:8,2:1},coldMode=true,latestCold=null,coldPollTimer=null,opening=false,armedTestAxis=0,trialActive=false,trialBeatTimer=null,testReadyUart={1:false,2:false},preparingTest=false,pendingPulse=null,pressHeld=false,heldControlId=null;
const el=id=>document.getElementById(id);
const heightPositions={1:0,2:0};
let keypadDraft='0';
function safeTouchHeight(v){
 const n=Number(v);
 return Number.isSafeInteger(n)&&n>=-2000000&&n<=2000000?n:null;
}
function setTouchHeight(v){
 const n=safeTouchHeight(v);
 if(n===null)return false;
 el('sharedHeightValue').value=String(n);
 return true;
}
function refreshKeypad(){
 el('heightKeypadDisplay').textContent=keypadDraft;
 const n=safeTouchHeight(keypadDraft);
 el('heightKeypadError').textContent=n===null?'Altura fuera de ±2.000.000 micropasos':'';
 el('keypadApply').disabled=n===null;
}
function openHeightKeypad(){
 if(trialActive||preparingTest||pressHeld)return;
 keypadDraft=String(safeTouchHeight(el('sharedHeightValue').value)??0);
 el('heightKeypad').hidden=false;
 document.body.classList.add('keypad-open');
 refreshKeypad();
}
function closeHeightKeypad(){
 el('heightKeypad').hidden=true;
 document.body.classList.remove('keypad-open');
}
function keypadInput(key){
 if(key==='clear')keypadDraft='0';
 else if(key==='back'){
  keypadDraft=keypadDraft.slice(0,-1);
  if(keypadDraft===''||keypadDraft==='-')keypadDraft='0';
 }else if(key==='sign'){
  keypadDraft=keypadDraft.startsWith('-')?keypadDraft.slice(1):'-'+keypadDraft;
 }else if(key==='stepdown'||key==='stepup'){
  const n=safeTouchHeight(keypadDraft);
  if(n!==null)keypadDraft=String(Math.max(-2000000,Math.min(2000000,n+(key==='stepup'?100:-100))));
 }else if(/^\d{1,2}$/.test(key)){
  const digits=keypadDraft.startsWith('-')?keypadDraft.slice(1):keypadDraft;
  if(digits.length<8){
   const neg=keypadDraft.startsWith('-');
   const next=(digits==='0'?'':digits)+key;
   keypadDraft=(neg?'-':'')+next;
  }
 }
 refreshKeypad();
}
const diagSamples={1:[],2:[]};
const diagInfo={1:null,2:null};
function diagSummary(axis){
 const raw=diagSamples[axis].filter(s=>Number.isFinite(s.sg)&&s.speed>=400);
 if(!raw.length)return 'Z'+axis+': sin muestras';
 const values=raw.map(s=>s.sg).sort((a,b)=>a-b);
 const min=values[0],max=values[values.length-1],med=values[Math.floor(values.length/2)];
 const zeros=values.filter(v=>v===0).length;
 const t=raw.filter(s=>Number.isFinite(s.tstep)&&s.tstep>0);
 return 'Z'+axis+': '+values.length+' lecturas · SG mediana '+med+
   ' · rango '+min+'–'+max+' · ceros '+zeros+
   (t.length?' · TSTEP '+t[t.length-1].tstep:'');
}
function diagRefresh(){
 el('diagZ1').textContent=diagSummary(1);
 el('diagZ2').textContent=diagSummary(2);
 const a=diagSamples[1].filter(s=>s.speed>=400), b=diagSamples[2].filter(s=>s.speed>=400);
 let conclusion='Realiza 5 segundos de giro libre con cada eje, a la misma velocidad.';
 if(a.length>=6&&b.length>=6){
   const az=a.filter(s=>s.sg===0).length,bz=b.filter(s=>s.sg===0).length;
   if(az===a.length&&bz<b.length)conclusion='Z1 SG_RESULT siempre 0 y Z2 da valores: problema de señal/configuración Z1, no de la escala HMI.';
   else if(az<a.length&&bz<b.length)conclusion='Ambos reportan SG_RESULT: calibra por separado a igual velocidad y sin carga.';
   else conclusion='Lecturas insuficientes o saturadas: revisa diagnóstico de registros y modo de funcionamiento.';
 }
 el('diagVerdict').textContent='Resultado: '+conclusion;
}
function diagReset(){
 diagSamples[1]=[];diagSamples[2]=[];
 diagInfo[1]=null;diagInfo[2]=null;
 el('diagCompareResult').textContent='Diagnóstico pendiente';
 diagRefresh();
}
function resetLoad(axis=selected){
 loadSamples=[];loadBaselineSamples=[];loadBaseline=null;loadSuggestedThreshold=null;
 el('loadRaw').textContent='—';el('loadPercent').textContent='—';
 el('loadTstep').textContent='TSTEP: pendiente';
 const gain=loadGains[axis]||1;el('loadGain').value=String(gain);el('loadGainValue').textContent=gain+'×';
 el('loadBaseline').textContent='Referencia sin calibrar';
 el('loadDirection').textContent='Z'+(axis||'?')+' · Sin lectura';
 el('loadFill').style.width='0%';
 el('loadDelta').textContent='Variación SG: —';
 el('loadSuggested').textContent='Umbral: esperando referencia en vacío';
 el('stallSuggest').disabled=true;
 el('loadStatus').textContent='Esperando lecturas válidas';
 drawLoadChart();
}
function drawLoadChart(){
 const canvas=el('loadChart');const ctx=canvas.getContext('2d');
 const w=canvas.width,h=canvas.height;
 ctx.clearRect(0,0,w,h);
 ctx.fillStyle='#0c1e31';ctx.fillRect(0,0,w,h);
 ctx.lineWidth=1;ctx.strokeStyle='#29435c';
 for(let i=0;i<=4;i++){let y=6+(h-12)*i/4;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(w,y);ctx.stroke();}
 if(loadSamples.length<2)return;
 const count=80,spacing=w/(count-1);ctx.beginPath();
 for(let i=0;i<loadSamples.length;i++){
  const p=loadSamples[i],x=w-(loadSamples.length-1-i)*spacing,y=h-6-(h-12)*p/100;
  if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
 }
 ctx.lineWidth=2.5;ctx.strokeStyle='#42c9b7';ctx.stroke();
}
function receiveLoad(d){
 if(!Number.isFinite(d.sg_result)||![1,2].includes(d.axis))return;
 if(selected!==d.axis)return;
 const raw=d.sg_result,axis=d.axis,dir=d.direction==='UP'?'SUBIR':d.direction==='DOWN'?'BAJAR':'GIRO';
 if(axis===1&&Number.isFinite(d.tstep))el('loadTstep').textContent='Z1 TSTEP='+d.tstep+(d.tstep>=1048575?' · fuera de rango':'');
 else if(axis===2)el('loadTstep').textContent='Z2: lectura SG_RESULT';
 el('loadRaw').textContent=String(raw);
 el('loadDirection').textContent='Z'+axis+' · '+dir+' · '+d.speed_sps+' pasos/s';
 if(raw>0&&loadBaselineSamples.length<5){
  loadBaselineSamples.push(raw);
  if(loadBaselineSamples.length===5){
    const sorted=[...loadBaselineSamples].sort((a,b)=>a-b);
    loadBaseline=sorted[2]; // median of first five unloaded samples
  }
 }
 if(loadBaseline===null){
   el('loadPercent').textContent='—';
   el('loadDelta').textContent='Variación SG: —';
   el('loadBaseline').textContent='Tomando referencia en vacío ('+loadBaselineSamples.length+'/5)';
   el('loadStatus').textContent=raw===0?'Sin lectura útil / SG_RESULT = 0':'Midiendo referencia';
   return;
 }
 const delta=loadBaseline-raw;
 el('loadDelta').textContent='Variación SG: '+(delta>=0?'+':'')+delta+' (positivo = más carga)';
 const gain=loadGains[axis]||1;
 const relative=Math.max(0,Math.min(100,Math.round((delta/Math.max(1,loadBaseline))*100*gain)));
 // This is a provisional 10% drop from the unloaded SG_RESULT, not a force setpoint.
 // Hardware SGTHRS is limited to 255, i.e. trip level <= 510.
 if(loadBaseline>=20){
  loadSuggestedThreshold=Math.min(255,Math.max(0,Math.round(loadBaseline*0.90/2)));
  const suggestedTrip=2*loadSuggestedThreshold;
  const limited=loadBaseline*0.9>510;
  el('loadSuggested').textContent='Umbral orientativo SGTHRS='+loadSuggestedThreshold+
    ' (paro si SG_RESULT ≤ '+suggestedTrip+')'+(limited?' · LIMITADO por hardware':'');
  el('stallSuggest').disabled=testing||!verified;
 }else{
  loadSuggestedThreshold=null;
  el('loadSuggested').textContent='Referencia demasiado baja para sugerir sensibilidad';
  el('stallSuggest').disabled=true;
 }
 el('loadPercent').textContent=relative+'% (×'+gain+')';
 el('loadBaseline').textContent='Referencia libre: '+loadBaseline+' · índice relativo';
 el('loadStatus').textContent='Z'+axis+' · LECTURA EN VIVO';
 el('loadFill').style.width=relative+'%';
 loadSamples.push(relative);if(loadSamples.length>80)loadSamples.shift();
 drawLoadChart();
}
function log(s){const t=el('log');t.textContent+='\n'+s;t.scrollTop=t.scrollHeight}
function showColdTest(d){
 latestCold=d;
 const z1=d.z1||{},z2=d.z2||{};
 const good=d.cold_lock===true&&d.gpio_local_safe===true&&
  z1.en_high===true&&z1.step_low===true&&z1.dir_low===true&&
  z2.en_high===true&&z2.step_low===true&&z2.dir_low===true;
 el('coldGpio').textContent=good?'OK · ESTADO LOCAL SEGURO':'FALLO · NO ALIMENTAR';
 el('coldGpio').style.color=good?'#a7f0c8':'#ffaaaa';
 el('coldResult').textContent=good?'ESP32 Y GPIO OK · RESTO DEL CABLEADO NO VERIFICADO':
   'FALLO EN GPIO DEL ESP32 · NO CONECTAR 24 V';
 el('coldResult').style.color=good?'#b8f4d5':'#ffaaaa';
 el('coldDetails').textContent='Z1: EN '+(z1.en_high?'HIGH OK':'ERROR')+
   ' / STEP '+(z1.step_low?'LOW OK':'ERROR')+' / DIR '+(z1.dir_low?'LOW OK':'ERROR')+
   '   |   Z2: EN '+(z2.en_high?'HIGH OK':'ERROR')+
   ' / STEP '+(z2.step_low?'LOW OK':'ERROR')+' / DIR '+(z2.dir_low?'LOW OK':'ERROR');
 el('coldLast').textContent='Última comprobación automática: '+new Date().toLocaleTimeString('es-ES')+
  ' · Solo lectura de GPIO ESP32; no prueba continuidad externa';
 log('AUTO-CHECK: '+(good?'GPIO locales OK':'FALLO GPIO')+' | conexiones físicas NO verificadas');
}

function showZHeights(d){
 for(const axis of [1,2]){
   const h=d['z'+axis]||{};
   heightPositions[axis]=Number(h.position??0);
   el('z'+axis+'Position').textContent='Pos. '+(h.position??0);
   el('z'+axis+'RestValue').textContent=h.rest_set===true?String(h.rest):'—';
   el('z'+axis+'DispenseValue').textContent=h.dispense_set===true?String(h.dispense):'—';
 }
}
function updateMotorTest(){
 const ready=connected&&pingOK&&verified&&!preparingTest&&!trialActive;
 const approved=el('motorChecked').checked;
 for(const axis of [1,2]){
   el('z'+axis+'Comms').textContent=testReadyUart[axis]?'UART OK':'UART SIN RESPUESTA';
   el('z'+axis+'Comms').style.color=testReadyUart[axis]?'#8bdfb4':'#f1b36e';
   for(const dir of ['Up','Down']){
     const id='motorZ'+axis+dir;
     // Keep held button enabled: disabling it may lose pointer capture.
     el(id).disabled=(!ready||!approved||!testReadyUart[axis])&&!(pressHeld&&heldControlId===id);
   }
 }
 el('motorStop').disabled=!connected;
 el('holdSpeed').disabled=!connected||!verified||preparingTest||trialActive;
 for(const axis of [1,2]){
   const active=connected&&verified&&!trialActive&&!preparingTest&&!pressHeld&&testReadyUart[axis];
   for(const kind of ['Rest','Dispense'])el('saveZ'+axis+kind).disabled=!active;
   el('clearZ'+axis+'Heights').disabled=!active;
 }
 const sharedReady=connected&&verified&&!trialActive&&!preparingTest&&!pressHeld;
 el('sharedRest').disabled=!sharedReady;
 el('sharedDispense').disabled=!sharedReady;
 for(const axis of [1,2])el('copyZ'+axis+'Height').disabled=!sharedReady;

}
function stopTrialHeartbeat(){
 if(trialBeatTimer){clearInterval(trialBeatTimer);trialBeatTimer=null;}
 trialActive=false;preparingTest=false;pendingPulse=null;armedTestAxis=0;
 updateMotorTest();
}
function stopHeldMotor(){
 const wasMoving=preparingTest||trialActive||pressHeld;
 pressHeld=false;heldControlId=null;stopTrialHeartbeat();
 if(wasMoving&&connected)void send('STOP_TEST').then(()=>send('GET_Z_HEIGHTS'));
 if(wasMoving)el('rgbPinNote').textContent='LED apagado · motores detenidos';
}
function startMotorHold(axis,up,e){
 if(e.pointerType==='mouse'&&e.button!==0)return;
 if(!connected||!pingOK||!verified||pressHeld||preparingTest||trialActive||!testReadyUart[axis]||
    !el('motorChecked').checked)return;
 pressHeld=true;heldControlId='motorZ'+axis+(up?'Up':'Down');
 const btn=el(heldControlId);
 btn.setPointerCapture?.(e.pointerId);
 preparingTest=true;pendingPulse={axis,up};armedTestAxis=0;
 el('motorTestState').textContent='Z'+axis+' · verificando UART y corriente antes de mover…';
 updateMotorTest();
 void send('PREPARE_TEST',{axis,confirm:1});
}
function launchPreparedHold(axis,up){
 if(!pressHeld||!connected||!verified||!el('motorChecked').checked)return;
 trialActive=true;preparingTest=false;pendingPulse=null;armedTestAxis=axis;
 el('motorTestState').textContent='Z'+axis+' · MOVIENDO mientras mantengas pulsado';
 updateMotorTest();
 void send('HOLD_START',{axis,up:up?1:0});
 if(trialBeatTimer)clearInterval(trialBeatTimer);
 trialBeatTimer=setInterval(()=>{
   if(connected&&trialActive&&pressHeld&&!document.hidden)void send('HOLD_HEARTBEAT');
   else stopHeldMotor();
 },110);
}
function update(){
 const ready=connected&&pingOK&&verified&&!testing&&!coldMode;
 el('state').textContent=connected?(pingOK?(verified?'ESP32 · PRUEBA LIMITADA':'COM · VERIFICAR FIRMWARE'):'COM ABIERTO · SIN PONG'):'DESCONECTADO';
 el('state').style.color=pingOK&&verified?'#f0c581':'#f5b4b4';
 for(const id of ['ping','info','status','sensors','readiness','disconnect'])el(id).disabled=!connected;
 el('connect').disabled=connected;

 el('stop').disabled=!connected;
 el('speed').disabled=!ready;
 el('diagCompare').disabled=!ready || jogHeld;
 for(const id of ['speedMinus','speedPlus','speedNumber'])el(id).disabled=!ready;
 const axisReady=ready&&selected>0;
 el('stallApply').disabled=!axisReady;
 el('stallEnable').disabled=!axisReady||!stallData['z'+selected+'_uart']||stallData['z'+selected+'_enabled'];
 el('stallDisable').disabled=!axisReady||!stallData['z'+selected+'_enabled'];
 el('stallSensitivity').disabled=!axisReady;
 el('stallSuggest').disabled=!axisReady||loadSuggestedThreshold===null;
 for(const id of ['stallMinus','stallPlus','stallNumber'])el(id).disabled=!axisReady;
 for(const id of ['choose1','choose2'])el(id).disabled=true;
 el('disable').disabled=!connected||!pingOK;
 el('channelState').textContent=selected?'Z'+selected:'NINGUNO';
 el('stallState').textContent=selected?(stallData['z'+selected+'_enabled']?'Z'+selected+' · ACTIVA':stallData['z'+selected+'_uart']?'Z'+selected+' · UART OK, detector apagado':'Z'+selected+' · UART sin conexión'):'Selecciona Z1/Z2';
 el('armState').textContent=trialActive?'MOVIMIENTO MANTENIDO':'MOTORES DETENIDOS · CORRIENTE PROTEGIDA';
 for(const n of [1,2]){
  el('card'+n).classList.toggle('selected',selected===n);
  el('card'+n).classList.toggle('locked',selected!==n);
 }
 document.querySelectorAll('.test').forEach(b=>b.disabled=true);
 updateMotorTest();
}
function response(data){
 if(data.reply==='PONG'){
   pingOK=true;
   el('coldUsb').textContent='OK · PONG';
   el('coldUsb').style.color='#a7f0c8';
   log('PONG recibido · verificando versión segura antes de diagnóstico automático');
   send('GET_INFO');send('GET_STATUS');
 }
 if(data.device==='VALVER SMART DISPENSER'&&typeof data.firmware==='string'){
   el('coldFirmware').textContent=(data.mode==='HOLD_MOTOR_TEST'?'OK · ':'NO SEGURO · ')+data.firmware;
   el('coldFirmware').style.color=data.mode==='HOLD_MOTOR_TEST'?'#a7f0c8':'#ffaaaa';
 }
 if(typeof data.mode==='string'&&data.mode!=='HOLD_MOTOR_TEST'){
   verified=false;
   el('coldResult').textContent='FIRMWARE ANTIGUO: actualiza a 1.0.0-touch-hold';
 }
 if(data.state==='HOLD_MOTOR_TEST'){
   if(!verified){
     verified=true;coldMode=true;
     el('coldFirmware').textContent='OK · BLOQUEO HOLD_MOTOR_TEST';
     el('coldFirmware').style.color='#a7f0c8';
     log('Bloqueo de movimiento confirmado · iniciando autodiagnóstico');
     send('GET_COLD_CHECK');send('GET_DRIVER_TEST');send('GET_Z_HEIGHTS');
     if(coldPollTimer)clearInterval(coldPollTimer);
     coldPollTimer=setInterval(()=>{
       if(connected&&pingOK&&verified&&!closing&&!trialActive&&!preparingTest){void send('GET_COLD_CHECK');void send('GET_DRIVER_TEST');}
     },5000);
   }
 }
 if(data.state==='HOLD_MOTOR_JOG'){
   verified=false;coldMode=true;
   el('coldResult').textContent='FIRMWARE ANTIGUO: no continuar sin actualizar';
 }
 if(data.cold_check){showColdTest(data.cold_check);}
 if(data.z_heights){
   showZHeights(data.z_heights);
   if(data.z_heights.flash_ready===false)
     el('heightCalNotice').textContent='ERROR: memoria permanente no disponible; no se pueden guardar alturas ni velocidad.';
 }
 if(data.height_saved){
   el('heightCalNotice').textContent='Z'+data.axis+' · '+(data.kind===1?'Reposo':'Dispensación')+
      ' guardada en '+data.position_steps+' micropasos relativos · guardado en ESP32. Sin origen físico.';
 }
 if(data.shared_height_saved){
   el('heightCalNotice').textContent='Valor '+data.value+' guardado para '+
     (data.kind===1?'REPOSO':'DISPENSACIÓN')+' en Z1 y Z2 · memoria del ESP32; referencia física pendiente.';
 }
 if(data.height_cleared){
   el('heightCalNotice').textContent='Z'+data.axis+' · referencias borradas; posición relativa conservada.';
 }
 if(data.driver_test){
   testReadyUart[1]=data.driver_test.z1_uart===true;
   testReadyUart[2]=data.driver_test.z2_uart===true;
   el('coldUart').textContent='Z1: '+(testReadyUart[1]?'OK':'SIN RESPUESTA')+' · Z2: '+(testReadyUart[2]?'OK':'SIN RESPUESTA');
   if(!preparingTest&&!trialActive)
     el('motorTestState').textContent='Z1 '+(testReadyUart[1]?'UART OK':'sin UART')+' · Z2 '+(testReadyUart[2]?'UART OK':'sin UART')+
       ' · el movimiento solo se permite tras validar corriente y conexiones';
 }
 if(data.test_prepared){
   const pending=pendingPulse;
   if(pending&&preparingTest&&pressHeld&&Number(data.axis)===pending.axis&&el('motorChecked').checked&&verified){
     launchPreparedHold(pending.axis,pending.up);
   }else{
     stopTrialHeartbeat();
     void send('STOP_TEST');
     el('motorTestState').textContent='Preparación cancelada · movimiento no iniciado';
   }
 }
 if(data.hold_running){
   el('rgbPinNote').textContent=Number(data.axis)===1?'Z1 activo · luz azul':'Z2 activo · luz roja';
   trialActive=true;
   el('motorTestState').textContent='Z'+data.axis+' · MOVIENDO · '+(data.rms_target_ma??'?')+' mA RMS de corriente configurada';
 }
 if(data.status==='DONE'&&data.reason==='HOLD_TIMEOUT_OR_LINK_LOST'){
   el('rgbPinNote').textContent='Apagado · motor detenido';
   pressHeld=false;heldControlId=null;stopTrialHeartbeat();
   void send('GET_Z_HEIGHTS');
   el('motorTestState').textContent='Parada por seguridad (10 s o comunicación) · suelta y vuelve a pulsar';
 }

 if(Number.isFinite(data.hold_speed_sps)){
   const speed=Math.max(80,Math.min(8000,Math.round(Number(data.hold_speed_sps))));
   el('holdSpeed').value=String(speed);
   el('holdSpeedValue').textContent=String(speed);
   el('holdSpeedInfo').textContent='Velocidad '+speed+' micropasos/s · guardada en ESP32';
 }
 if(Number.isFinite(data.speed_sps)&&data.status!=='SAMPLE'){
   el('speed').value=String(data.speed_sps);el('speedNumber').value=String(data.speed_sps);el('speedValue').textContent=String(data.speed_sps);
   if(!jogHeld&&data.speed_sps<400)el('loadStatus').textContent='Para medir carga, usa 400 pasos/s o más';
 }
 if(typeof data.selected_axis==='number'){
  selected=data.selected_axis;
  if(selected){el('stallSensitivity').value=String(stallData['z'+selected+'_threshold']??40);el('stallNumber').value=el('stallSensitivity').value;el('stallValue').textContent=el('stallSensitivity').value;}
 }
 if(data.driver_diag){
   const a=data.driver_diag.z1,b=data.driver_diag.z2;
   diagInfo[1]=a;diagInfo[2]=b;
   const diffs=['gconf','chopconf','ihold_irun','pwmconf'].filter(k=>a[k]!==b[k]);
   const comm=(!a.uart||!b.uart)?'UART NO RESPONDE en '+(!a.uart?'Z1 ':'')+(!b.uart?'Z2':''):'UART de ambos OK';
   const compare=diffs.length?'DIFERENCIAS: '+diffs.join(', '):'Registros de configuración iguales';
   el('diagCompareResult').textContent=comm+' · '+compare;
   log('DIAGNÓSTICO TMC2209: '+JSON.stringify(data.driver_diag));
 }
 if(data.stallguard){
  stallData={...stallData,...data.stallguard};
  // Cold inspection: no automatic TMC2209 UART commands.
  el('sensorReadout').textContent='UART Z1/Z2: NO COMPROBADO (VM desconectado)';
  if(selected){el('stallSensitivity').value=String(stallData['z'+selected+'_threshold']??40);el('stallNumber').value=el('stallSensitivity').value;el('stallValue').textContent=el('stallSensitivity').value;}
 }
 if(data.status==='SAMPLE'&&data.axis){
   if(Number.isFinite(data.sg_result)){
     const list=diagSamples[data.axis];
     if(list&&list.length<100)list.push({sg:data.sg_result,speed:data.speed_sps,tstep:data.tstep,direction:data.direction});
     diagRefresh();
   }
   receiveLoad(data);
   el('stallResult').textContent='SG_RESULT Z'+data.axis+': '+data.sg_result+' · Umbral de parada: '+(2*data.threshold)+' · Velocidad actual: '+data.speed_sps+' pasos/s';
 }
 if(typeof data.monitoring==='boolean'&&data.axis){
   stallData['z'+data.axis+'_monitoring']=data.monitoring;
   send('GET_SENSORS');
 }
 if(typeof data.stall_enabled==='boolean'&&data.axis)stallData['z'+data.axis+'_enabled']=data.stall_enabled;
 if(Number.isFinite(data.threshold)&&data.axis)stallData['z'+data.axis+'_threshold']=data.threshold;
 if(data.status==='ERROR'){
   if(trialActive||preparingTest)void send('STOP_TEST');
   el('rgbPinNote').textContent='Apagado · revisa el error';
   pressHeld=false;heldControlId=null;stopTrialHeartbeat();
   el('motorTestState').textContent='Error: '+data.code+' · prueba no autorizada';
   log('ERROR: '+data.code);
   if(data.code==='UART_NOT_CONNECTED'){
     el('loadStatus').textContent='Sin UART · Verificar cableado';
   }
   endJog();
 }
 if(data.status==='DONE'||data.state==='JOG_STOPPED'||data.state==='STOPPED'){
   testing=false;
   el('loadStatus').textContent='Motor detenido · última lectura';
 }
 if(data.status==='ACCEPTED'&&data.mode==='MOTOR_JOG')testing=true;
 if(['HEARTBEAT_LOST','STALL_DETECTED','STALL_UART_LOST'].includes(data.reason)){log('PARADA AUTOMÁTICA: '+data.reason+(data.sg_result!==undefined?' · SG_RESULT='+data.sg_result:''));endJog();send('GET_SENSORS');}
 el('last').textContent=data.status+' · '+(data.code||data.mode||data.state||data.reply||'ESP32');
 update();
}
async function readLoop(){const decoder=new TextDecoder();
 try{while(connected&&reader){
  const {value,done}=await reader.read();if(done)break;
  rx+=decoder.decode(value,{stream:true});
  if(rx.length>4096){log('RX demasiado largo');rx='';}
  let pos;while((pos=rx.indexOf('\n'))>=0){
   const line=rx.slice(0,pos).trim();rx=rx.slice(pos+1);if(!line)continue;
   try{const d=JSON.parse(line);if(d.status!=='SAMPLE')log('RX ← '+line);response(d)}catch{log('RX texto ← '+line);if(line.includes('Brownout')||line.includes('ESP-ROM:')){verified=false;pingOK=false;selected=0;autoSelected=false;endJog();update();}}
  }
 }}catch(e){log('Error de lectura: '+e.message)}
 finally{if(connected)await disconnect()}
}
async function connect(authorizedPort=null){
 if(!('serial' in navigator)){log('Este navegador no admite Web Serial; usa Chrome o Edge');return;}
 if(connected||opening)return;
 opening=true;
 try{
  port=authorizedPort||await navigator.serial.requestPort();
  await port.open({baudRate:115200});
  reader=port.readable.getReader();writer=port.writable.getWriter();
  connected=true;pingOK=false;verified=false;coldMode=true;latestCold=null;armedTestAxis=0;trialActive=false;preparingTest=false;pendingPulse=null;pressHeld=false;heldControlId=null;testReadyUart={1:false,2:false};selected=0;autoSelected=false;monitorRequested={1:false,2:false};resetLoad(0);update();
  el('coldUsb').textContent='COM ABIERTO · ESPERANDO PONG';
  el('coldFirmware').textContent='PENDIENTE';
  el('coldGpio').textContent='PENDIENTE';
  el('coldResult').textContent='Diagnóstico automático iniciado…';
  el('coldDetails').textContent='—';
  el('coldLast').textContent='Esperando respuesta del firmware…';
  log('Puerto COM abierto, arranca comprobación automática');
  void readLoop();void send('PING');
 }catch(e){
  log('Error de conexión: '+e.message);
  el('coldResult').textContent='NO SE PUDO ABRIR COM · revisa que VS Code/PlatformIO no lo estén utilizando';
  await disconnect();
 }finally{opening=false;}
}
async function disconnect(){
 if(closing)return;
 if(coldPollTimer){clearInterval(coldPollTimer);coldPollTimer=null;}
 pressHeld=false;heldControlId=null;stopTrialHeartbeat();armedTestAxis=0;testReadyUart={1:false,2:false};
 endJog();closing=true;connected=false;pingOK=false;verified=false;testing=false;selected=0;autoSelected=false;
 el('coldUsb').textContent='DESCONECTADO';el('coldUsb').style.color='#ffaaaa';
 el('coldGpio').textContent='SIN LECTURA ACTUAL';
 el('coldResult').textContent='ESP32 desconectado · no interpretar lecturas anteriores como actuales';
 update();
 try{await reader?.cancel()}catch{}
 try{reader?.releaseLock()}catch{}reader=null;
 try{writer?.releaseLock()}catch{}writer=null;
 try{await port?.close()}catch(e){log('Cierre: '+e.message)}
 port=null;closing=false;
}
function send(cmd,fields={}){
 if(!connected||!writer)return Promise.resolve();
 const coldAllowed=new Set(['PING','GET_INFO','GET_STATUS','GET_READINESS','GET_SENSORS','GET_COLD_CHECK','COLD_SELF_TEST',
  'SELECT_Z1','SELECT_Z2','DISABLE_CHANNELS','STOP_TEST','JOG_GPIO_STOP','JOG_STOP','JOG_MOTOR_STOP',
  'GET_DRIVER_TEST','PREPARE_TEST','HOLD_START','HOLD_HEARTBEAT','SET_HOLD_SPEED',
  'GET_Z_HEIGHTS','SAVE_Z_HEIGHT','CLEAR_Z_HEIGHTS','SET_SHARED_HEIGHT']);
 if(!coldAllowed.has(cmd)){log('COMANDO BLOQUEADO EN FRÍO: '+cmd);return Promise.resolve();}
 const m={id:counter++,cmd,...fields};
 const payload=new TextEncoder().encode(JSON.stringify(m)+'\n');
 txQueue=txQueue.catch(()=>{}).then(async()=>{
  if(!connected||!writer)return;
  await writer.write(payload);
  if(cmd!=='JOG_MOTOR_HEARTBEAT'&&cmd!=='HOLD_HEARTBEAT')log('TX → '+JSON.stringify(m));
 }).catch(e=>{log('Error de envío: '+e.message);endJog();});
 return txQueue;
}
el('connect').onclick=()=>void connect();
el('disconnect').onclick=disconnect;
el('holdSpeed').oninput=()=>{
 el('holdSpeedValue').textContent=el('holdSpeed').value;
 el('holdSpeedInfo').textContent='Velocidad seleccionada: '+el('holdSpeed').value+' micropasos/s';
};
el('holdSpeed').onchange=()=>{
 if(!connected||!verified||trialActive||preparingTest)return;
 void send('SET_HOLD_SPEED',{speed_sps:Number(el('holdSpeed').value)});
};
el('motorChecked').onchange=()=>{stopHeldMotor();updateMotorTest();};
el('sharedHeightValue').addEventListener('click',openHeightKeypad);
el('sharedHeightValue').addEventListener('focus',()=>el('sharedHeightValue').blur());
for(const b of document.querySelectorAll('#heightKeypad [data-key]')){
 b.addEventListener('click',()=>keypadInput(b.dataset.key));
}
el('keypadClose').onclick=closeHeightKeypad;
el('keypadCancel').onclick=closeHeightKeypad;
el('keypadApply').onclick=()=>{
 if(setTouchHeight(keypadDraft))closeHeightKeypad();
};
for(const [id,delta] of [['heightMinus100',-100],['heightMinus10',-10],['heightPlus10',10],['heightPlus100',100]]){
 el(id).onclick=()=>setTouchHeight(Math.max(-2000000,Math.min(2000000,Number(el('sharedHeightValue').value)+delta)));
}
for(const axis of [1,2]){
 el('copyZ'+axis+'Height').onclick=()=>{
  if(trialActive||preparingTest||pressHeld)return;
  setTouchHeight(heightPositions[axis]);
  el('heightCalNotice').textContent='Altura Z'+axis+' copiada. Pulsa guardar reposo o dispensación para ambos motores.';
 };
}
document.addEventListener('keydown',e=>{if(e.key==='Escape'&&!el('heightKeypad').hidden)closeHeightKeypad();});
for(const [kind,id] of [[1,'sharedRest'],[2,'sharedDispense']]){
 el(id).onclick=()=>{
   if(!connected||!verified||trialActive||preparingTest||pressHeld)return;
   const txt=el('sharedHeightValue').value.trim();
   const value=Number(txt);
   if(!/^-?\d+$/.test(txt)||!Number.isInteger(value)||Math.abs(value)>2000000){
     el('heightCalNotice').textContent='Introduce un entero entre -2.000.000 y +2.000.000 micropasos.';
     return;
   }
   void send('SET_SHARED_HEIGHT',{kind,value});
 };
}
for(const axis of [1,2]){
 for(const [kind,name] of [[1,'Rest'],[2,'Dispense']]){
   el('saveZ'+axis+name).onclick=()=>{
     if(!connected||!verified||trialActive||preparingTest||pressHeld||!testReadyUart[axis])return;
     void send('SAVE_Z_HEIGHT',{axis,kind});
   };
 }
 el('clearZ'+axis+'Heights').onclick=()=>{
   if(!connected||!verified||trialActive||preparingTest||pressHeld)return;
   void send('CLEAR_Z_HEIGHTS',{axis});
 };
}
for(const [axis,up,id] of [[1,true,'motorZ1Up'],[1,false,'motorZ1Down'],[2,true,'motorZ2Up'],[2,false,'motorZ2Down']]){
 const btn=el(id);
 btn.addEventListener('pointerdown',e=>startMotorHold(axis,up,e));
 btn.addEventListener('pointerup',stopHeldMotor);
 btn.addEventListener('pointercancel',stopHeldMotor);
 btn.addEventListener('lostpointercapture',stopHeldMotor);
}
el('motorStop').onclick=()=>{
 stopHeldMotor();void send('STOP_TEST');
 el('motorTestState').textContent='PARADA · ambos drivers deshabilitados';
};
window.addEventListener('pointerup',stopHeldMotor);
window.addEventListener('blur',stopHeldMotor);
document.addEventListener('visibilitychange',()=>{if(document.hidden)stopHeldMotor()});
el('ping').onclick=()=>send('PING');
el('info').onclick=()=>send('GET_INFO');
el('status').onclick=()=>send('GET_STATUS');
el('sensors').onclick=()=>send('GET_SENSORS');
el('diagCompare').onclick=()=>log('DIAG_TMC no disponible: inspección sin 24 V');
el('diagReset').onclick=diagReset;
function setStallValue(v){const n=Math.max(0,Math.min(255,Math.round(Number(v)||0)));el('stallSensitivity').value=String(n);el('stallNumber').value=String(n);el('stallValue').textContent=String(n);}
el('loadGain').oninput=()=>{if(!selected)return;loadGains[selected]=Number(el('loadGain').value);el('loadGainValue').textContent=loadGains[selected]+'×';};
el('stallSensitivity').oninput=()=>setStallValue(el('stallSensitivity').value);
el('stallNumber').onchange=()=>setStallValue(el('stallNumber').value);
el('stallMinus').onclick=()=>setStallValue(Number(el('stallSensitivity').value)-1);
el('stallPlus').onclick=()=>setStallValue(Number(el('stallSensitivity').value)+1);
el('stallApply').onclick=()=>{if(selected)send('SET_STALL_SENSITIVITY',{axis:selected,threshold:Number(el('stallSensitivity').value)})};
el('stallSuggest').onclick=()=>{if(!selected||loadSuggestedThreshold===null||jogHeld)return;setStallValue(loadSuggestedThreshold);send('SET_STALL_SENSITIVITY',{axis:selected,threshold:loadSuggestedThreshold});};
el('stallEnable').onclick=()=>{if(selected)send('ENABLE_STALL',{axis:selected,enabled:1}).then(()=>send('GET_SENSORS'))};
el('stallDisable').onclick=()=>{if(selected)send('ENABLE_STALL',{axis:selected,enabled:0}).then(()=>send('GET_SENSORS'))};
el('readiness').onclick=()=>send('GET_READINESS');
el('choose1').onclick=()=>{resetLoad(1);return send('SELECT_Z1').then(()=>send('GET_SENSORS'))};
el('choose2').onclick=()=>{resetLoad(2);return send('SELECT_Z2').then(()=>send('GET_SENSORS'))};
el('disable').onclick=()=>{endJog();send('DISABLE_CHANNELS')};
el('stop').onclick=()=>{endJog();send('STOP_TEST')};
function setSpeedValue(v){const n=Math.max(20,Math.min(1600,Math.round(Number(v)||20)));el('speed').value=String(n);el('speedNumber').value=String(n);el('speedValue').textContent=String(n);}
function applySpeed(){if(!jogHeld)send('SET_SPEED',{speed_sps:Number(el('speed').value)})}
el('speed').oninput=()=>setSpeedValue(el('speed').value);
el('speed').onchange=applySpeed;
el('speedNumber').onchange=()=>{setSpeedValue(el('speedNumber').value);applySpeed();};
el('speedMinus').onclick=()=>{setSpeedValue(Number(el('speed').value)-1);applySpeed();};
el('speedPlus').onclick=()=>{setSpeedValue(Number(el('speed').value)+1);applySpeed();};
function endJog(){
 if(jogTimer){clearInterval(jogTimer);jogTimer=null;}
 if(!jogHeld)return;
 jogHeld=false;testing=false;el('loadStatus').textContent='Motor detenido · última lectura';update();
 void send('JOG_MOTOR_STOP');
}
function beginJog(b,e){
 if(coldMode||b.disabled||jogHeld||!connected||!verified)return;
 if(e.pointerType==='mouse'&&e.button!==0)return;
 jogHeld=true;testing=true;resetLoad(selected);el('loadStatus').textContent='Arrancando · se requieren ≥400 pasos/s';update();
 b.setPointerCapture?.(e.pointerId);
 void send(b.dataset.cmd);
 jogTimer=setInterval(()=>{
  if(!jogHeld)return;
  if(!connected||document.hidden){endJog();return;}
  void send('JOG_MOTOR_HEARTBEAT');
 },150);
}
document.querySelectorAll('.test').forEach(b=>{
 b.addEventListener('pointerdown',e=>beginJog(b,e));
 b.addEventListener('pointerup',endJog);
 b.addEventListener('pointercancel',endJog);
 b.addEventListener('lostpointercapture',endJog);
});
window.addEventListener('pointerup',endJog);
window.addEventListener('blur',endJog);
document.addEventListener('visibilitychange',()=>{if(document.hidden)endJog()});
update();
resetLoad(0);
diagRefresh();
// Reconnect automatically only when the browser has ALREADY granted exactly one serial port.
if('serial' in navigator){
 navigator.serial.getPorts().then(ports=>{
   if(!connected&&!opening&&ports.length===1){
     log('Puerto COM autorizado detectado · conexión automática');
     void connect(ports[0]);
   }else if(ports.length!==1){
     el('coldResult').textContent=ports.length?'Hay varios COM autorizados: selecciona el ESP32':
       'Primera conexión: pulsa Seleccionar COM y autoriza tu ESP32';
   }
 }).catch(e=>log('No se pudieron consultar puertos autorizados: '+e.message));
}else{
 el('coldResult').textContent='Web Serial no disponible en este navegador';
}
const descriptions={
home:['CONTROL Z','Selecciona una sola aguja. Los motores están energizados únicamente durante el movimiento.'],
tip:['CALIBRACIÓN DE PUNTA','Preparación: validar cableado y estado óptico en ambos sentidos; después referenciar Z1/Z2 por separado. Calibración física BLOQUEADA.'],
table:['ALTURA DE MESA','Preparación: validar UART y StallGuard4 a la velocidad de aproximación, con tope independiente. Detección de contacto BLOQUEADA.'],
test:['PRUEBAS DE SEÑALES GPIO','Selecciona Z1 o Z2 en INICIO y mantén un botón de movimiento. EN solo se habilita durante una pulsación mantenida.'],
diag:['DIAGNÓSTICO','Utiliza PING, INFO y Estado para comprobar la comunicación USB con el ESP32.'],
settings:['AJUSTES DE ALTURAS','Plan: guardar reposo y dispensación de Z1/Z2 en NVS tras homing y calibración verificados. Las posiciones quedan pendientes de referencia.']
};
function page(name){
const x=descriptions[name]||descriptions.home;
el('homePage').style.display=name==='home'?'block':'none';
el('detailPage').style.display=name==='home'?'none':'block';
el('detailTitle').textContent=x[0];el('detailText').textContent=x[1];
el('detailChannel').textContent=selected?'Z'+selected:'Ninguno';
document.querySelectorAll('button.nav').forEach(b=>b.classList.toggle('active',b.dataset.page===name));
}
document.querySelectorAll('[data-page]').forEach(b=>b.addEventListener('click',()=>page(b.dataset.page)));
