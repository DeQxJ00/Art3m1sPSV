"""Verify downloaded production-host probe RGBA captures; never accesses a device."""
from pathlib import Path
import argparse,json,hashlib
import numpy as np
p=argparse.ArgumentParser();p.add_argument('directory',type=Path);a=p.parse_args();root=a.directory
expanded=(root/'stage-20.rgba').exists()
frames=[np.frombuffer((root/f'stage-{i:02}.rgba').read_bytes(),np.uint8).reshape(544,960,4) for i in range(29 if (root/'stage-28.rgba').exists() else 27 if (root/'stage-26.rgba').exists() else 21 if expanded else 13)]
results=[];transparent=[]
for base in [0,3,6,9]:
 for offset in [1,2]:
  diff=np.any(frames[base]!=frames[base+offset],axis=2);item={'test':'parent_group_exact','reference':base,'stage':base+offset,'different_pixels':int(diff.sum())}
  if expanded and base in [3,9]:
   item.update(status='not_exact' if diff.any() else 'exact',max_rgb_delta=int(abs(frames[base][:,:,:3].astype(int)-frames[base+offset][:,:,:3]).max()),alpha_different_pixels=int(np.any(frames[base][:,:,3:]!=frames[base+offset][:,:,3:],axis=2).sum()));transparent.append(item)
  else:results.append(item)
y,x=np.mgrid[0:544,0:960];x=x+.5;y=y+.5
bg=np.array([20,30,40,255],np.uint8);fg=np.array([220,60,100,255],np.uint8);marker=np.array([32,208,80,255],np.uint8)
for stage in range(12):
 mapped=stage//6;X=x;Y=y
 if mapped:
  A,B,C,D,T,U=.85,.1,-.08,.9,50,15;det=A*D-B*C;X=(D*(x-T)-C*(y-U))/det;Y=(-B*(x-T)+A*(y-U))/det
 pos=(X>=180)&(X<620)&(Y>=140)&(Y<390);neg=(X>=320)&(X<470)&(Y>=220)&(Y<310);inside=pos&~neg
 # Retain a conservative edge exclusion for transformed soft-mask filtering.
 edge=np.minimum.reduce([abs(X-180),abs(X-620),abs(Y-140),abs(Y-390),abs(X-320),abs(X-470),abs(Y-220),abs(Y-310)])<2
 clipmask=np.ones((544,960),bool)
 if mapped:clipmask=(x>=190.25)&(x<570.5)&(y>=140.75)&(y<370.25)
 expected=np.broadcast_to(bg,(544,960,4)).copy();expected[inside&clipmask]=fg
 mark=(x>=850)&(x<910)&(y>=450)&(y<510);expected[mark]=marker
 bad=np.any(expected!=frames[stage],axis=2)&(~edge|~clipmask|mark)
 results.append({'test':'opaque_interior_and_exact_clip','stage':stage,'different_pixels':int(bad.sum()),'excluded_edge_pixels':int((edge&clipmask&~mark).sum())})
expected=np.broadcast_to(bg,(544,960,4)).copy();expected[(x>=300)&(x<400)&(y>=200)&(y<300)]=marker
results.append({'test':'failed_opacity_parent_recovery','stage':12,'different_pixels':int(np.any(frames[12]!=expected,axis=2).sum())})
if expanded:
 for base in [3,9]:results.append({'test':'transparent_depth1_vs_depth2_exact','reference':base+1,'stage':base+2,'different_pixels':int(np.any(frames[base+1]!=frames[base+2],axis=2).sum())})
 for stage in range(15,21):
  ref=3 if stage<18 else 9
  results.append({'test':'opaque_parent_soft_exact','reference':ref,'stage':stage,'different_pixels':int(np.any(frames[ref]!=frames[stage],axis=2).sum())})
 results.append({'test':'BC3_linear_vs_swizzled_exact','reference':13,'stage':14,'different_pixels':int(np.any(frames[13]!=frames[14],axis=2).sum())})
 for stage in [13,14]:
  expected_colors=np.array([[255,0,0,255],[0,255,0,255],[0,0,255,255],[255,255,255,255]],np.uint8)
  results.append({'test':'BC3_constant_block_interior','stage':stage,'different_pixels':int(np.any(frames[stage][200,[300,400,500,600]]!=expected_colors,axis=1).sum())})
if len(frames)>=27:
 for stage in [21,23]:results.append({'test':'whole_character_opacity_exact','reference':stage+1,'stage':stage,'different_pixels':int(np.any(frames[stage]!=frames[stage+1],axis=2).sum())})
 # Alpha=1 takes the direct path; compare to the matching ungrouped source.
 results.append({'test':'opacity_one_direct_exact','reference':3,'stage':25,'different_pixels':int(np.any(frames[3]!=frames[25],axis=2).sum())})
if len(frames)>=29:results.append({'test':'consecutive_packet_state_exact','reference':28,'stage':27,'different_pixels':int(np.any(frames[27]!=frames[28],axis=2).sum())})
passed=all(r['different_pixels']==0 for r in results)
report={'status':('scoped_pass_transparent_groups_not_exact' if any(r['different_pixels'] for r in transparent) else 'pass') if passed else 'fail','transparent_parent_measurements':transparent,'results':results,'scope':'Nested-parent exact RGBA equality; opaque interiors and strict outside-clip oracle. Transformed mask boundary pixels excluded from CPU interior oracle, but included in parent exact equality. Transparent-group soft differences are recorded separately and never promoted to exact by a tolerance. Expanded variant tests actual host upload/token/retirement and opaque parent controls. Not real PSB/all materials.','captures':{f'stage-{i:02}.rgba':hashlib.sha256((root/f'stage-{i:02}.rgba').read_bytes()).hexdigest() for i in range(len(frames))}}
(root/'pixel-verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(0 if passed else 1)
