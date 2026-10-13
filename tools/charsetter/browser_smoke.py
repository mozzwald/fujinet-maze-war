#!/usr/bin/env python3
"""Exercise the real static editor in Chromium, offline. Artifacts go to /tmp."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parent
SCRIPT=r'''
(async function () {
    const result=document.createElement('pre'); result.id='smoke-result'; document.body.appendChild(result);
    const check=(ok,why)=>{if(!ok)throw Error(why);};
    const el=id=>document.getElementById(id);
    const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
    function choose(id) {
        const button=[...document.querySelectorAll('.tile-item')].find(b=>b.dataset.imageId===id);
        check(button,'missing library image '+id); button.click();
    }
    function mouse(type,x,y,button=0) {
        const canvas=el('editor-canvas'), r=canvas.getBoundingClientRect();
        canvas.dispatchEvent(new MouseEvent(type,{bubbles:true,button,clientX:r.left+x,clientY:r.top+y}));
    }
    try {
        const errors=[]; window.addEventListener('error',e=>errors.push(e.message));
        window.alert=message=>{throw Error(message);};
        const input=el('tileset-input'), transfer=new DataTransfer();
        transfer.items.add(new File([JSON.stringify(PROJECT)],'maze-war-tiles.json',{type:'application/json'}));
        input.files=transfer.files; input.dispatchEvent(new Event('change'));
        await wait(200);
        check(document.querySelectorAll('.tile-item').length===57,'57 named library entries');
        choose('explosion_1');
        check(el('editor-canvas').width===480 && el('editor-canvas').height===360,'12×9 explosion canvas');
        check(document.querySelectorAll('.swatch').length===16,'explosion palette');
        const labels=new Set();
        for(let i=0;i<12;i++) {labels.add(el('animation-label').textContent);await wait(70);}
        check(labels.size===4,'all four animation frames play');
        el('animation-play').click();
        // Choose a different pen than the current first pixel, paint and undo.
        const pen=parseInt(PROJECT.entries[12].rows[0][0],16)===15?1:15;
        document.querySelectorAll('.swatch')[pen].click();
        mouse('mousedown',20,20); window.dispatchEvent(new MouseEvent('mouseup'));
        check(!el('undo').disabled && !el('dirty-indicator').hidden,'painting records history');
        el('undo').click(); check(!el('redo').disabled,'undo'); el('redo').click();
        // Test independent rectangular transforms.
        document.querySelector('[data-transform="mirror-h"]').click(); el('undo').click();
        choose('player_3');check(document.querySelectorAll('.swatch').length===5,'player role palette');
        check(el('editor-canvas').width===320 && el('editor-canvas').height===200,'8×5 player canvas');
        el('player-preview').value='1'; el('player-preview').dispatchEvent(new Event('change'));
        const swatch=document.querySelectorAll('.swatch')[1];
        const colour=document.createElement('span');colour.style.background=PROJECT.palette[14];
        check(swatch.style.background===colour.style.background,'P2 robe is blue');
        choose('glyph_65');check(document.querySelectorAll('.swatch').length===2,'font pen restriction');
        check(el('editor-canvas').width===160 && el('editor-canvas').height===240,'4×6 glyph canvas');
        choose('muzzle');check(el('editor-canvas').width===80 && el('editor-canvas').height===40,'2×1 effect canvas');
        let saved;
        URL.createObjectURL=blob=>{saved=blob;return 'blob:test';}; URL.revokeObjectURL=()=>{};
        HTMLAnchorElement.prototype.click=function(){};
        el('save-tileset').click(); check(saved instanceof Blob,'save creates JSON download');
        const exported=JSON.parse(await saved.text()); MazeWarLynx.validateTileset(exported);
        check(parseInt(exported.entries[12].rows[0][0],16)===pen,'saved explosion edit');
        check(JSON.stringify(exported.entries[13])===JSON.stringify(PROJECT.entries[13]),'other animation frames unchanged');
        check(el('dirty-indicator').hidden,'save clears modified indicator');
        check(errors.length===0,errors.join('; '));
        choose('explosion_1');
        result.textContent='PASS: load, 57 images, rectangular painting, undo/redo, transforms, recoloring, animation and save round trip';
    } catch(error) {result.textContent='FAIL: '+error.stack;}
})();
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--chromium',default=shutil.which('chromium'))
    args=parser.parse_args()
    output=Path(tempfile.mkdtemp(prefix='maze-charsetter-'))
    html=(ROOT/'index.html').read_text().replace('<head>',f'<head><base href="{ROOT.as_uri()}/">')
    project=json.loads((ROOT.parents[1]/'clients/lynx/art/maze-war-tiles.json').read_text())
    html=html.replace('</body>','<script>const PROJECT='+json.dumps(project).replace('<',r'\u003c')+';'+SCRIPT+'</script></body>')
    test=output/'test.html';test.write_text(html)
    cmd=[args.chromium,'--headless','--no-sandbox','--disable-gpu','--allow-file-access-from-files',
         '--no-first-run','--disable-background-networking',f'--user-data-dir={output}/profile',
         '--window-size=1400,1100','--virtual-time-budget=4000','--dump-dom',
         f'--screenshot={output}/editor.png',test.as_uri()]
    run=subprocess.run(cmd,capture_output=True,text=True,timeout=40)
    (output/'dom.html').write_text(run.stdout);(output/'browser.log').write_text(run.stderr)
    import re
    match=re.search(r'<pre id="smoke-result">(.*?)</pre>',run.stdout,re.S)
    message=match[1] if match else 'FAIL: editor did not run'
    print(message);print('Artifacts:',output)
    if run.returncode or not message.startswith('PASS:'):raise SystemExit(1)


if __name__=='__main__':main()
