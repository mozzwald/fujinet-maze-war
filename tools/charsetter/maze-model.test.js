const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const M=require('./maze-model');
const source=JSON.parse(fs.readFileSync(path.join(__dirname,'../../clients/lynx/art/maze-war-tiles.json')));
const fresh=()=>structuredClone(source);
test('57 fixed bindings, sizes, names and save/load round trip',()=>{
 const p=fresh(); M.validateTileset(p); assert.equal(M.entries(p).length,57);
 assert.deepEqual(M.animation(p,p.entries[12]).map(e=>e.id),['explosion_1','explosion_2','explosion_3','explosion_4']);
 assert.deepEqual(M.validateTileset(JSON.parse(JSON.stringify(p))),p);
});
test('reject incompatible art before loading it',()=>{
 for(const change of [p=>p.version=3,p=>p.palette[1]='#ffffff',p=>p.entries.reverse(),p=>p.entries[0].width=9,p=>p.entries[0].rows.pop(),p=>p.entries[4].rows[0]='FFFF0000',p=>p.entries[18].rows[0]='222']) {
  const p=fresh(); change(p); assert.throws(()=>M.validateTileset(p));
 }
});
test('rectangular painting, bounds, transform and independent history',()=>{
 const p=fresh(), e=p.entries[12], saved=M.cloneState(p);
 assert.equal(M.setPixel(e,11,8,15),M.getPixel(source.entries[12],11,8)!==15);
 assert.equal(M.getPixel(e,11,8),15); assert.equal(M.setPixel(e,12,8,0),false);
 assert.equal(M.getPixel(e,0,9),-1); M.transform(e,'mirror-h'); assert.equal(M.getPixel(e,0,8),15);
 M.transform(e,'mirror-v'); assert.equal(M.getPixel(e,0,0),15);
 M.restoreState(p,saved); assert.deepEqual(p,source);
 for(const action of ['left','right','up','down','clear']) {M.transform(e,action);M.validateTileset(p);}
 assert.throws(()=>M.setPixel(p.entries[4],0,0,5));
});
test('player recoloring, opaque terrain zero, transparent sprite zero',()=>{
 const p=fresh(),e=p.entries[4];
 assert.deepEqual([0,1,2,3].map(i=>M.palette(p,e,i)[1]),[10,14,3,9].map(i=>p.palette[i]));
 M.setPixel(e,0,0,0); assert.equal(M.colour(p,e,0,0),null);
 M.setPixel(p.entries[0],0,0,0); assert.equal(M.colour(p,p.entries[0],0,0),p.palette[0]);
 assert.equal(M.penCount(p.entries[18]),2); assert.equal(M.penCount(e),5);
});
test('hardware row sizes, zero guards and animation cadence',()=>{
 for(const e of source.entries) {
  const bytes=M.spriteBytes(e);
  if(e.mode==='font') {assert.equal(bytes.length,6);continue;}
  const stride=e.kind==='terrain'?7:e.width/2+2;
  assert.equal(bytes.length,stride*e.height+1);
  assert.equal(bytes.at(-1),0);
  for(let y=0;y<e.height;y++) {assert.equal(bytes[y*stride],stride);assert.equal(bytes[(y+1)*stride-1],0);}
 }
 assert.equal(M.animation(source,source.entries[8]).length,4);
 assert.equal(M.animation(source,source.entries[0]).length,0);
 assert.equal(M.FRAME_MS,1000*8/60);
});

test('4x6 O and zero are independently editable, including spacing pixels',()=>{
 const p=fresh(), o=M.findEntry(p,'glyph_79').entry, zero=M.findEntry(p,'glyph_48').entry;
 const original=o.rows.slice();
 assert.equal(zero.width,4); assert.equal(zero.height,6);
 const pen=M.getPixel(zero,3,5)===0 ? 1 : 0;
 M.setPixel(zero,3,5,pen);
 assert.equal(M.getPixel(zero,3,5),pen); assert.deepEqual(o.rows,original);
 assert.equal(M.spriteBytes(zero)[5],parseInt(zero.rows[5],2));
 M.validateTileset(p);
});
