const fs = require('node:fs');
const path = require('node:path');
const repo = path.resolve(__dirname, '../..');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const nodes = new Map();
let now = 1000;
const context = {
  document: {getElementById(id) {
    if (!nodes.has(id)) nodes.set(id, {textContent:'', innerHTML:'', value:'0',
      classList:{add(){},remove(){},toggle(){}}, setAttribute(k,v){this[k]=v;},addEventListener(){}});
    return nodes.get(id);
  }},
  navigator: {userAgent:'unit-test'}, location:{protocol:'http:',host:'localhost'},
  performance:{now:()=>now}, setInterval(){}, console, DataView, BigInt, Uint8Array,
};
vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(repo, 'tools/uwb-position.js'),'utf8'), context);
const html = fs.readFileSync(path.join(repo, 'tools/range-dashboard.html'),'utf8');
vm.runInContext(html.split('<script>')[1].split('</script>')[0],context);
function packet(round, distances, status=0) {
  const data = new DataView(new ArrayBuffer(60));
  for (let i=0;i<3;i++) { const o=i*20;
    data.setUint8(o,1);data.setUint8(o+1,i);data.setUint8(o+2,0);data.setUint8(o+3,status);
    data.setBigUint64(o+4,BigInt(round),true);data.setUint32(o+12,distances[i],true);data.setUint32(o+16,0,true);
  } return data;
}
context.payload=packet('4294967297',[5000,5000,6000]);
vm.runInContext('showMetadata(payload)', context);
assert.match(nodes.get('positionValue').textContent,/x=3.000 m.*y=4.000 m.*measured/);
assert.equal(nodes.get('anchorDot').cx,6000);
assert.equal(nodes.get('tagDot').visibility,'visible');
now=2000;context.payload=packet('4294967298',[200,300,700]);
vm.runInContext('showMetadata(payload)',context);
assert.match(nodes.get('positionValue').textContent,/x=3.000 m.*held/);
assert.equal(nodes.get('positionReason').textContent,'triangle');
now=6000;vm.runInContext('renderPosition()',context);
assert.match(nodes.get('positionValue').textContent,/stale/);
assert.equal(nodes.get('tagDot').visibility,'visible');
console.log('Dashboard BLE metadata -> position, rejection and stale integration PASS');
