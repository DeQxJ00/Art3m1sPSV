"""Extract a bounded, reproducible local acceptance fixture; never run game EXEs.

Output is exclusively beneath temp/native-command-port. Original fonts are
subset using WSL fontTools; game images retain their original pixels/alpha.
"""
from pathlib import Path
import hashlib, json, re, struct, subprocess, zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'temp/native-command-port'
SOURCES = {
    'toshiue': (Path('F:/WorkSpaceAI2/art3m1s_test_rom/年上彼女2'),
                ['font/notosansjp-bold.ttf', 'pc/ja/mw/bt_blog.png']),
    'otomeriron': (Path('F:/HikariFieldGames/Otomeriron'),
                  ['font/sourcehansans-bold.otf', 'pc/cn/mw/bt_blog.png']),
}

def index(source):
    result = {}
    for archive in sorted(source.iterdir()):
        if not re.search(r'\.pfs(?:\.\d{3})?$', archive.name): continue
        with archive.open('rb') as f:
            header = f.read(11)
            assert header[:3] in (b'pf8', b'pf6')
            size, count = struct.unpack_from('<II', header, 3)
            assert 0 < size < 16*1024*1024
            f.seek(0); raw = f.read(size+7)
        key = hashlib.sha1(raw[7:]).digest() if header[:3] == b'pf8' else b''
        pos = 11
        for _ in range(count):
            n, = struct.unpack_from('<I', raw, pos); pos += 4
            name = raw[pos:pos+n]; pos += n+4
            try: name = name.decode('utf-8')
            except UnicodeDecodeError: name = name.decode('cp932')
            offset, length = struct.unpack_from('<II', raw, pos); pos += 8
            result[name.rstrip('\0').replace('\\','/').lower()] = (archive, offset, length, key)
    return result

def extract(entries, name, dest):
    archive, offset, length, key = entries[name]
    assert length < 30*1024*1024
    with archive.open('rb') as f: f.seek(offset); data = f.read(length)
    assert len(data) == length
    if key: data = bytes(c ^ key[i % len(key)] for i, c in enumerate(data))
    dest.parent.mkdir(parents=True, exist_ok=True); dest.write_bytes(data)
    return dict(path=name, archive=str(archive), offset=offset, size=length,
                sha256=hashlib.sha256(data).hexdigest())

def wsl(path):
    path = path.resolve().as_posix()
    return '/mnt/'+path[0].lower()+path[2:]

def scenario(game, font):
    # Deliberately synthetic dialogue, no original plot or executable.
    s = ['*top', '[lua]\nfunction getGameMode() return "adv" end\n[/lua]', '[debug mode="1" level="3"]', '[fontdefault face="'+font+'" size="28" color="ffffff" show="none" spacetop="0" spacemiddle="4" spacebottom="0"]',
         '[lyc id="0" width="960" height="544" color="182334"]',
         '[backlog allow="1" includefont="1"]', '[writebacklog mode="1"]', '[wordparts parts=""]', '[prohibit head="" foot=""]']
    def text(layer, x, y, width, content, extra=''):
        s.extend([f'[chgmsg id="{layer}" stack="0"]', f'[fontinit]',
                  f'[font left="{x}" top="{y}" width="{width}" height="380"]', '[rp backlog="0"]', '[indentmodify unindent="-1"]'])
        if extra: s.append(extra)
        s.append(f'[print data="{content}"]')
    def heading(n, title):
        s.append(f'[debugprint data="NATIVE-CMD {game} CASE {n}"]')
        text('heading',24,18,920,f'{game.upper()}  {n}/6  {title}')
        text('footer',24,470,920,'CIRCLE / TOUCH: NEXT     SQUARE: HOST MENU')
    def pause():
        case=sum('NATIVE-CMD '+game+' CASE ' in row for row in s)
        s.extend(['[trans time="0"]', '[wait time="300"]', '[takess]',
                  f'[savess file="cmd-case-{case}.png" width="960" height="544"]', '[@]'])
    heading(1,'RANGE 0 / NEST')
    text('body',40,120,600,'「甲乙『丙丁', '[indent pair="「」『』" range="0" nest="1" logicalrange="1"]')
    s += ['[rt]', '[print data="双重缩进：此行应向右"]','[indentmodify unindent="1"]','[rt]','[print data="弹出一层：此行向左"]','[indentmodify unindent="-1"]','[rt]','[print data="清空：此行回到左边界"]']
    pause()
    heading(2,'RP / NAME INDEPENDENCE')
    text('body',40,120,600,'「翻页前保留一层', '[indent pair="「」" range="0" nest="1" logicalrange="1"]')
    s += ['[rp backlog="1"]','[print data="翻页后：此行仍应缩进"]','[rt]']
    text('name',650,120,280,'「姓名独立', '[indent pair="「」" range="0" nest="1"]')
    s += ['[indentmodify unindent="-1"]','[chgmsg id="body" stack="0"]','[print data="正文仍缩进"]','[indentmodify unindent="-2"]','[rt]','[print data="负数清空：回到左边界"]']
    pause()
    s += ['[lydel id="name"]']
    heading(3,'LOGICAL RANGE / WRAP')
    # Original fonts have an advance below their requested pixel height. Force
    # the bracket past an auto-wrap so finite physical/logical ranges diverge.
    text('body',40,120,80,'甲甲甲甲「乙乙乙乙乙乙', '[indent pair="「」" range="3" nest="1" logicalrange="0"]')
    text('logical',340,120,80,'甲甲甲甲「乙乙乙乙乙乙', '[indent pair="「」" range="3" nest="1" logicalrange="1"]')
    text('explain',520,120,410,'LEFT: physical range[rt]RIGHT: logical range'.replace('[rt]',' / '))
    pause()
    s += ['[lydel id="logical"]','[lydel id="explain"]']
    heading(4,'ANIMEDEL / ORIGINAL PNG')
    text('body',40,120,850,'原游戏 Log 按钮会移动；按下一页后应删除。')
    s += ['[lyc id="button" file="assets/bt_blog.png"]','[lyprop id="button" left="100" top="340"]',
          '[lytween id="button" param="left" from="100" to="700" time="3000" loop="-1" yoyo="1"]']
    pause()
    s += ['[animedel id="button"]','[appreview]', '*link_check']
    heading(5,'LINKRESET / APPREVIEW')
    text('body',40,120,850,'移动按钮应已消失。appreview 已继续执行。')
    s += ['[rt]', '[link file="system/first.iet" label="link_ok"]','[print data="TOUCH THIS LINK TO FINISH"]','[/link]', '[linkdisable]', '[linkreset]']
    pause()
    # A generic advance must not masquerade as a successful restored link.
    s += ['[jump file="system/first.iet" label="link_check"]', '*link_ok']
    heading(6,'COMPLETE')
    text('body',40,120,850,'验收结束。重启本测试可重复检查。')
    s += ['[debugprint data="NATIVE-CMD '+game+' COMPLETE"]','[trans time="0"]',
          '[wait time="300"]','[takess]','[savess file="cmd-case-6.png" width="960" height="544"]','[stop]']
    script='\n'.join(s)+'\n'
    if game=='toshiue':
        # Its Japanese font does not cover these Simplified Chinese characters.
        # Retain the original font and use covered Traditional forms in labels.
        script=script.translate(str.maketrans('删动层应弹戏执检测试结继续缩负边进钮验查','刪動層應彈戲執檢測試結繼續縮負邊進鈕驗査'))
    return script

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    manifests=[]
    for game, (source, wanted) in SOURCES.items():
        entries=index(source)
        game_id='TEST_'+game.upper()+'_CMD'
        dest=OUT/'games'/game_id
        dest.mkdir(parents=True, exist_ok=True)
        font='assets/probe'+Path(wanted[0]).suffix
        script=scenario(game,font)
        (dest/'system').mkdir(exist_ok=True)
        (dest/'system/first.iet').write_text(script,encoding='utf-8')
        (dest/'system.ini').write_text('[WINDOWS]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=system/first.iet\nSAVEPATH=savedata\n[VITA]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=system/first.iet\nSAVEPATH=savedata\n',encoding='utf-8')
        (dest/'title.txt').write_text('Native command test - '+game+'\n',encoding='utf-8')
        (dest/'platform.txt').write_text('psvita\n',encoding='utf-8')
        original=OUT/'originals'/game/Path(wanted[0]).name
        rows=[extract(entries,wanted[0],original), extract(entries,wanted[1],dest/'assets/bt_blog.png')]
        chars=OUT/'originals'/game/'characters.txt'; chars.write_text(script,encoding='utf-8')
        subprocess.run(['wsl','-d','Ubuntu-24.04','--','python3','-m','fontTools.subset',wsl(original),
                        '--text-file='+wsl(chars),'--output-file='+wsl(dest/font)],check=True)
        # Only relevant source lines, retained for review and not executed.
        excerpts=[]
        for name in ('system/table/list_windows_ja.tbl','system/msg/message.lua'):
            if name not in entries: continue
            temp=OUT/'originals'/game/Path(name).name
            row=extract(entries,name,temp)
            lines=temp.read_text(encoding='utf-8-sig').splitlines()
            matches=[(i+1,line) for i,line in enumerate(lines) if 'logicalrange' in line or 'indentmodify' in line]
            excerpts.append(dict(source=row,lines=matches))
        manifest=dict(game=game,id=game_id,resources=rows,reference=excerpts,
            files=[dict(path=p.relative_to(dest).as_posix(),size=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in sorted(dest.rglob('*')) if p.is_file() and p.name!='provenance.json'])
        (dest/'provenance.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
        manifests.append(manifest)
        print(game, 'files',len(manifest['files']), 'bytes',sum(r['size'] for r in manifest['files']))
    (OUT/'resources.json').write_text(json.dumps(manifests,ensure_ascii=False,indent=2),encoding='utf-8')
    with zipfile.ZipFile(OUT/'native-command-test-resources.zip','w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted((OUT/'games').rglob('*')):
            if p.is_file(): z.write(p,'ux0/data/art3m1s-gxm/'+p.relative_to(OUT).as_posix())

if __name__=='__main__': main()
