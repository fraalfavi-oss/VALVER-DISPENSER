let port=null,reader=null,writer=null,active=false,id=1,buffer='',closing=false;
const $=x=>document.getElementById(x);
function log(s){$('log').textContent+='\n'+s;$('log').scrollTop=$('log').scrollHeight}
function ui(on){$('state').textContent=on?'USB CONECTADO':'DESCONECTADO';$('state').style.color=on?'#50e2ac':'#f5b4b4';
for(const b of ['disconnect','ping','info','status']) $(b).disabled=!on;
$('connect').disabled=on}
async function close(){if(closing)return;closing=true;active=false;ui(false);
try{if(reader)await reader.cancel()}catch{}
try{if(reader)reader.releaseLock()}catch{}reader=null;
try{if(writer)writer.releaseLock()}catch{}writer=null;
try{if(port)await port.close()}catch(e){log('Cierre: '+e.message)}port=null;closing=false}
async function listen(){const decode=new TextDecoder();
try{while(active&&reader){const {value,done}=await reader.read();if(done)break;
buffer+=decode.decode(value,{stream:true});
if(buffer.length>4096){log('Respuesta demasiado larga');buffer='';}
let at;while((at=buffer.indexOf('\n'))>=0){const line=buffer.slice(0,at).trim();buffer=buffer.slice(at+1);
if(line){try{const obj=JSON.parse(line);log('RX ← '+JSON.stringify(obj))}
catch{log('RX (no JSON) ← '+line)}}}
}}catch(e){log('Lectura: '+e.message)}finally{if(active)await close()}}
async function connect(){if(!('serial' in navigator)){log('Web Serial no disponible. Usa Chrome/Edge en localhost.');return}
try{port=await navigator.serial.requestPort();await port.open({baudRate:115200});
reader=port.readable.getReader();writer=port.writable.getWriter();active=true;ui(true);log('COM abierto a 115200');void listen()}
catch(e){log('Conexión: '+e.message);await close()}}
async function send(cmd){if(!active||!writer)return;const msg={id:id++,cmd};try{await writer.write(new TextEncoder().encode(JSON.stringify(msg)+'\n'));log('TX → '+JSON.stringify(msg))}catch(e){log('Envío: '+e.message)}}
$('connect').onclick=connect;$('disconnect').onclick=close;
$('ping').onclick=()=>send('PING');$('info').onclick=()=>send('GET_INFO');$('status').onclick=()=>send('GET_STATUS');
for(const n of [1,2]){const e=$('z'+n);function select(){$('z1').classList.toggle('selected',n===1);$('z2').classList.toggle('selected',n===2);log('Z'+n+' seleccionado (solo HMI)')}
e.onclick=select;e.onkeydown=ev=>{if(ev.key==='Enter'||ev.key===' '){ev.preventDefault();select()}}}
