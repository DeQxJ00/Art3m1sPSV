#!/usr/bin/env python3
"""Compare the actual per-layer memo helpers against uncached/hash fallbacks.

Run with a host C compiler: python tests/direct_gxm/recovered_layer_memo_test.py
No external model or device is required.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "vendor/emote-recovery/native/model_view.c").read_text(encoding="utf-8")


def function(name):
    start = SOURCE.index("static void " + name + "(")
    end = SOURCE.index("{", start) + 1
    depth = 1
    while depth:
        depth += (SOURCE[end] == "{") - (SOURCE[end] == "}")
        end += 1
    return SOURCE[start:end]


types = "\n".join(line for line in SOURCE.splitlines()
                  if line.startswith("typedef struct") and any(
                      line.endswith("} " + name + ";") for name in
                      ("Mat", "ParentSyncMemo", "ShapeTrigMemo", "ShapeMemo")))
header = r"""
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include "mesh_child.h"
#include "exact_bytes.h"
typedef struct {int dummy;} EmSceneLayer;
"""
context = r"""
typedef struct {
 ShapeTrigMemo shape_trig_memo[8];
 ShapeMemo shape_memo[64],*shape_by_layer;unsigned shape_by_layer_count;
 ParentSyncMemo parent_sync[128],*parent_sync_by_layer;unsigned parent_sync_by_layer_count;
} EmViewContext;
"""
main = r"""
int main(void){
 EmViewContext direct={0},hashed={0};
 direct.shape_by_layer=calloc(1600,sizeof(ShapeMemo));direct.shape_by_layer_count=1600;
 direct.parent_sync_by_layer=calloc(1600,sizeof(ParentSyncMemo));direct.parent_sync_by_layer_count=1600;
 EmSceneLayer layers[1601];unsigned checks=0;
 for(int pass=0;pass<5;pass++)for(unsigned i=0;i<1601;i++){
  int order[4]={0,3,2,1};if(i%3==0){order[0]=1;order[3]=0;}
  Mat a={0};a.angle=(float)(i%117)+(pass==3?0.25f:0);a.zx=1+(i%7)*.1f;a.zy=.7f;a.shx=(i%5)*.01f;a.shy=(i%3)*.02f;a.fx=i&1;a.fy=i&2;Mat b=a;
  shape(&direct,i,&a,order);shape(&hashed,i,&b,order);assert(!memcmp(&a,&b,sizeof a));
  EmPoint patch[16];for(int k=0;k<16;k++)patch[k]=(EmPoint){(k%4)/3.f+(i%7)*.002f,(k/4)/3.f+(pass==3?.003f:0)};
  EmMeshChildPose p={{i%100,i%87},i%30,1.2f,.8f},q=p;unsigned sync=(i%4)*2+1,inherit=(i%8)*16;
  EmPoint offset={i%3,i%5};parent_sync(&direct,i,layers+i,&p,patch,200,300,offset,sync,inherit);em_mesh_child_sync(&q,patch,200,300,offset,sync,inherit);assert(!memcmp(&p,&q,sizeof p));
  /* Components ignored by the kernel must remain live on a cache hit. */
  p=q=(EmMeshChildPose){{i%100,i%87},55,1.8f,2.1f};parent_sync(&direct,i,layers+i,&p,patch,200,300,offset,sync,inherit);em_mesh_child_sync(&q,patch,200,300,offset,sync,inherit);assert(!memcmp(&p,&q,sizeof p));checks+=3;
 }
 printf("PASS %u exact comparisons, direct and fallback slots, changed dependencies and ignored components\n",checks);return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="layer-memo-test-") as directory:
    test = Path(directory) / "test.c"
    binary = Path(directory) / ("test.exe" if os.name == "nt" else "test")
    test.write_text(header + types + context + "\n".join(function(name) for name in
                    ("shape_trig", "shape_calculate", "shape", "parent_sync")) + main,
                    encoding="utf-8")
    recovered = ROOT / "vendor/emote-recovery/recovered"
    subprocess.run([os.environ.get("CC", "cc"), "-O2", "-ffp-contract=off",
                    "-I" + str(recovered), "-I" + str(ROOT / "vendor/emote-recovery/native"),
                    str(test), str(recovered / "mesh_child.c"), str(recovered / "mesh.c"),
                    "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
