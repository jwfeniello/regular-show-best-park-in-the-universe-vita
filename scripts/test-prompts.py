"""Check production prompt rewriting with sanitizers and optional local game data."""
from pathlib import Path
import subprocess
import tempfile
import os
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='park-prompts-') as folder:
    work = Path(folder)
    driver = work / 'driver.c'
    driver.write_text(r'''
#include "prompts.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    assert(argc==4);
    assert(!park_prompt_sprite("ordinary_game_art.png"));
    assert(!strcmp(park_prompt_sprite("vita_square.png"),"app0:prompts/square.png"));
    FILE *f=fopen(argv[2],"rb"); assert(f);
    fseek(f,0,SEEK_END); long n=ftell(f); rewind(f); assert(n>=0);
    unsigned char *s=malloc(n+1); assert(s);
    assert(fread(s,1,n,f)==(size_t)n); fclose(f);
    size_t size=n;
    unsigned char *out=park_prompts_rewrite(argv[1],s,n,&size);
    f=fopen(argv[3],"wb"); assert(f);
    assert(fwrite(out?out:s,1,size,f)==size); fclose(f);
    free(out); free(s); return 0;
}
''')
    binary = work / 'prompts'
    subprocess.run(['gcc','-Wall','-Wextra','-Werror','-g','-O1',
                    '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                    '-I'+str(ROOT/'source'),str(driver),str(ROOT/'source/prompts.c'),
                    '-o',str(binary)],check=True)
    def rewrite(path, data):
        (work/'input').write_bytes(data)
        subprocess.run([str(binary),path,str(work/'input'),str(work/'output')],check=True,
                       env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
        return (work/'output').read_bytes()

    data = b'instruction1:Tap a spot to walk directly there.\r\nmission:Swipe the key!\r\nrigby_combo2x1_description:Swipe Down during a Regular Combo'
    result = rewrite('assets/locale/english.txt',data)
    assert b'left stick or D-pad' in result and 'Press \u00d7'.encode() in result
    assert b'mission:Swipe the key!\r\n' in result
    assert rewrite('locale/french.txt',data)==data
    assert rewrite('not-locale/english.txt',data)==data
    assert rewrite('locale/english.txt',b'bad\0data')==b'bad\0data'
    assert rewrite('locale/english.txt',b'')==b''
    xml=b'<Animations><Animation name="notification_loop" frameCount="21"><Part name="original"/></Animation><Animation frameCount="37" name="double_tap"><Part name="hand"/></Animation></Animations>'
    changed=ET.fromstring(rewrite('hd/gui/animations.xml',xml))
    assert changed[0][0].get('name')=='original'
    assert changed[1][0].get('name')=='vita_r'
    assert changed[1].get('frameCount')=='37'
    assert [int(f.get('index')) for f in changed[1][0]]==list(range(37))
    assert all(f.get('alpha')=='1' and f.get('scaleX')=='1' and f.get('scaleY')=='1' for f in changed[1][0])
    assert rewrite('hd/mordecai/animations.xml',xml)==xml
    malformed=b'<Animations><Animation name="double_tap"'
    assert rewrite('gui/animations.xml',malformed)==malformed

    apk=ROOT/'data/game.apk'
    if apk.exists():
        with zipfile.ZipFile(apk) as z:original=z.read('assets/locale/english.txt')
        updated=rewrite('assets/locale/english.txt',original)
        def rows(data):
            return [line.split(':',1) if ':' in line else [line] for line in data.decode('utf-8-sig').splitlines()]
        before,after=rows(original),rows(updated)
        assert len(before)==len(after)
        count=0
        for a,b in zip(before,after):
            assert a[0]==b[0]
            if a!=b:
                count+=1
                assert a[0].startswith(('instruction','tutorial','mordecai_','rigby_','muscleman_','pops_'))
                assert not any(x in b[1].lower() for x in ('swipe','swiping','tap with','tapping'))
                assert not any(x in b[1] for x in ('SQUARE','TRIANGLE','CROSS','CIRCLE','Press R','Press L'))
        assert count==41,count
        translated={r[0]:r[1] for r in after if len(r)==2}
        assert 'Press \u25b3 during a Regular Combo' == translated['rigby_knockdown_description']
        assert '\ue001' in translated['instruction4']
        assert '\ue000' in translated['instruction3']
        print('Checked all 41 help/tutorial/skill text replacements against Android 1.2.1.')
    obb=ROOT/'data/main.16.com.turner.bestparkintheuniverse.obb'
    if obb.exists():
        with zipfile.ZipFile(obb) as z:
            for tier in ('sd','hd','ipad3'):
                name=tier+'/gui/animations.xml'; original=z.read(name)
                before,after=ET.fromstring(original),ET.fromstring(rewrite(name,original))
                assert len(before)==len(after)==14
                for a,b in zip(before,after):
                    assert a.attrib==b.attrib
                    if a.get('name').startswith('notification_'):assert ET.tostring(a)==ET.tostring(b)
                    else:
                        assert len(b)==1 and b[0].get('name').startswith('vita_')
                        frames=list(b[0])
                        assert [int(f.get('index')) for f in frames]==list(range(int(b.get('frameCount'))))
                        states=[{k:v for k,v in f.attrib.items() if k!='index'} for f in frames]
                        assert all(s==states[0] and s['alpha']=='1' and s['scaleX']=='1' and s['scaleY']=='1' for s in states)
                        assert (ROOT/'extras/prompts'/(b[0].get('name')[5:]+'.png')).exists()
        print('Checked all 11 gesture animations in all three resource tiers; notifications unchanged.')
    print('Prompt rewrite checks passed under ASan/UBSan.')
