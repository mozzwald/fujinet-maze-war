#!/usr/bin/env python3
"""Check the editable art contract and browser/build encoding agreement."""
import copy
import json
from pathlib import Path
import subprocess
import unittest
import generate_art as art


class ArtTests(unittest.TestCase):
    def setUp(self):
        self.project = json.loads(art.PROJECT.read_text())

    def test_browser_contract_and_encoding(self):
        model = art.ROOT.parents[1] / 'tools/charsetter/maze-model.js'
        script = '''const M=require(process.argv[1]);const p=require(process.argv[2]);
        M.validateTileset(p);console.log(JSON.stringify(p.entries.map(e=>M.spriteBytes(e))));'''
        actual = json.loads(subprocess.check_output(['node','-e',script,str(model),str(art.PROJECT)],text=True))
        images = art.validate(self.project)
        expected = []
        for e in self.project['entries']:
            rows = images[e['id']]
            if e['mode']=='font': expected.append([sum(v<<(2-x) for x,v in enumerate(row)) for row in rows])
            else: expected.append((art.counted if e['kind']=='terrain' else art.literal)(rows))
        self.assertEqual(actual, expected)

    def test_validation(self):
        changes = [lambda p:p.update(version=2),lambda p:p['entries'].reverse(),
                   lambda p:p['entries'][0].update(width=9),lambda p:p['entries'][0]['rows'].pop(),
                   lambda p:p['entries'][4]['rows'].__setitem__(0,'FFFF0000'),
                   lambda p:p['entries'][18]['rows'].__setitem__(0,'222'),
                   lambda p:p['palette'].__setitem__(0,'#ffffff')]
        for change in changes:
            p=copy.deepcopy(self.project);change(p)
            with self.assertRaises(ValueError): art.validate(p)

    def test_pixel_edits_reach_generated_outputs(self):
        baseline=art.outputs(self.project)
        for index, output in [(0,'maze_sprites.h'),(4,'maze_sprites.h'),(8,'maze_sprites.h'),(12,'death_sprites.h'),(16,'maze_sprites.h'),(17,'maze_sprites.h'),(18,'font_art.h')]:
            p=copy.deepcopy(self.project); e=p['entries'][index]
            e['rows'][0]=('1' if e['rows'][0][0]=='0' else '0')+e['rows'][0][1:]
            self.assertNotEqual(art.outputs(p)[output],baseline[output])
            # Artwork edits must not consume any extra runtime storage.
            self.assertEqual(len(art.outputs(p)[output]),len(baseline[output]))


if __name__=='__main__': unittest.main()
