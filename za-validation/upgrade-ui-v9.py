from pathlib import Path
p=Path('companions/eden-duo-companions-super-mario-odyssey-1.0.0/tools/za/gen_manifest.py')
s=p.read_text(encoding='utf8')
s=s.replace('button(850,890,338,50,"ENVOYER","send_out",wid="send_out",scale=5)', 'button(1084,832,104,104,"","send_out",wid="send_out",scale=5,bg=RED),image(1104,852,64,64,src="file:shapes/send-arrow.png")')
i=s.index('manifest = {')
extra='''# ---- 0.9: persistent detail panes and touch scrolling ------------------------
def scroll_region(name,rect,count,row_h,reset=None):
    out=dict(id=name,rect=rect,count_bind=count,row_h=row_h,fling=True,
             bar=GOLD,bar_track=LINE,bar_w=7)
    if reset:out["reset_bind"]=reset
    return out

def dex_page():
    w=header("dex")
    for i,name in enumerate(["ILLUMIS","MÉGA-DIMENSION"]):
        x=24+i*608
        w += [button(x,126,580,58,name,f"dex_gen_{i+1}",scale=6),
              rect(x,126,580,58,color=GOLD,frame=3,hide_bind="za.dex.gen",keep_min=i+1,keep_max=i+1)]
    for i,name in enumerate(["TOUS","CAPTURÉS","MANQUANTS","VUS","EN STOCK"]):
        x=24+i*240
        w += [button(x,204,228,50,name,f"dex_filter_{i}",scale=4),
              rect(x,204,228,50,color=GOLD,frame=3,hide_bind="za.dex.filter",keep_min=i,keep_max=i)]
    grid=dict(repeat=512,repeat_bind="za.dex.rows",repeat_dy=110,scroll="za_dex")
    gate=dict(need_bind="za.d{i}.v")
    w += [rect(24,276,516,102,bg=PANEL,color=LINE,**grid,**gate),
          image(28,279,96,96,src_format="module:icon/%d",bind="za.d{i}.icon",**grid,**gate),
          bound_text(132,286,"za.d{i}.name",6,TEXT,wrap_width=390,max_lines=1,fit_text=True,**grid,**gate),
          value(132,328,"za.d{i}.number",4,GOLD,text="No. ",pad=3,**grid,**gate),
          value(274,328,"za.d{i}.state",4,DIM,names=DEX_STATES,**grid,**gate),
          rect(24,276,516,102,color=GOLD,frame=3,need_bind="za.d{i}.selected",**grid),
          button(24,276,516,102,"","dex_pick",bg=CLEAR,color=CLEAR,border=0,payload="{i}",**grid,**gate),
          panel(560,276,652,656),
          bound_text(580,292,"za.detail.name",8,TEXT,wrap_width=612,max_lines=1,fit_text=True),
          value(580,344,"za.detail.number",5,GOLD,text="No. ",pad=3),
          image(728,376,296,296,src_format="module:icon/%d",bind="za.detail.icon"),
          value(580,678,"za.detail.state",5,GREEN,names=DEX_STATES),
          value(1186,681,"za.detail.owned",4,DIM,text="EN STOCK ",align="right"),
          bound_text(580,788,"za.detail.description",5,TEXT,wrap_width=600,max_lines=128,line_gap=8,scroll="za_dex_text"),
          value(24,938,"za.dex.rows",3,DIM,text="POKÉMON ")]
    w += type_badge(580,728,290,"za.detail.type1",4)+type_badge(890,728,290,"za.detail.type2",4)
    w += not_ready()+tabbar("dex")
    return dict(id="dex",title="Pokédex",nav_order=nav_order(),widgets=w,scrolls=[
        scroll_region("za_dex",[24,276,516,650],"za.dex.rows",110,"za.dex.reset"),
        scroll_region("za_dex_text",[580,788,612,136],"za.detail.lines",33,"za.detail.sp")])

def bag_page():
    w=header("bag")
    captions=["TOUT","SOINS","BALLS","OBJETS","TRÉSORS","CLÉS","BAIES","CT","MÉGA","AUTRES"]
    for i,name in enumerate(captions):
        x=24+i%5*240;y=130+i//5*64
        w += [button(x,y,228,54,name,f"bag_category_{i}",scale=5),
              rect(x,y,228,54,color=GOLD,frame=3,hide_bind="za.bag.category",keep_min=i,keep_max=i)]
    grid=dict(repeat=1024,repeat_bind="za.bag.rows",repeat_dy=124,scroll="za_bag")
    gate=dict(need_bind="za.i{i}.v")
    w += [rect(24,280,598,116,bg=PANEL,color=LINE,**grid,**gate),
          image(32,290,96,96,src_format="module:item/%d",bind="za.i{i}.id",**grid,**gate),
          bound_text(144,292,"za.i{i}.name",6,TEXT,wrap_width=360,max_lines=2,fit_text=True,**grid,**gate),
          value(602,358,"za.i{i}.count",5,GOLD,text="x",align="right",**grid,**gate),
          rect(24,280,598,116,color=GOLD,frame=3,need_bind="za.i{i}.selected",**grid),
          button(24,280,598,116,"","bag_pick",payload="{i}",bg=CLEAR,color=CLEAR,border=0,**grid,**gate),
          panel(642,280,570,652),
          image(818,304,212,212,src_format="module:item/%d",bind="za.item.id"),
          bound_text(666,542,"za.item.name",7,TEXT,wrap_width=520,max_lines=2,fit_text=True),
          value(1188,308,"za.item.count",6,GOLD,text="x",align="right"),
          bound_text(666,690,"za.item.description",6,TEXT,wrap_width=516,max_lines=128,line_gap=8,scroll="za_bag_text"),
          value(24,938,"za.bag.rows",3,DIM,text="OBJETS ")]
    w+=type_badge(666,632,516,"za.item.type",5)+not_ready()+tabbar("bag")
    return dict(id="bag",title="Sac",nav_order=nav_order(),widgets=w,scrolls=[
        scroll_region("za_bag",[24,280,598,648],"za.bag.rows",124,"za.bag.reset"),
        scroll_region("za_bag_text",[666,690,520,232],"za.item.lines",38,"za.item.id")])

def missions_page():
    w=header("missions")
    for i,caption in enumerate(QUEST_CATEGORIES):
        x=24+i*400
        w += [button(x,132,388,64,caption,f"quest_category_{i}",scale=6),
              rect(x,132,388,64,color=GOLD,frame=3,hide_bind="za.quest.category",keep_min=i,keep_max=i)]
    w += [bound_text(24,214,"za.quest.status",4,DIM,wrap_width=1188,max_lines=1)]
    grid=dict(repeat=1024,repeat_bind="za.quest.rows",repeat_dy=164,scroll="za_quests")
    gate=dict(need_bind="za.q{i}.v")
    w += [rect(24,266,1188,152,bg=PANEL,color=LINE,**grid,**gate),
          bound_text(46,282,"za.q{i}.title",7,TEXT,wrap_width=1140,max_lines=1,fit_text=True,hide_bind="za.q{i}.state",hide_eq=3,**grid,**gate),
          bound_text(46,282,"za.q{i}.title",7,DIM,wrap_width=1140,max_lines=1,fit_text=True,hide_bind="za.q{i}.state",keep_min=3,keep_max=3,**grid,**gate),
          bound_text(46,332,"za.q{i}.description",5,DIM,wrap_width=1140,max_lines=1,fit_text=True,**grid,**gate),
          value(46,382,"za.q{i}.state",5,GOLD,names=QUEST_STATES,**grid,**gate),
          rect(24,266,1188,152,color=GOLD,frame=4,hide_bind="za.q{i}.state",keep_min=2,keep_max=2,**grid,**gate),
          button(24,266,1188,152,"","quest_pick",payload="{i}",bg=CLEAR,color=CLEAR,border=0,**grid,**gate),
          value(24,936,"za.quest.rows",3,DIM,text="MISSIONS ")]
    w+=not_ready()+tabbar("missions")
    return dict(id="missions",title="Missions",nav_order=nav_order(),widgets=w,scrolls=[scroll_region("za_quests",[24,266,1188,658],"za.quest.rows",164,"za.quest.category")])

_old_map_page=map_page
def map_page():
    page=_old_map_page()
    w=[v for v in page["widgets"] if not (v["rect"][0]>=700 and 120<=v["rect"][1]<916)]
    for v in w:
        if v.get("src")=="module:map":v.update(on_tap="map_tap",payload="$tap")
    slot=next(i for i,v in enumerate(w) if v.get("input_block") and v.get("need_bind")=="za.center.open")
    grid=dict(repeat=128,repeat_bind="za.zone.rows",repeat_dy=112,scroll="za_zone")
    gate=dict(need_bind="za.zp{i}.v")
    right=[panel(710,200,502,640),
           value(730,220,"za.zone.selected",6,GOLD,text="ZONE SAUVAGE ",hide_bind="za.zone.selected",hide_eq=0),
           label(730,220,"RENCONTRES",6,GOLD,hide_bind="za.zone.selected",keep_min=0,keep_max=0),
           label(736,330,"Touche une zone verte\\npour voir ses Pokémon.",5,DIM,hide_bind="za.zone.selected",keep_min=0,keep_max=0),
           rect(722,282,476,104,bg=PANEL_HI,**grid,**gate),
           image(726,286,96,96,src_format="module:icon/%d",bind="za.zp{i}.icon",**grid,**gate),
           bound_text(834,292,"za.zp{i}.name",5,TEXT,wrap_width=352,max_lines=1,fit_text=True,**grid,**gate),
           bound_text(834,326,"za.zp{i}.levels",3,DIM,wrap_width=352,max_lines=1,**grid,**gate),
           value(834,356,"za.zp{i}.state",3,GREEN,names=DEX_STATES,**grid,**gate),
           button(40,858,312,54,"CARTE DU JEU","game_map",scale=4),
           button(368,858,312,54,"DESTINATIONS","game_destinations",scale=4,hide_bind="za.travel.active",hide_eq=1),
           button(368,858,312,54,"ANNULER VOYAGE","travel_cancel",scale=4,need_bind="za.travel.active")]
    for i in range(20):
        right.append(button(-20,-20,40,40,"",f"zone_pick_{i}",bg=CLEAR,color=CLEAR,border=0,x_bind=f"za.zone{i}.x",y_bind=f"za.zone{i}.y",need_bind=f"za.zone{i}.v"))
    w[slot:slot]=right
    page["widgets"]=w;page["scrolls"]=[scroll_region("za_zone",[722,282,476,548],"za.zone.rows",112,"za.zone.selected")]
    return page

actions["bag_pick"]={"kind":"module","action":"bag_pick","argument":"$payload"}
actions["map_tap"]={"kind":"module","action":"map_tap","argument":"$payload"}
actions["game_menu"]={"kind":"button","button":"X","frames":10}
for i in range(20):actions[f"zone_pick_{i}"]={"kind":"module","action":"zone_pick","argument":i}

'''
s=s[:i]+extra+s[i:]
# The dex no longer changes page when a row is selected.
a=s.index('    "page_binds": [');b=s.index('                   {"point":"za.quest.open"',a)
s=s[:a]+'    "page_binds": ['+s[b:]
# A generated, redistributable arrow, so it does not depend on phone font coverage.
i=s.index('round_pages(manifest["pages"])')
s=s[:i]+'''def write_send_arrow():
    raw=bytearray()
    for y in range(64):
        raw.append(0)
        for x in range(64):
            on=(12<=y<=34 and abs(x-31.5)<=y-11) or (27<=x<=36 and 30<=y<=54)
            raw.extend([255,255,255,255 if on else 0])
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    target=OUT.parent/"shapes/send-arrow.png";target.parent.mkdir(parents=True,exist_ok=True)
    target.write_bytes(b'\\x89PNG\\r\\n\\x1a\\n'+chunk(b'IHDR',struct.pack('>2I5B',64,64,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(raw),9))+chunk(b'IEND',b''))

write_send_arrow()
'''+s[i:]
p.write_text(s,encoding='utf8');print('UI upgraded')
