/* Maze War adaptation of FujiRealm's Lynx Charsetter model. */
(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    root.MazeWarLynx = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';
    const HEX = '0123456789ABCDEF';
    const PLAYER_COLOURS = [10, 14, 3, 9];
    const PALETTE = ["#332222", "#110011", "#dd3344", "#ff99aa", "#8899bb", "#777777", "#553333", "#884444", "#ee7755", "#ffdd55", "#33cc55", "#228822", "#444488", "#5588ee", "#55ccff", "#ffffff"];
    // DEFPALETTE uses GBR nibble order, not RGB.
    const specs = [];
    function spec(id, width, height, mode, kind) { specs.push({id, width, height, mode, kind}); }
    ['floor_tile','wall_even','wall_odd','border_tile'].forEach(id => spec(id,8,5,'palette','terrain'));
    for(let i=0;i<4;i++) spec(`player_${i}`,8,5,'player','player');
    for(let i=1;i<=4;i++) spec(`dance_${i}`,8,5,'player','dance');
    for(let i=1;i<=4;i++) spec(`explosion_${i}`,12,9,'palette','explosion');
    spec('projectile',2,2,'palette','effect'); spec('muzzle',2,1,'palette','effect');
    for(const c of 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-./') spec(`glyph_${c.charCodeAt(0)}`,4,6,'font','font');
    function validateTileset(project) {
        if(!project || project.projectType !== 'maze-war-lynx-tiles' || project.version !== 2) throw Error('Expected Maze War Lynx tiles version 2');
        if(JSON.stringify(project.palette) !== JSON.stringify(PALETTE)) throw Error('Palette must match the game palette');
        if(!Array.isArray(project.entries) || project.entries.length !== specs.length) throw Error(`Expected ${specs.length} images`);
        project.entries.forEach((e,i) => {
            const s=specs[i];
            if(!e || Object.keys(s).some(k=>e[k] !== s[k])) throw Error(`${s.id}: binding, size and mode are fixed`);
            if(typeof e.name !== 'string' || !e.name.trim()) throw Error(`${s.id}: missing name`);
            const allowed=e.mode==='player' ? /^[0-4]+$/ : e.mode==='font' ? /^[01]+$/ : /^[0-9a-f]+$/i;
            if(!Array.isArray(e.rows) || e.rows.length!==e.height || e.rows.some(r=>typeof r!=='string'||r.length!==e.width||!allowed.test(r))) throw Error(`${s.id}: invalid pixel rows`);
        });
        return project;
    }
    function entries(project) { return project.entries.map(entry=>({key:entry.id,kind:entry.kind,label:entry.name,detail:`${entry.width}×${entry.height} · ${entry.id}`,used:true,entry})); }
    function findEntry(project,key) { return entries(project).find(e=>e.key===key)||null; }
    function getPixel(e,x,y) { return x<0||y<0||x>=e.width||y>=e.height ? -1 : parseInt(e.rows[y][x],16); }
    function penCount(e) { return e.mode==='player'?5:e.mode==='font'?2:16; }
    function setPixel(e,x,y,v) {
        if(getPixel(e,x,y)<0) return false;
        if(!Number.isInteger(v)||v<0||v>=penCount(e)) throw Error('Invalid pen for image');
        if(getPixel(e,x,y)===v) return false;
        e.rows[y]=e.rows[y].slice(0,x)+HEX[v]+e.rows[y].slice(x+1); return true;
    }
    function transform(e,action) {
        let rows=e.rows.map(r=>r.split(''));
        if(action==='left') rows=rows.map(r=>r.slice(1).concat(r[0]));
        else if(action==='right') rows=rows.map(r=>[r.at(-1)].concat(r.slice(0,-1)));
        else if(action==='up') rows=rows.slice(1).concat([rows[0]]);
        else if(action==='down') rows=[rows.at(-1)].concat(rows.slice(0,-1));
        else if(action==='mirror-h') rows=rows.map(r=>r.reverse());
        else if(action==='mirror-v') rows.reverse();
        else if(action==='clear') rows=rows.map(r=>r.map(()=> '0'));
        else throw Error('Unknown transform');
        e.rows=rows.map(r=>r.join('')); return e.rows;
    }
    function cloneState(project) { return project.entries.map(e=>({key:e.id,rows:e.rows.slice()})); }
    function restoreState(project,state) { state.forEach(s=>{const e=findEntry(project,s.key);if(e)e.entry.rows=s.rows.slice();}); }
    function spriteBytes(e) {
        if(e.mode==='font') return e.rows.map(r=>parseInt(r,2));
        const bytes=[];
        e.rows.forEach(r=>{
            if(e.kind==='terrain') {
                let bits='10111'+[...r].map(c=>parseInt(c,16).toString(2).padStart(4,'0')).join('');
                bits=bits.padEnd(40,'0'); bytes.push(7);
                for(let i=0;i<40;i+=8) bytes.push(parseInt(bits.slice(i,i+8),2));
            } else {
                bytes.push(e.width/2+2);
                for(let x=0;x<e.width;x+=2) bytes.push(parseInt(r.slice(x,x+2),16));
            }
            bytes.push(0);
        }); bytes.push(0); return bytes;
    }
    function palette(project,e,player=0) {
        if(e?.mode==='player') return [null,project.palette[PLAYER_COLOURS[player]],project.palette[8],project.palette[15],project.palette[6]];
        if(e?.mode==='font') return [null,project.palette[PLAYER_COLOURS[player]]];
        return project.palette;
    }
    function colour(project,e,x,y,player=0) {
        const pen=getPixel(e,x,y);
        return pen===0 && e.kind!=='terrain' ? null : palette(project,e,player)[pen];
    }
    function animation(project,e) {
        return e && ['dance','explosion'].includes(e.kind) ? project.entries.filter(f=>f.kind===e.kind) : [];
    }
    return {validateTileset,entries,findEntry,getPixel,setPixel,penCount,transform,cloneState,restoreState,spriteBytes,palette,colour,animation,specs,PALETTE,FRAME_MS:1000*8/60};
});
