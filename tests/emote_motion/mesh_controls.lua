-- Independent controls for the external motion demo. The main script waits
-- with input=0 so arrows/capture cannot also advance the scenario page.
demo.mesh_ratio = em:getMeshDivisionRatio()
demo.mesh_ratios = {1.0, 0.8, 0.6, 0.4}
demo.mesh_slot = 1
for i, value in ipairs(demo.mesh_ratios) do
 if math.abs(value - demo.mesh_ratio) < 0.00001 then demo.mesh_slot=i end
end
demo.motion_names = {
 "STILL", "IDLE", "IDLE + MOUTH", "NOD", "SHAKE HEAD",
 "TILT HEAD", "TURN", "WALK", "JOY", "MOUTH CLOSED",
 "MOUTH HALF", "MOUTH OPEN", "SPEECH + IDLE", "SPEECH + NOD", "SPEECH + SHAKE"
}
function demo_caption(e)
 e:tag{"rp"}
 e:tag{"print", data=string.format("[%02d/15] %s | MESH:%.2f",demo.index,demo.motion_names[demo.index],demo.mesh_ratio)}
 e:tag{"rt"}
 e:tag{"print", data="Circle:motion | Left/Right:mesh | Triangle:capture"}
 e:tag{"trans", time=0}
 e:tag{"debugprint", data=string.format("EMOTE-CONTROLS motion=%d ratio=%.2f start=%s",demo.index,demo.mesh_ratio,tostring(demo.start))}
end
local original_stage = demo_stage
function demo_stage(e,p)
 original_stage(e,p)
 demo_caption(e)
end
local original_frame = demo_frame
function demo_frame(e,p)
 if e:isDownEdge(27) then original_frame(e,p);return end
 local step=0
 if e:isDownEdge(37) then step=-1 elseif e:isDownEdge(39) then step=1 end
 if step~=0 then
  demo.mesh_slot=(demo.mesh_slot-1+step)%#demo.mesh_ratios+1
  demo.mesh_ratio=demo.mesh_ratios[demo.mesh_slot]
  em:setMeshDivisionRatio(demo.mesh_ratio)
 end
 if e:isDownEdge(13) then
  demo_stage(e,{index=demo.index%15+1})
 elseif step~=0 then
  -- Do not restart the timeline, mouth clock, expression or variables.
  demo_caption(e)
 end
 original_frame(e,p)
end
