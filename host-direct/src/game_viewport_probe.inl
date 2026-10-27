bool game_viewport_self_test(){
    if(active)return false;
    bool ok=true;
    std::vector<uint8_t> screen(960*544*4),snapshot(720*540*4);
    for(unsigned pass=0;pass<3;++pass){
        const unsigned w=pass==1?540:720,h=pass==1?960:540;
        const auto view=game_viewport(w,h);
        begin();rect(0,0,960,544,0x204060ff);rect(0,0,480,544,0xe04020ff);
        ok=fit_game_frame(w,h)&&ok;end();wait();
        ok=readback(960,544,screen.data())&&ok;
        auto color=[&](unsigned x,unsigned y,unsigned r,unsigned g,unsigned b){
            const auto* p=screen.data()+(y*960+x)*4;
            return std::abs(int(p[0])-int(r))<=2&&std::abs(int(p[1])-int(g))<=2&&std::abs(int(p[2])-int(b))<=2;
        };
        ok=color(0,0,0,0,0)&&color(unsigned(view.x+view.w*.25f),272,224,64,32)&&color(unsigned(view.x+view.w*.75f),272,32,64,96)&&ok;
        ok=read_game_frame(720,540,snapshot.data())&&ok;
        ok=snapshot[0]==224&&snapshot[(720-1)*4]==32&&ok;
        // GPU transition capture must exclude the same bars as CPU thumbnails.
        begin();auto* captured=capture_completed_texture();
        if(captured){
            Vertex q[]={{0,0,0,0,1,1,1,1},{960,0,1,0,1,1,1,1},{0,544,0,1,1,1,1,1},{960,544,1,1,1,1,1,1}};
            draw_quad(captured,q);ok=fit_game_frame(w,h)&&ok;
        }else ok=false;
        end();wait();if(captured)destroy(captured);
        ok=readback(960,544,screen.data())&&ok;
        ok=color(unsigned(view.x+view.w*.1f),272,224,64,32)&&color(unsigned(view.x+view.w*.9f),272,32,64,96)&&ok;
    }
    begin();rect(0,0,960,544,0x204060ff);ok=fit_game_frame(960,540)&&ok;end();wait();
    ok=read_game_frame(720,540,snapshot.data())&&snapshot[0]==32&&ok;
    log("[viewport-probe] pass=%d landscape_portrait_repeat=3 capture=cpu+gpu native_reset=1",int(ok));
    return ok;
}
