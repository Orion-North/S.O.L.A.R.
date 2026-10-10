import {test} from 'node:test';
import assert from 'node:assert/strict';
import {zeroImu,resetImuZero,imuZeroLabel} from '../imu-calibration.js';
const reply=data=>({ok:true,status:200,json:async()=>data});
test('zero waits for persistent completion rather than acknowledging sample collection as success',async()=>{
 const states=[{calibration_state:'collecting',calibration_samples:50},{calibration_state:'saving'},{calibration_state:'complete',calibrated:true,gyro_bias_dps:[1,2,3]}];
 const requests=[],progress=[];
 const result=await zeroImu(async(path,params,options)=>{requests.push({path,options});return path==='/imu/calibrate'?{ok:true,status:202}:reply(states.shift())},data=>progress.push(data.calibration_state),async()=>{});
 assert.equal(requests[0].options.method,'POST');assert.equal(result.calibrated,true);
 assert.deepEqual(progress,['collecting','saving','complete']);
});
test('rejected zero does not poll or report success',async()=>{
 let calls=0;await assert.rejects(zeroImu(async()=>{calls++;return {ok:false,status:409,text:async()=> 'Disable motors first'}},()=>{},async()=>{}),/Disable motors/);assert.equal(calls,1);
});
test('movement failure is surfaced and never replaces the zero in the panel',async()=>{
 await assert.rejects(zeroImu(async path=>path==='/imu/calibrate'?{ok:true}:reply({calibration_state:'failed',calibrated:true,calibration_message:'Movement detected'}),()=>{},async()=>{}),/Movement detected/);
});
test('temporary polling throttle is retried and confirmation timeout remains explicit',async()=>{
 let polls=0;const result=await zeroImu(async path=>path==='/imu/calibrate'?{ok:true}:++polls===1?{ok:false,status:429}:reply({calibration_state:'complete',calibrated:true}),()=>{},async()=>{});assert.equal(result.calibrated,true);
 await assert.rejects(zeroImu(async path=>path==='/imu/calibrate'?{ok:true}:reply({calibration_state:'collecting'}),()=>{},async()=>{}),/confirmation timed out/);
});
test('reset uses POST and surfaces rejection',async()=>{
 const requests=[];const result=await resetImuZero(async(path,params,options)=>{requests.push({path,options});return reply({calibrated:false})});assert.equal(result.calibrated,false);assert.equal(requests[0].options.method,'POST');
 await assert.rejects(resetImuZero(async()=>({ok:false,status:409,text:async()=> 'Calibration still running'})),/still running/);
 assert.equal(imuZeroLabel({calibration_state:'collecting',calibration_samples:70}),'SAMPLING 70/100 · HOLD STILL');assert.equal(imuZeroLabel({calibrated:true}),'ZERO STORED');
});
