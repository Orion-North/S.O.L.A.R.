const {test}=require('node:test');
const assert=require('node:assert/strict');
const vm=require('node:vm');
const {readFileSync}=require('node:fs');
const {resolve}=require('node:path');
function camera(response, elapsed=0) {
 const events={}, delays=[]; let now=10000, requests=0, src='';
 const surface={innerText:'',textContent:'',classList:{add(){},remove(){},toggle(){}},addEventListener(name,fn){events[name]=fn}};
 const image={...surface, set src(value){src=value},get src(){return src}};
 const context=vm.createContext({AbortController,Date:{now:()=>now},URL:{createObjectURL:()=> 'blob:frame',revokeObjectURL(){}},
  liveFeedBtn:surface,feedStatus:surface,camStream:image,cameraPlaceholder:surface,statusBlock:surface,targetIpText:surface,
  ipInput:{value:'robot.invalid'},logTerminal(){},clearTimeout(){},setTimeout(fn,delay){delays.push(delay);return 1},
  fetchRobot:async()=>{requests++;now+=elapsed;return typeof response==='function'?await response():response}
 });
 const source=readFileSync(resolve(__dirname,'../main.js'),'utf8');
 vm.runInContext(source.slice(source.indexOf('  let liveFeed ='),source.indexOf('  function normalizeImuData')),context);
 vm.runInContext('globalThis.api={setLiveFeedState,requestCameraFrame};',context);
 return {api:context.api,events,delays,image,get requests(){return requests}};
}
const frame=(interval=50)=>({ok:true,status:200,headers:new Headers({'x-camera-interval-ms':String(interval)}),blob:async()=>new Blob(['jpeg'],{type:'image/jpeg'})});

function flashControl(response) {
 const source=readFileSync(resolve(__dirname,'../main.js'),'utf8');
 const events={}, requests=[], logs=[], attrs={};
 const button={textContent:'',disabled:false,classList:{toggle(){}},setAttribute:(key,value)=>attrs[key]=value,addEventListener:(name,fn)=>events[name]=fn};
 const context=vm.createContext({AbortSignal,flashBtn:button,ipInput:{value:'robot.invalid'},
  fetchRobot:async(path,params)=>{requests.push({path,params});return typeof response==='function'?await response():response},logTerminal:message=>logs.push(message)});
 vm.runInContext(source.slice(source.indexOf('  let flashState ='),source.indexOf('  let liveFeed =')),context);
 return {button,attrs,requests,logs,click:events.click};
}

test('front flash button commits on/off state only after acknowledgement',async()=>{
 const f=flashControl({ok:true});
 await f.click(); assert.equal(f.requests[0].path,'/flash'); assert.equal(f.requests[0].params.state,1);
 assert.equal(f.button.textContent,'FLASH ON'); assert.equal(f.attrs['aria-pressed'],'true');
 await f.click(); assert.equal(f.requests[1].params.state,0);
 assert.equal(f.button.textContent,'FLASH OFF'); assert.equal(f.attrs['aria-pressed'],'false');
});

test('rejected flash command keeps previous state and allows retry',async()=>{
 const f=flashControl({ok:false,status:403}); await f.click();
 assert.equal(f.button.textContent,'FLASH OFF'); assert.equal(f.button.disabled,false);
 assert.ok(f.logs.some(message=>message.includes('FAILED')));
 await f.click(); assert.equal(f.requests[1].params.state,1);
});

test('front flash blocks overlapping requests and recovers from network failure',async()=>{
 let reject; const f=flashControl(()=>new Promise((resolve,rejectPromise)=>{reject=rejectPromise}));
 const first=f.click(); assert.equal(f.button.disabled,true); await f.click();
 assert.equal(f.requests.length,1); reject(new Error('offline')); await first;
 assert.equal(f.button.disabled,false); assert.equal(f.button.textContent,'FLASH OFF');
});
test('camera pacing includes request time and follows the walking interval',async()=>{
 const c=camera(frame(100),40); c.api.setLiveFeedState(true); await new Promise(setImmediate);
 c.events.load(); assert.equal(c.delays.at(-1),65);
});
test('camera rate limiting respects the remaining delay instead of dropping to 4 FPS',async()=>{
 const c=camera({ok:false,status:429,headers:new Headers({'x-camera-interval-ms':'50','x-retry-after-ms':'12'})});
 c.api.setLiveFeedState(true); await new Promise(setImmediate); assert.equal(c.delays.at(-1),17);
});
test('stopping the camera discards a late IPC response and overlapping requests',async()=>{
 let release; const pending=new Promise(r=>release=r); const c=camera(()=>pending);
 c.api.setLiveFeedState(true); await c.api.requestCameraFrame(); assert.equal(c.requests,1);
 c.api.setLiveFeedState(false);release(frame());await new Promise(setImmediate);
 assert.equal(c.image.src,'');assert.equal(c.delays.length,0);
});

test('desktop JPEG bytes become an image without base64 conversion and preserve pacing headers',async()=>{
 const source=readFileSync(resolve(__dirname,'../main.js'),'utf8');
 const context=vm.createContext({Response,Blob,Headers,Uint8Array});
 vm.runInContext(source.slice(source.indexOf('  function responseFromDesktop'),source.indexOf('  async function persistConfig')),context);
 const jpeg=[255,216,1,2,255,217];
 const response=context.responseFromDesktop({status:200,contentType:'image/jpeg',bodyBytes:new Uint8Array(jpeg),headers:{'x-camera-interval-ms':'50'}});
 assert.equal(response.headers.get('x-camera-interval-ms'),'50');
 assert.equal(response.headers.get('content-type'),'image/jpeg');
 assert.deepEqual([...new Uint8Array(await response.arrayBuffer())],jpeg);
});
