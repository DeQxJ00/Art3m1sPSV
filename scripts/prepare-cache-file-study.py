"""Prepare all PNGs in matching FG folders, retaining original relative filenames."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path
from PIL import Image


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fg-root',required=True,type=Path)
    p.add_argument('--folder-prefix',required=True)
    p.add_argument('--output',required=True,type=Path)
    a=p.parse_args()
    files=sorted(p for d in a.fg_root.iterdir() if d.is_dir() and d.name.casefold().startswith(a.folder_prefix.casefold())
                 for p in d.rglob('*') if p.is_file() and p.suffix.lower()=='.png')
    if not files or len(files)>512:p.error('Expected 1..512 PNG files')
    a.output.mkdir(parents=True,exist_ok=True)
    sources=[]
    for path in files:
        relative=path.relative_to(a.fg_root).as_posix();sample=relative[:-4]
        if len(sample)>31 or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789_-/' for c in sample):
            p.error('Filename exceeds current external harness format: '+relative)
        with Image.open(path) as im:
            w,h=im.size;mode=im.mode
            if max(w,h)>4096 or w*h*4>16*1024*1024:p.error('Image exceeds harness limit: '+relative)
            im.verify()
        data=path.read_bytes();target=a.output/relative;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(path,target)
        assert hashlib.sha256(target.read_bytes()).digest()==hashlib.sha256(data).digest()
        sources.append(dict(sample=sample,filename=relative,folder=path.relative_to(a.fg_root).parts[0],
                            source=str(path.resolve()),width=w,height=h,mode=mode,png_bytes=len(data),
                            raw_bytes=w*h*4,sha256=hashlib.sha256(data).hexdigest()))
    document=dict(root=str(a.fg_root.resolve()),folder_prefix=a.folder_prefix,samples=sources)
    content=json.dumps(document,ensure_ascii=False,indent=2)
    (a.output/'sources.json').write_text(content,encoding='utf-8')
    (a.output/'cache-study.key').write_text('file-study-v1:'+hashlib.sha256(content.encode()).hexdigest()+'\n')
    (a.output/'cache-study.profile').write_text('all named FG files\n')
    (a.output/'cache-study.files').write_text('PNG decode / select / encode / GPU; 128 KiB; 333 MHz\n')
    (a.output/'cache-study.scene').write_text(str(len(sources))+'\n'+''.join(f'{i} 0 {s["sample"]} {s["width"]} {s["height"]}\n' for i,s in enumerate(sources)))
    (a.output/'title.txt').write_text('Named FG cache study\n')
    (a.output/'system.ini').write_text('[system]\nwidth=960\nheight=544\nboot=first.iet\n')
    (a.output/'first.iet').write_text('[text text="External cache benchmark"]\n')
    print('Files',len(sources),'folders',len({s['folder'] for s in sources}),
          'PNG MiB',sum(s['png_bytes'] for s in sources)/1048576,
          'RGBA MiB',sum(s['raw_bytes'] for s in sources)/1048576)


if __name__=='__main__':main()
