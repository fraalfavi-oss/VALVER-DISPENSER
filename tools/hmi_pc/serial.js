let port=null,reader=null,writer=null,connected=false,closing=false,rx='',counter=1,pingOK=false,verified=false,testing=false,selected=0,armed=false;
const el=id=>document.getElementById(id);
function log(s){const t=el('log');t.textContent+='\n'+s;t.scrollTop=t.scrollHeight}
function update(){const ready=connected&&pingOK&&verified&&!testing&&armed&&el('confirm').checked;
el('state').textContent=connected?(pingOK?'ESP32 RESPONDE':'COM ABIERTO · SIN PONG'):'DESCONECTADO';
el('state').style.color=pingOK?'#5fe2ad':'#f5b4b4';
for(const id of ['ping','info','status','sensors','disconnect'])el(id).disabled=!connected;
el('connect').disabled=connected;el('stop').disabled=!connected;
for(const id of ['choose1','choose2','disable','arm','disarm'])el(id).disabled=!connected||!pingOK||!verified||testing;
el('arm').disabled=el('arm').disabled||!selected||armed||!el('confirm').checked;
el('disarm').disabled=el('disarm').disabled||!armed;
el('channelState').textContent=selected?'Z'+selected:'NINGUNO';
el('armState').textContent=armed?'AUTORIZADO PARA UNA PRUEBA':'BLOQUEADO';
for(const n of [1,2]){el('card'+n).classList.toggle('selected',selected===n);el('card'+n).classList.toggle('locked',selected!==n)}
document.querySelectorAll('.test').forEach(b=>b.disabled=!ready||!b.dataset.cmd.includes('Z'+selected))}
function response(data){el('last').textContent=data.status+' · '+(data.code||data.reply||data.state||data.test||'ESP32');
if(typeof data.selected_axis==='number'){selected=data.selected_axis;armed=data.armed===true;}
if(data.status==='DONE'||data.status==='ACCEPTED'){armed=false;}
if(data.status==='ERROR'){armed=false;log('Controlador rechazó la operación: '+data.code);}
if(data.optical && data.stallguard) {
 el('sensorReadout').textContent='Sensor óptico: '+(data.optical.configured?'configurado':'PENDIENTE')+' · StallGuard4 Z1/Z2: '+(data.stallguard.z1_uart&&data.stallguard.z2_uart?'UART verificado':'UART PENDIENTE');
 log('Calibraciones bloqueadas hasta validar sensores y movimiento');
}
if(data.reply==='PONG'){pingOK=true;log('Comunicación verificada con PONG')}
if(data.status==='OK'&&data.state==='GPIO_TEST_ONLY')verified=data.drivers_enabled===false&&data.motors_enabled===false;
if(data.status==='DONE'||(data.status==='OK'&&data.state==='STOPPED')||data.status==='ERROR')testing=false;
if(data.status==='ACCEPTED')testing=true;
update()}
async function readLoop(){const decoder=new TextDecoder();
try{while(connected&&reader){const {value,done}=await reader.read();if(done)break;
rx+=decoder.decode(value,{stream:true});if(rx.length>4096){log('ERROR: RX largo');rx=''}
let pos;while((pos=rx.indexOf('\n'))>=0){const line=rx.slice(0,pos).trim();rx=rx.slice(pos+1);if(!line)continue;
try{const data=JSON.parse(line);log('RX ← '+line);response(data)}catch{log('RX texto ← '+line)}}}}
catch(e){log('Error de lectura: '+e.message)}
finally{if(connected)await disconnect()}}
async function connect(){if(!('serial' in navigator)){log('Web Serial requiere Chrome o Edge en localhost');return}
try{port=await navigator.serial.requestPort();await port.open({baudRate:115200});
reader=port.readable.getReader();writer=port.writable.getWriter();connected=true;pingOK=false;verified=false;selected=0;armed=false;update();log('COM abierto · esperando firmware');void readLoop()}
catch(e){log('Error de conexión: '+e.message);await disconnect()}}
async function disconnect(){if(closing)return;closing=true;connected=false;pingOK=false;verified=false;testing=false;selected=0;armed=false;update();
try{await reader?.cancel()}catch{}try{reader?.releaseLock()}catch{}reader=null;
try{writer?.releaseLock()}catch{}writer=null;
try{await port?.close()}catch(e){log('Cierre: '+e.message)}port=null;closing=false}
async function send(cmd){if(!connected||!writer)return;const m={id:counter++,cmd};
try{await writer.write(new TextEncoder().encode(JSON.stringify(m)+'\n'));log('TX → '+JSON.stringify(m))}catch(e){log('Error de envío: '+e.message)}}
el('connect').onclick=connect;el('disconnect').onclick=disconnect;
el('sensors').onclick=()=>send('GET_SENSORS');
el('ping').onclick=()=>send('PING');el('info').onclick=()=>send('GET_INFO');el('status').onclick=()=>send('GET_STATUS');
el('choose1').onclick=()=>send('SELECT_Z1');
el('choose2').onclick=()=>send('SELECT_Z2');
el('disable').onclick=()=>send('DISABLE_CHANNELS');
el('arm').onclick=()=>send('ARM_TEST');
el('disarm').onclick=()=>send('DISARM_TEST');
el('stop').onclick=()=>send('STOP_TEST');el('confirm').onchange=update;
document.querySelectorAll('.test').forEach(b=>b.onclick=async()=>{if(b.disabled)return;testing=true;armed=false;update();await send(b.dataset.cmd)});
update();
const descriptions={
home:['CONTROL Z','Selecciona una sola aguja. Los motores siguen desconectados.'],
tip:['CALIBRACIÓN DE PUNTA','La lectura del sensor óptico todavía no está implementada. No se ejecuta calibración física.'],
table:['ALTURA DE MESA','La detección de contacto de mesa y StallGuard requieren validación antes de habilitarse.'],
test:['PRUEBAS DE SEÑALES GPIO','Selecciona Z1 o Z2 en INICIO y autoriza una prueba individual. EN permanece deshabilitado.'],
diag:['DIAGNÓSTICO','Utiliza PING, INFO y Estado para comprobar la comunicación USB con el ESP32.'],
settings:['AJUSTES DE ALTURAS','Las alturas de reposo y dispensación se configurarán tras validar los motores y la referencia Z.']
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
