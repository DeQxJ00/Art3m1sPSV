"""Independent GL-equation oracle for the direct effects probe (960x544 PNG)."""
import json, sys
from pathlib import Path
import numpy as np
from PIL import Image
actual=np.array(Image.open(sys.argv[1]).convert('RGB'),dtype=float)/255
assert actual.shape==(544,960,3)
bg=np.array([30,90,150,255],dtype=float)/255
src=np.array([180,90,30,128],dtype=float)/255
pm=np.array([90,45,15,128],dtype=float)/255
mask=128/255
checks=[]
def quantize(c): return np.round(np.clip(c,0,1)*255)/255
def blend(s,d,mode):
    sa=s[3];da=d[3]
    if mode==0: out=np.r_[s[:3]*sa+d[:3]*(1-sa),sa+da*(1-sa)]
    elif mode==1: out=s*sa+d
    elif mode==2: out=s*d+d*(1-sa)
    elif mode==3: out=s+d*(1-s)
    elif mode==4: out=np.r_[d[:3]-s[:3]*sa,da]
    elif mode==5: out=s+d*(1-sa)
    elif mode==6: out=s+d
    elif mode==7: out=np.r_[s[:3]*sa+d[:3],da]
    elif mode==8: out=np.r_[s[:3]*d[:3]+d[:3]*(1-sa),da]
    elif mode==9: out=np.r_[s[:3]*(1-d[:3])+d[:3],da]
    else: out=s
    return quantize(out)
def filter_rgb(rgb,gray,negative):
    if gray: rgb=np.full(3,np.dot(rgb,[.299,.587,.114]))
    if negative: rgb=1-rgb
    return rgb
def group(c,tint=np.ones(4),gray=False,negative=False,opaque=0,ma=1):
    rgb=c[:3]/c[3] if c[3]>0 else np.zeros(3)
    rgb=filter_rgb(rgb*tint[:3],gray,negative)
    alpha=((1-opaque)*c[3]+opaque)*tint[3]*ma
    return np.r_[rgb*alpha,alpha]
def point(name,cell,expected,dx=40,dy=34,tolerance=4):
    x=(cell%12)*80+dx;y=(cell//12)*68+dy
    got=actual[y,x]*255;wanted=np.asarray(expected)[:3]*255
    diff=float(np.max(abs(got-wanted)))
    checks.append(dict(case=name,point=[x,y],actual=got.tolist(),expected=wanted.tolist(),max_error=diff,passed=diff<=tolerance))
for f in range(4):
    c=src.copy();c[:3]=filter_rgb(c[:3]*[.6,.8,.4],f&1,f&2);c[3]*=.7
    point(f'sprite_filter_{f}',f,blend(c,bg,0))
for i in range(8):
    rgb=np.array(([255,0,0],[0,255,0],[0,0,255])[i] if i<3 else [180,90,30],dtype=float)/255
    rgb=filter_rgb(rgb,i not in (4,6),i==7)
    point(f'opaque_gray_toggle_{i}',4+i,np.r_[rgb,1],tolerance=1)
for mode in range(10):
    point(f'blend_{mode}',12+mode,blend(src,bg,mode))
    target=blend(src,np.zeros(4),0);target=blend(src,target,mode)
    point(f'blend_group_alpha_{mode}',24+mode,blend(group(target),bg,5),tolerance=5)
for f in range(4): point(f'group_filter_{f}',36+f,blend(group(pm,np.array([.6,.8,.4,.7]),f&1,f&2,ma=mask),bg,5))
point('alpha_mask',40,blend(pm*mask*.7,bg,5))
point('opaque_group',41,blend(group(pm,opaque=1,ma=mask),bg,5))
point('opaque_empty',42,blend(group(np.zeros(4),opaque=1,ma=mask),bg,5))
point('opaque_empty_negative',43,blend(group(np.zeros(4),negative=True,opaque=1,ma=mask),bg,5))
for i in range(3):
    threshold=(i*.5)*1.2-.2;v=np.clip((mask-threshold)/.2,0,1);a=v*v*(3-2*v)
    c=src.copy();c[3]*=a
    point(f'rule_{i}',48+i,blend(c,bg,0))
for cell in [52,53]: point(f'clip_outside_{cell}',cell,bg,dx=20)
c=src.copy();c[:3]=filter_rgb(c[:3],True,True)
point('gray_negative_clip_inside',52,blend(c,bg,0),dx=55)
point('mask_clip_inside',53,blend(pm*mask,bg,5),dx=55)
for mode in range(8):
    c=src.copy()
    if mode==1:
        # Texture UV at the chosen pixel centre, with 4 px cell padding.
        u=(40+.5-4)/72;v=(34+.5-4)/60;c[:3]*=[.2+.8*u,.3+.7*v,1]
    if mode==2: c[3]=max(0,c[3]-.25)
    if mode in [3,6]: c[:3]=np.clip(c[:3]*2,0,1)
    if mode==4: c[:3]*=c[3]
    if mode==5: c[:3]=1-c[:3]
    if mode==6: c[:3]=filter_rgb(c[:3],True,True)
    if mode==7: c[3]=0
    point(f'emote_{mode}',60+mode,blend(c,bg,0))
inner=blend(src,np.zeros(4),0)
outer=blend(src,np.zeros(4),0)
outer=blend(group(inner,np.array([1,1,1,.5]),gray=True),outer,5)
point('nested_group_rendered_mask',72,blend(outer*mask,bg,5),tolerance=5)
point('group_reuse_old_pixels_cleared',73,bg)
point('group_reuse_current_pixels',74,blend(src,bg,0))
point('transparent_copy_not_culled',75,np.zeros(4))
for cell in [22,23,34,35,44,45,46,47,51,54,55,56,57,58,59,68,69,70,71,76,77,78,79,80,81,82,83]:
    point(f'untouched_{cell}',cell,bg)
report=dict(passed=all(c['passed'] for c in checks),count=len(checks),checks=checks)
for i in range(3):
    c=src.copy();c[:3]=filter_rgb(c[:3],i!=1,i!=0)
    point(f'ffi_color_{i}',84+i,blend(c,bg,0))
point('ffi_model_clip_outside',87,bg,dx=20)
point('ffi_model_clip_inside',87,blend(src,bg,0),dx=55)
for cell in [88,89]:
    point(f'ffi_mesh_inside_{cell}',cell,blend(src,bg,0),dx=12,dy=12)
    point(f'ffi_mesh_outside_{cell}',cell,bg,dx=65,dy=45)
point('ffi_mesh_model_clip',89,bg,dx=50,dy=12)
point('ffi_rule_default_white_mask_end',90,bg)
c=src.copy();c[:3]=filter_rgb(c[:3],True,False)
point('ffi_gray_multiply',91,blend(c,bg,2))
point('ffi_mask',92,blend(pm*mask,bg,5))
point('ffi_group_gray_negative',93,blend(group(pm,gray=True,negative=True),bg,5))
c=src.copy();c[0]*=.2+.8*(1-(40+.5-4)/72)
point('ffi_flipped_uv_corner_gradient',94,blend(c,bg,0))
point('ffi_wipe_discard',95,bg)
report=dict(passed=all(c['passed'] for c in checks),count=len(checks),checks=checks)
Path(sys.argv[2]).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'passed':report['passed'],'count':len(checks),'failed':[c for c in checks if not c['passed']]},indent=2))
sys.exit(0 if report['passed'] else 1)
