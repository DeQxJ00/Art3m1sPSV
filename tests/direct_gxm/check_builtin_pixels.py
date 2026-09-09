"""Compare the production GXM probe screenshot with independent color math."""
import sys
from PIL import Image
im=Image.open(sys.argv[1]).convert('RGB')
assert im.size==(960,544),im.size
bg=[32,64,96];src=[200,100,50];a=128/255
gray=sum(v*w for v,w in zip(src,[.299,.587,.114]))
neg=[255-v for v in src]
def over(c,alpha=a):return [x*alpha+y*(1-alpha) for x,y in zip(c,bg)]
checks=[]
def check(name,x,y,want):
    actual=im.getpixel((x,y));error=max(abs(v-w) for v,w in zip(actual,want))
    checks.append((name,actual,[round(w,2) for w in want],error))
check('plain',60,60,src)
check('gray',180,60,[gray]*3)
check('negative',300,60,neg)
check('gray then negative',420,60,[255-gray]*3)
check('rule white keeps image',540,60,src)
check('straight alpha',660,60,over(src))
check('negative preserves alpha',780,60,over(neg))
check('group gray opaque',60,190,[gray]*3)
check('group gray unpremultiplies',180,190,over([gray]*3))
check('outer opacity',300,190,over(src,.5))
check('nested negative opacity',420,190,over(neg,a*.5))
check('rendered alpha mask',540,190,over(src))
check('pool reuse negative',660,190,over(neg))
check('ordinary after groups',780,190,[0,255,0])
check('outside groups remains transparent',600,260,bg)
for b in range(1,10):
    if b in [1,7]:want=[s*a+d for s,d in zip(src,bg)]
    elif b in [2,8]:want=[s*d/255+d*(1-a) for s,d in zip(src,bg)]
    elif b in [3,9]:want=[s+d*(1-s/255) for s,d in zip(src,bg)]
    elif b==4:want=[d-s*a for s,d in zip(src,bg)]
    elif b==5:want=[s+d*(1-a) for s,d in zip(src,bg)]
    elif b==6:want=[s+d for s,d in zip(src,bg)]
    check('blend '+str(b),50+(b-1)*102,320,[max(0,min(255,v)) for v in want])
check('clip inside',40,450,[gray]*3)
check('clip outside',90,450,bg)
check('mesh inside',175,450,neg)
check('mesh outside',245,490,bg)
check('ordinary after immediate effect',350,450,[255,0,255])
check('owned capture survives display recycling',480,450,src)
for c in checks:print(c)
failed=[c for c in checks if c[3]>2]
assert not failed,failed
print(f'PASS {len(checks)} pixel checks (tolerance 2/255)')
