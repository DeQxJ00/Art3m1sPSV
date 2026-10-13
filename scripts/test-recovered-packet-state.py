from pathlib import Path
import re,subprocess,hashlib,json
root=Path(__file__).resolve().parents[1];out=root/'build/recovered-packet-state';out.mkdir(parents=True,exist_ok=True)
sdk='/home/qxj00/ae3-vitagl-build-20260830/vitasdk/arm-vita-eabi/include'
def linux(p):return '/mnt/'+str(p)[0].lower()+str(p)[2:].replace('\\','/')
gpu=(root/'host-direct/src/gpu.cpp').read_text(encoding='utf-8');resume=gpu[gpu.index('bool resume_target('):gpu.index('void clear_offscreen()')];(out/'recovered_resume_target.inc').write_text(resume,encoding='utf-8')
fixture=root/'tests/direct_gxm/recovered_packet_state.cpp';source=(root/'host-direct/src/recovered_packet.inl').read_text();test=fixture.read_text();names=set(re.findall(r'\b(sceGxm\w+)\(',source+resume));overrides=set(re.findall(r'#define (sceGxm\w+)',test));(out/'recovered_packet_mock_ignores.inc').write_text('\n'.join('#define '+n+' ignore' for n in sorted(names-overrides)))
recovery=root.parent/'emote-psv-test';inc=['-I'+linux(root/'host-direct/src'),'-I'+linux(recovery/'native'),'-I'+linux(recovery/'recovered'),'-I'+linux(out),'-idirafter',sdk]
flags=['-O2','-g','-ffp-contract=off','-fsanitize=address,undefined','-no-pie']
for n,folder in [('render_packet','native'),('material','recovered')]:subprocess.run(['wsl','-e','gcc',*flags,*inc,'-c',linux(recovery/folder/(n+'.c')),'-o',linux(out/(n+'.o'))],check=True)
subprocess.run(['wsl','-e','g++','-std=c++17',*flags,*inc,linux(fixture),linux(out/'render_packet.o'),linux(out/'material.o'),'-o',linux(out/'test')],check=True)
r=subprocess.run(['wsl','-e',linux(out/'test')],capture_output=True,text=True);print(r.stdout,r.stderr);r.check_returncode();assert not r.stderr
(root/'tests/direct_gxm/recovered-packet-state-verification.json').write_text(json.dumps({'status':'pass','output':r.stdout,'scope':'Instrumented production GPU module; no actual shader execution or rasterization. Device pixels are separate.','boundaries':['feature disabled by default','only 960x544 parent targets','opacity other than 1 rejected','affine uses full-resolution full soft ROI','API failure may leave already-drawn pixels; failed parent BeginScene leaves frame incomplete and inactive'], 'sources':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'host-direct/src/gpu.cpp',root/'host-direct/src/recovered_packet.inl',fixture,Path(__file__)]}},indent=2)+'\n')
