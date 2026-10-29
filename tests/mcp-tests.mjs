import {spawn} from 'node:child_process';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {resolve,dirname} from 'node:path';
const exe=resolve(dirname(fileURLToPath(import.meta.url)),'../dist/simple-md.exe');
const child=spawn(exe,['--mcp'],{windowsHide:true,stdio:['pipe','pipe','pipe']});
const waiting=new Map();let buffer='',serial=0,stderr='';
child.stderr.on('data',d=>stderr+=d);
child.stdout.on('data',d=>{buffer+=d;for(;;){const end=buffer.indexOf('\n');if(end<0)break;const line=buffer.slice(0,end);buffer=buffer.slice(end+1);const reply=JSON.parse(line);const key=JSON.stringify(reply.id);const entry=waiting.get(key);if(entry){waiting.delete(key);entry(reply);}}});
child.on('error',e=>{throw e;});
function request(method,params={}){return new Promise((done,reject)=>{const id=++serial;const timer=setTimeout(()=>reject(new Error(`Timeout: ${method}`)),5000);waiting.set(JSON.stringify(id),r=>{clearTimeout(timer);done(r);});child.stdin.write(JSON.stringify({jsonrpc:'2.0',id,method,params})+'\n');});}
try{
  let r=await request('tools/list');assert.equal(r.error.code,-32002);
  r=await request('initialize',{protocolVersion:'2025-11-25',capabilities:{},clientInfo:{name:'simple-md-test',version:'1'}});assert.equal(r.result.protocolVersion,'2025-11-25');assert.equal(r.result.serverInfo.name,'simple-md');
  child.stdin.write(JSON.stringify({jsonrpc:'2.0',method:'notifications/initialized'})+'\n');
  r=await request('tools/list');assert.deepEqual(r.result.tools.map(t=>t.name),['list_documents','read_document','update_document']);
  r=await request('ping');assert.deepEqual(r.result,{});
  r=await request('unknown');assert.equal(r.error.code,-32601);
  r=await request('tools/call',{name:'list_documents',arguments:{}});assert.equal(r.result.isError,true);assert.match(r.result.content[0].text,/enable MCP/);
  r=await request('initialize',{protocolVersion:'unsupported'});assert.equal(r.result.protocolVersion,'2025-11-25');
  assert.equal(stderr,'');console.log('PASS: MCP stdio handshake, notifications, discovery, ping, errors, default-off access, version fallback.');
}finally{child.stdin.end();child.kill();}
