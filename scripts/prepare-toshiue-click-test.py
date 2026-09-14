"""Bounded input fixture matching Toshiue's queued clickGlyphwait protocol.

All generated/extracted assets stay in a separate build test-game directory.
Subsets an already extracted CJK font; audio is a generated quiet tone.
"""
from pathlib import Path
import math, shutil, struct, subprocess, wave

root = Path(__file__).resolve().parents[1]
out = root / 'build/toshiue-click-test/game'
(out / 'system').mkdir(parents=True, exist_ok=True)
(out / 'assets').mkdir(exist_ok=True)
source = root / 'build/native-command-port/games/TEST_TOSHIUE_CMD'
for name in ['system.ini', 'platform.txt']:
    shutil.copyfile(source / name, out / name)
(out / 'title.txt').write_text('TEST TOSHIUE CLICK\n', encoding='utf-8')
with wave.open(str(out / 'assets/tone.wav'), 'wb') as w:
    w.setparams((1, 2, 48000, 0, 'NONE', 'not compressed'))
    w.writeframes(b''.join(struct.pack('<h', int(600 * math.sin(2*math.pi*440*i/48000))) for i in range(48000*3)))
lines = ['*top', '[debug mode=1 level=3]', '[lua]',
    'function getGameMode() return "adv" end',
    'function decide(e,p) e:overrideKey{key=124,status=32} end',
    'function glyphwait(e,p) e:enqueueTag{"wait",scenario=1,input=1} end',
    'function stamp(e,p) e:tag{"debugprint",data="CLICK-PROBE "..p.phase.." time="..e:now()} end',
    '[/lua]', '[keyconfig role=0 keys=124]',
    '[setonpush key=13 handler=calllua function=decide]',
    '[fontdefault face="assets/probe.otf" size=28 color=ffffff show=none]',
    '[lyc id=0 width=960 height=544 color=182334]',
    '*tests', '[chgmsg id=body stack=0]', '[font left=30 top=130 width=900 height=350]',
    '[rp backlog=0]', '[print data="按圆圈开始长句测试。文字出现时按一次圆圈，应立即补全整段；再按一次才换页。"]', '[@]']
paragraphs = [
    '傍晚的风从敞开的窗边吹进教室，桌上的书页轻轻翻动。我停下手中的笔，望向走廊尽头逐渐远去的身影，忽然想起今天还有一句话没有说出口。明明只是再普通不过的一天，此刻却有许多细小的事情留在心里：窗外的树影、远处的脚步声，还有刚才那个短暂的微笑。现在请在文字尚未显示完时按一次圆圈，确认后面的文字是否全部出现，并且画面仍然停留在这一页，而不是直接进入下一段。这是无音频测试长句的最后一句。',
    '第二页会播放一段轻声测试音，文字依然按照相同的速度逐字出现。我们沿着操场旁的小路慢慢向前走，谈论着明天的天气和还没有完成的约定。她似乎想起了什么，转过身来望着我，却又笑着把话题轻轻带开。我没有催促，只是陪她站在树荫下，等那阵风吹过。请在这段文字显示到中途时再按一次圆圈，检查整段能否立即补全；然后稍等片刻，确认它不会自行翻页。最后再按一次圆圈，才应进入测试结束画面。'
]
for page in (1, 2):
    lines += ['[chgmsg id=heading stack=0]', '[font left=30 top=90 width=900 height=50]',
        '[rp backlog=0]', f'[print data="长句测试 {page}/2：'+('无音频' if page==1 else '有音频（测试音）')+'"]',
        '[chgmsg id=body stack=0]', '[rp backlog=0]', '[scetween mode=init type=in]',
        '[scetween mode=add type=in param=alpha ease=none time=50 delay=100 diff=-255]']
    if page == 2:
        lines += ['[voice id=probe_voice file="assets/tone.wav" loop=0 gain=1000]']
    lines += [f'[print data="{paragraphs[page-1]}"]',
        f'[calllua function=stamp phase="{page}-waiting"]',
        '[calllua function=glyphwait]',
        f'[calllua function=stamp phase="{page}-revealed"]',
        '[trans time=0]', '[takess]',
        f'[savess file="revealed-{page}.png" width=960 height=544]', '[@]',
        f'[calllua function=stamp phase="{page}-advanced"]']
lines += ['[scetween mode=init type=in]', '[rp backlog=0]',
    '[print data="测试结束。按圆圈可以从第一页重新测试。"]',
    '[calllua function=stamp phase="done"]', '[@]', '[jump label=tests]']
script = '\n'.join(lines)+'\n'
(out / 'system/first.iet').write_text(script, encoding='utf-8')
chars = out.parent / 'characters.txt'
chars.write_text(script, encoding='utf-8')
def wsl(p):
    p=p.resolve().as_posix()
    return '/mnt/'+p[0].lower()+p[2:]
subprocess.run(['wsl','-d','Ubuntu-24.04','--','python3','-m','fontTools.subset',
    wsl(root/'build/native-command-port/originals/otomeriron/sourcehansans-bold.otf'),
    '--text-file='+wsl(chars), '--output-file='+wsl(out/'assets/probe.otf')], check=True)
print('Characters per page:', [len(p) for p in paragraphs])
print(out)
