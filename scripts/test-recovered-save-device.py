"""Repeat the real saved game scene without manual menu/load navigation.
Requires a host with recovered-save-bench support. Never rewrites the source save.
"""
from pathlib import Path
import argparse,sys,io,json,time,uuid,re,hashlib
from ftplib import error_perm
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root.parent/'emote-psv-test/tools'))
import hardware_demo as h

def digest(data):return hashlib.sha256(data).hexdigest()
def put(f,path,data):
 f.storbinary('STOR '+path,io.BytesIO(data),blocksize=65536)
 assert h.read(f,path)==data,path

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--game',default='sample01');p.add_argument('--save',default='save0001.dat',help='UI slot2 is save0001.dat in this game')
 p.add_argument('--seconds',type=int,default=40);p.add_argument('--warmup',type=int,default=20);p.add_argument('--run',default=time.strftime('%Y%m%d-%H%M%S'))
 a=p.parse_args()
 assert re.fullmatch(r'[A-Za-z0-9_-]+',a.game) and re.fullmatch(r'[A-Za-z0-9_.-]+',a.save) and a.save not in ('.','..')
 assert 30<=a.seconds<=600 and 10<=a.warmup<=120
 out=root/'build/recovered-save-bench'/a.run;out.mkdir(parents=True,exist_ok=False)
 base='ux0:/data/art3m1s-gxm/';token=uuid.uuid4().hex
 prefs={'recovered-save-bench.txt':f'{a.game} {a.save} {token}\n'.encode(),'last-game.txt':(a.game+'\n').encode(),'cpu-clock.conf':b'5 444 444 222 222 444 222 444 222\n','trace-nextline.flag':b'1\n','trace-nextline.off':b'1\n','font-settings/'+a.game.encode().hex()+'.emote-backend':b'1 2\n'}
 saved={};savepath=base+'saves/'+a.game+'/save/'+a.save
 report={'status':'running','token':token,'game':a.game,'save':a.save,'requested_clocks':[444,222],'warmup_s':a.warmup,'sample_s':a.seconds}
 with h.ftp() as f:
  source=h.read(f,savepath);report['source_save_sha256']=digest(source)
  report['eboot_sha256']=digest(h.read(f,'ux0:/app/ART3DIR01/eboot.bin'))
  for name in prefs:
   try:saved[name]=h.read(f,base+name)
   except error_perm as e:
    if not str(e).startswith('550'):raise
    saved[name]=None
 (out/'preferences-backup.json').write_text(json.dumps({k:None if v is None else v.hex() for k,v in saved.items()},indent=2))
 try:
  h.command('kill ART3DIR01')
  with h.ftp() as f:
   for n,d in prefs.items():put(f,base+n,d)
  h.command('launch ART3DIR01');time.sleep(7);h.command('press circle; wait 100ms; release circle')
  deadline=time.monotonic()+180+a.warmup+a.seconds;loaded=None;last=None;windows=[]
  while time.monotonic()<deadline:
   time.sleep(5)
   with h.ftp() as f:data=h.read(f,base+'host.log')
   (out/'host.log').write_bytes(data);lines=data.decode('utf-8',errors='replace').splitlines()
   marks=[x for x in lines if '[save-bench]' in x and 'token='+token in x]
   if not marks:continue
   if 'load_result=1 ' not in marks[-1]:raise RuntimeError(marks[-1])
   loaded=int(re.search(r'at_us=(\d+)',marks[-1])[1]);cut=loaded+a.warmup*1000000
   rows=[{k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)} for line in lines if '[frame-perf]' in line]
   windows=[]
   for before,after in zip(rows,rows[1:]):
    if before['at_us']>=cut and after['at_us']>before['at_us']:
     windows.append(dict(after,duration_us=after['at_us']-before['at_us']))
   if windows and windows[-1]['at_us']!=last:
    last=windows[-1]['at_us'];print('sample seconds',round(sum(x['duration_us'] for x in windows)/1e6,1),flush=True)
   if sum(x['duration_us'] for x in windows)>=a.seconds*1000000:break
  else:raise RuntimeError('No completed scene sample; see host.log')
  tail='\n'.join(x for x in lines[lines.index(marks[-1]):])
  if any(x in tail for x in ['recovered packet replay failed','[game-load-failed]','[core:E]']):raise RuntimeError('Scene load/render errors; see log')
  poses=[{k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)} for line in lines if '[recovered-pose]' in line][-len(windows):]
  assert poses and all(x['frames']>0 and x['sprites']>0 for x in poses),'Missing animated character capture'
  with h.ftp() as f:assert h.read(f,savepath)==source,'Source save changed'
  report.update(status='pass_performance_sample_visual_check_separate',windows=windows,mean_fps=sum(x['frames'] for x in windows)*1e6/sum(x['duration_us'] for x in windows),pose_windows=poses,source_save_unchanged=True)
 except Exception as e:
  report.update(status='failed',error=str(e));raise
 finally:
  try:
   with h.ftp() as f:
    for n,d in saved.items():
     if d is None:
      try:f.delete(base+n)
      except error_perm as e:
       if not str(e).startswith('550'):raise
     else:put(f,base+n,d)
   report['preferences_restored']=True
  finally:(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({'status':report['status'],'mean_fps':report['mean_fps'],'report':str(out/'result.json')}),flush=True)
if __name__=='__main__':main()
