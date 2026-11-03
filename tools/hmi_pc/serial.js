let port=null,reader=null,writer=null,connected=false,closing=false,rx='',counter=1,pingOK=false,verified=false,testing=false,selected=0,jogHeld=false,jogTimer=null,txQueue=Promise.resolve(),autoSelected=false;
const el=id=>document.getElementById(id);
function log(s){const t=el('log');t.textContent+='\n'+s;t.scrollTop=t.scrollHeight}
function update(){
 const ready=connected&&pingOK&&verified&&!testing;
 el('state').textContent=connected?(pingOK?'ESP32 RESPONDE':'COM ABIERTO · SIN PONG'):'DESCONECTADO';
 el('state').style.color=pingOK?'#5fe2ad':'#f5b4b4';
 for(const id of ['ping','info','status','sensors','readiness','disconnect'])el(id).disabled=!connected;
 el('connect').disabled=connected;
 el('stop').disabled=!connected;
 el('speed').disabled=!ready;
 for(const id of ['choose1','choose2','disable'])el(id).disabled=!connected||!pingOK||testing;
 el('channelState').textContent=selected?'Z'+selected:'NINGUNO';
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
  send('GET_STATUS');
 }
 if(data.state==='HOLD_MOTOR_JOG'){
   verified=true;

 }
 if(Number.isFinite(data.speed_sps)){el('speed').value=String(data.speed_sps);el('speedValue').textContent=String(data.speed_sps);}
 if(typeof data.selected_axis==='number')selected=data.selected_axis;
 if(data.optical&&data.stallguard)el('sensorReadout').textContent='Sensor óptico: pendiente · StallGuard4: pendiente';
 if(data.status==='ERROR'){log('ERROR: '+data.code);endJog();}
 if(data.status==='DONE'||data.state==='JOG_STOPPED'||data.state==='STOPPED')testing=false;
 if(data.status==='ACCEPTED'&&data.mode==='MOTOR_JOG')testing=true;
 if(data.reason==='HEARTBEAT_LOST'){endJog();}
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
   try{const d=JSON.parse(line);log('RX ← '+line);response(d)}catch{log('RX texto ← '+line);if(line.includes('Brownout')||line.includes('ESP-ROM:')){verified=false;pingOK=false;selected=0;autoSelected=false;endJog();update();}}
  }
 }}catch(e){log('Error de lectura: '+e.message)}
 finally{if(connected)await disconnect()}
}
async function connect(){
 if(!('serial' in navigator)){log('Este navegador no admite Web Serial');return;}
 try{
  port=await navigator.serial.requestPort();await port.open({baudRate:115200});
  reader=port.readable.getReader();writer=port.writable.getWriter();
  connected=true;pingOK=false;verified=false;selected=0;autoSelected=false;update();
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
el('readiness').onclick=()=>send('GET_READINESS');
el('choose1').onclick=()=>send('SELECT_Z1');
el('choose2').onclick=()=>send('SELECT_Z2');
el('disable').onclick=()=>{endJog();send('DISABLE_CHANNELS')};
el('stop').onclick=()=>{endJog();send('STOP_TEST')};
el('speed').oninput=()=>{el('speedValue').textContent=el('speed').value};
el('speed').onchange=()=>{if(jogHeld)return;send('SET_SPEED',{speed_sps:Number(el('speed').value)})};
function endJog(){
 if(jogTimer){clearInterval(jogTimer);jogTimer=null;}
 if(!jogHeld)return;
 jogHeld=false;testing=false;update();
 void send('JOG_MOTOR_STOP');
}
function beginJog(b,e){
 if(b.disabled||jogHeld||!connected||!verified)return;
 if(e.pointerType==='mouse'&&e.button!==0)return;
 jogHeld=true;testing=true;update();
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
