let port=null,reader=null,writer=null,connected=false,closing=false,rx='',counter=1,pingOK=false,verified=false,testing=false,selected=0,jogHeld=false,jogTimer=null,txQueue=Promise.resolve(),autoSelected=false,stallData={z1_uart:false,z2_uart:false,z1_enabled:false,z2_enabled:false,z1_threshold:40,z2_threshold:40},monitorRequested={1:false,2:false},loadSamples=[],loadBaselineSamples=[],loadBaseline=null;
const el=id=>document.getElementById(id);
function resetLoad(axis=selected){
 loadSamples=[];loadBaselineSamples=[];loadBaseline=null;
 el('loadRaw').textContent='—';el('loadPercent').textContent='—';
 el('loadBaseline').textContent='Referencia sin calibrar';
 el('loadDirection').textContent='Z'+(axis||'?')+' · Sin lectura';
 el('loadFill').style.width='0%';
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
   el('loadBaseline').textContent='Tomando referencia en vacío ('+loadBaselineSamples.length+'/5)';
   el('loadStatus').textContent=raw===0?'Sin lectura útil / SG_RESULT = 0':'Midiendo referencia';
   return;
 }
 const relative=Math.max(0,Math.min(100,Math.round((loadBaseline-raw)/Math.max(1,loadBaseline)*100)));
 el('loadPercent').textContent=relative+'%';
 el('loadBaseline').textContent='Referencia libre: '+loadBaseline+' · índice relativo';
 el('loadStatus').textContent='Z'+axis+' · LECTURA EN VIVO';
 el('loadFill').style.width=relative+'%';
 loadSamples.push(relative);if(loadSamples.length>80)loadSamples.shift();
 drawLoadChart();
}
function log(s){const t=el('log');t.textContent+='\n'+s;t.scrollTop=t.scrollHeight}
function update(){
 const ready=connected&&pingOK&&verified&&!testing;
 el('state').textContent=connected?(pingOK?'ESP32 RESPONDE':'COM ABIERTO · SIN PONG'):'DESCONECTADO';
 el('state').style.color=pingOK?'#5fe2ad':'#f5b4b4';
 for(const id of ['ping','info','status','sensors','readiness','disconnect'])el(id).disabled=!connected;
 el('connect').disabled=connected;
 el('stop').disabled=!connected;
 el('speed').disabled=!ready;
 for(const id of ['speedMinus','speedPlus','speedNumber'])el(id).disabled=!ready;
 const axisReady=ready&&selected>0;
 el('stallApply').disabled=!axisReady;
 el('stallEnable').disabled=!axisReady||!stallData['z'+selected+'_uart']||stallData['z'+selected+'_enabled'];
 el('stallDisable').disabled=!axisReady||!stallData['z'+selected+'_enabled'];
 el('stallSensitivity').disabled=!axisReady;
 for(const id of ['stallMinus','stallPlus','stallNumber'])el(id).disabled=!axisReady;
 for(const id of ['choose1','choose2','disable'])el(id).disabled=!connected||!pingOK||testing;
 el('channelState').textContent=selected?'Z'+selected:'NINGUNO';
 el('stallState').textContent=selected?(stallData['z'+selected+'_enabled']?'Z'+selected+' · ACTIVA':stallData['z'+selected+'_uart']?'Z'+selected+' · UART OK, detector apagado':'Z'+selected+' · UART sin conexión'):'Selecciona Z1/Z2';
 el('armState').textContent=testing?'MOVIENDO · Z'+selected:'MOTORES DETENIDOS';
 for(const n of [1,2]){
  el('card'+n).classList.toggle('selected',selected===n);
  el('card'+n).classList.toggle('locked',selected!==n);
 }
 document.querySelectorAll('.test').forEach(b=>b.disabled=(!jogHeld&&(!ready||selected===0))||!b.dataset.cmd.includes('Z'+selected));
}
function response(data){
 if(data.reply==='PONG'){
  pingOK=true;
  log('Comunicación verificada con PONG');
  if(!autoSelected){autoSelected=true;send('SELECT_Z1');log('Z1: selección automática enviada');}
  send('GET_STATUS');send('GET_SENSORS');
 }
 if(data.state==='HOLD_MOTOR_JOG'){
   verified=true;

 }
 if(Number.isFinite(data.speed_sps)&&data.status!=='SAMPLE'){
   el('speed').value=String(data.speed_sps);el('speedNumber').value=String(data.speed_sps);el('speedValue').textContent=String(data.speed_sps);
   if(!jogHeld&&data.speed_sps<400)el('loadStatus').textContent='Para medir carga, usa 400 pasos/s o más';
 }
 if(typeof data.selected_axis==='number'){
  selected=data.selected_axis;
  if(selected){el('stallSensitivity').value=String(stallData['z'+selected+'_threshold']??40);el('stallNumber').value=el('stallSensitivity').value;el('stallValue').textContent=el('stallSensitivity').value;}
 }
 if(data.stallguard){
  stallData={...stallData,...data.stallguard};
  // Configure passive monitoring once the UART is confirmed.
  // This does not activate the automatic stall stop.
  for(const axis of [1,2]){
    if(stallData['z'+axis+'_uart']&&!stallData['z'+axis+'_monitoring']&&!monitorRequested[axis]){
      monitorRequested[axis]=true;
      send('MONITOR_STALL',{axis});
    }
  }
  el('sensorReadout').textContent='StallGuard4 · Z1 UART: '+(stallData.z1_uart?'OK':'sin conexión')+' · Z2 UART: '+(stallData.z2_uart?'OK':'sin conexión');
  if(selected){el('stallSensitivity').value=String(stallData['z'+selected+'_threshold']??40);el('stallNumber').value=el('stallSensitivity').value;el('stallValue').textContent=el('stallSensitivity').value;}
 }
 if(data.status==='SAMPLE'&&data.axis){
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
async function connect(){
 if(!('serial' in navigator)){log('Este navegador no admite Web Serial');return;}
 try{
  port=await navigator.serial.requestPort();await port.open({baudRate:115200});
  reader=port.readable.getReader();writer=port.writable.getWriter();
  connected=true;pingOK=false;verified=false;selected=0;autoSelected=false;monitorRequested={1:false,2:false};resetLoad(0);update();
  log('COM abierto');void readLoop();void send('PING');
 }catch(e){log('Error de conexión: '+e.message);await disconnect();}
}
async function disconnect(){
 if(closing)return;endJog();closing=true;connected=false;pingOK=false;verified=false;testing=false;selected=0;autoSelected=false;update();
 try{await reader?.cancel()}catch{}
 try{reader?.releaseLock()}catch{}reader=null;
 try{writer?.releaseLock()}catch{}writer=null;
 try{await port?.close()}catch(e){log('Cierre: '+e.message)}
 port=null;closing=false;
}
function send(cmd,fields={}){
 if(!connected||!writer)return Promise.resolve();
 const m={id:counter++,cmd,...fields};
 const payload=new TextEncoder().encode(JSON.stringify(m)+'\n');
 txQueue=txQueue.catch(()=>{}).then(async()=>{
  if(!connected||!writer)return;
  await writer.write(payload);
  if(cmd!=='JOG_MOTOR_HEARTBEAT')log('TX → '+JSON.stringify(m));
 }).catch(e=>{log('Error de envío: '+e.message);endJog();});
 return txQueue;
}
el('connect').onclick=connect;el('disconnect').onclick=disconnect;
el('ping').onclick=()=>send('PING');
el('info').onclick=()=>send('GET_INFO');
el('status').onclick=()=>send('GET_STATUS');
el('sensors').onclick=()=>send('GET_SENSORS');
function setStallValue(v){const n=Math.max(0,Math.min(255,Math.round(Number(v)||0)));el('stallSensitivity').value=String(n);el('stallNumber').value=String(n);el('stallValue').textContent=String(n);}
el('stallSensitivity').oninput=()=>setStallValue(el('stallSensitivity').value);
el('stallNumber').onchange=()=>setStallValue(el('stallNumber').value);
el('stallMinus').onclick=()=>setStallValue(Number(el('stallSensitivity').value)-1);
el('stallPlus').onclick=()=>setStallValue(Number(el('stallSensitivity').value)+1);
el('stallApply').onclick=()=>{if(selected)send('SET_STALL_SENSITIVITY',{axis:selected,threshold:Number(el('stallSensitivity').value)})};
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
 if(b.disabled||jogHeld||!connected||!verified)return;
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
