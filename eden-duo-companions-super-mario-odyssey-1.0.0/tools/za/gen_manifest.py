#!/usr/bin/env python3
"""Writes packages/PokemonLegendsZA/dualscreen/manifest.json.

Six French tabs on a 1240x1080 logical canvas. Values come from the player's
live game state or save via the native module; names, icons and the map come from their romfs.
Only the redistributable DejaVu font atlas/metrics ship with the package.

  header   0..108
  content  120..950
  tab bar  958..1080

The map shows live world coordinates on verified build 2.0.2, with save fallback. The outdoor player marker uses measured world-to-texture calibration.

Run from anywhere: python tools/za/gen_manifest.py
"""
import json
import math
import struct
import zlib
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "packages/PokemonLegendsZA/dualscreen/manifest.json"

W, H = 1240, 1080

# palette (AARRGGBB)
BG = "#FF070A11"
HEADER = "#FF101623"
LINE = "#FF232C3E"
PANEL = "#FF141B28"
PANEL_HI = "#FF1A2231"
WELL = "#FF0B111C"
RED = "#FFE33B3B"
GOLD = "#FFF2C33D"
TEXT = "#FFE8EEF7"
DIM = "#FF8492A8"
GREEN = "#FF3FD07A"
BLUE = "#FF57A9F5"
CLEAR = "#00000000"

# the game's box and party geometry (za_save.h)
BOX_SLOTS = 30
PARTY_SLOTS = 6

# PA9 nature ids, French names written ASCII (the built-in font draws e-acute as "e", but
# spelling them plainly keeps the manifest readable and the widths predictable).
NATURES = [
    "Hardi", "Solitaire", "Brave", "Rigide", "Mauvais",
    "Assuré", "Docile", "Relax", "Malin", "Lâche",
    "Timide", "Pressé", "Sérieux", "Jovial", "Naïf",
    "Modeste", "Doux", "Calme", "Pudique", "Foufou",
    "Serein", "Gentil", "Malpoli", "Prudent", "Bizarre",
]
LANGUAGES = ["-", "JAP", "ANG", "FRA", "ITA", "ALL", "-", "ESP", "COR", "CHS", "CHT"]
GENDERS = ["M", "F", "-"]

TABS = [("team", "ÉQUIPE"), ("dex", "POKÉDEX"), ("box", "BOÎTE"),
        ("map", "CARTE"), ("bag", "SAC"), ("missions", "MISSIONS")]
PAGE_TITLE = dict(TABS)


# ---- small builders ------------------------------------------------------------------------

def rect(x, y, w, h, bg=CLEAR, color=CLEAR, frame=2, **kw):
    out = {"type": "rect", "rect": [x, y, w, h], "bg": bg, "color": color}
    if frame != 2:
        out["frame"] = frame
    out.update(kw)
    return out


def panel(x, y, w, h, bg=PANEL, edge=LINE):
    return rect(x, y, w, h, bg=bg, color=edge, frame=2)


def label(x, y, text, scale, color=TEXT, align="left", **kw):
    out = {"type": "label", "rect": [x, y, 0, scale * 5], "text": text,
           "text_scale": scale, "color": color, "align": align}
    out.update(kw)
    return out


def value(x, y, bind, scale, color=TEXT, align="left", **kw):
    out = {"type": "value", "rect": [x, y, 0, scale * 5], "bind": bind,
           "text_scale": scale, "color": color, "align": align}
    out.update(kw)
    return out


def bound_text(x, y, bind, scale, color=TEXT, align="left", **kw):
    return label(x, y, "", scale, color, align, bind_text=bind, **kw)


def image(x, y, w, h, **kw):
    return {"type": "image", "rect": [x, y, w, h], **kw}


def bar(x, y, w, h, bind, color=BLUE, max_bind=None, maxv=None, bg=WELL, frame=0, **kw):
    out = {"type": "bar", "rect": [x, y, w, h], "bind": bind, "color": color, "bg": bg,
           "frame": frame}
    if max_bind:
        out["max_bind"] = max_bind
    if maxv is not None:
        out["max"] = maxv
    out.update(kw)
    return out


def button(x, y, w, h, text, action, wid=None, scale=4, bg=PANEL_HI, color=TEXT, **kw):
    out = {"type": "button", "rect": [x, y, w, h], "text": text, "text_scale": scale,
           "align": "center", "bg": bg, "color": color, "border": 2, "on_tap": action}
    if wid:
        out["id"] = wid
    out.update(kw)
    return out


def header(page_id):
    """A quiet title strip. Loading/save diagnostics belong in the content, not the header."""
    return [
        rect(0, 0, W, 108, bg=HEADER),
        image(1, 1, 1, 1, src="module:ready"),
        rect(0, 104, W, 4, bg=RED, frame=0),
        label(28, 18, "LÉGENDES POKÉMON Z-A", 6),
        label(30, 66, PAGE_TITLE[page_id], 3, GOLD),
        bound_text(1212, 28, "za.ot", 5, align="right"),
    ]


def tabbar(active):
    out = [rect(0, 958, W, 122, bg=HEADER),
           rect(0, 958, W, 3, bg=LINE, frame=0)]
    for i, (pid, name) in enumerate(TABS):
        on = pid == active
        out.append({
            "id": f"tab_{pid}", "type": "button",
            "rect": [12 + i * 204, 984, 192, 76],
            "text": name, "text_scale": 4, "align": "center",
            "bg": RED if on else PANEL_HI,
            "color": "#FFFFFFFF" if on else DIM,
            "border": 0 if on else 2,
            "on_tap": f"go_{pid}",
        })
    return out


def not_ready():
    """A panel over the content while no save has been read yet (the tab bar stays usable)."""
    gate = {"need_bind": "!za.ready"}
    return [
        rect(0, 108, W, 850, bg="#F0070A11", frame=0, input_block=True, **gate),
        label(620, 360, "SAUVEGARDE NON LUE", 6, RED, align="center", **gate),
        label(620, 430, "Z-A 2.0.2 requis pour le suivi en direct.", 4, TEXT,
              align="center", **gate),
        bound_text(620, 490, "za.status", 4, DIM, align="center", **gate),
    ]


def nav_order(*extra):
    return [f"tab_{pid}" for pid, _ in TABS] + list(extra)


# ---- EQUIPE ---------------------------------------------------------------------------------

TYPES = ["NORMAL", "COMBAT", "VOL", "POISON", "SOL", "ROCHE", "INSECTE", "SPECTRE",
         "ACIER", "FEU", "EAU", "PLANTE", "ÉLECTRIK", "PSY", "GLACE", "DRAGON", "TÉNÈBRES", "FÉE"]
TYPE_COLORS = ["#FF9099A0", "#FFC34E48", "#FF92A9DE", "#FFAA65B7", "#FFC5A971", "#FFB9A65C",
               "#FF93AE3D", "#FF6C71B6", "#FF83A2B4", "#FFE58A41", "#FF4C98DB", "#FF65B55A",
               "#FFE4C547", "#FFDC75A3", "#FF75C9CF", "#FF697FC7", "#FF80727A", "#FFDA91C6"]


def type_badge(x, y, w, bind, scale=3, **gate):
    """Unknown types are -1. A single-type Pokémon has type2=-1, so no false Normal badge."""
    out=[]
    for i, (name, color) in enumerate(zip(TYPES, TYPE_COLORS)):
        out += [rect(x,y,w,40,bg=color,frame=0,hide_bind=bind,keep_min=i,keep_max=i,**gate),
                image(x+5,y+5,30,30,src=f"module:type/{i}",hide_bind=bind,keep_min=i,keep_max=i,**gate),
                label(x+42+(w-42)//2,y+10,name,scale,"#FFFFFFFF",align="center",
                      hide_bind=bind,keep_min=i,keep_max=i,**gate)]
    return out


def team_page():
    w=header("team")
    for i in range(PARTY_SLOTS):
        x=24+i*200
        present={"need_bind":f"za.p{i}.v"}
        w += [rect(x,126,188,304,bg=PANEL_HI,color=LINE,frame=2),
              image(x+14,138,160,160,src_format="module:icon/%d",bind=f"za.p{i}.icon",**present),
              bound_text(x+14,304,f"za.p{i}.name",4,TEXT,wrap_width=160,max_lines=1,fit_text=True,**present),
              bar(x+14,340,160,13,f"za.p{i}.hp",GREEN,max_bind=f"za.p{i}.hpmax",**present),
              bar(x+14,364,160,7,f"za.p{i}.xp","#FF36B8EA",max_bind=f"za.p{i}.xpmax",**present),
              value(x+14,386,f"za.p{i}.lvl",5,TEXT,text="N. ",**present),
              value(x+170,392,f"za.p{i}.g",3,DIM,names=GENDERS,align="right",**present),
              label(x+94,224,"VIDE",3,DIM,align="center",hide_bind=f"za.p{i}.v",hide_eq=1),
              rect(x,126,188,304,color=GOLD,frame=4,hide_bind="za.team.selected",keep_min=i,keep_max=i),
              button(x,126,188,304,"","team_select",wid=f"party_{i}",bg=CLEAR,color=CLEAR,border=0,payload=i,**present)]
    w += [panel(24,448,1188,496,bg="#FF202A44"),rect(24,448,1188,64,bg="#FFE8EEF7",frame=0),
          bound_text(48,460,"za.sel.name",7,"#FF202738",wrap_width=760,max_lines=1,fit_text=True),
          value(1188,466,"za.sel.lvl",5,"#FF202738",text="N. ",align="right"),
          bar(48,532,330,20,"za.sel.hp",GREEN,max_bind="za.sel.hpmax"),
          value(404,526,"za.sel.hp",5,TEXT,max_bind="za.sel.hpmax",max_sep=" / "),
          value(1188,565,"za.sel.nature",3,DIM,table="natures",align="right"),
          button(1084,832,104,104,"","send_out",wid="send_out",scale=5,bg=RED),image(1104,852,64,64,src="file:shapes/send-arrow.png"),bound_text(48,876,"za.send.status",4,DIM,wrap_width=770,max_lines=1),bound_text(48,908,"za.live.status",4,DIM,wrap_width=770,max_lines=2,line_gap=2)]
    w += type_badge(48,566,198,"za.sel.type1")+type_badge(260,566,198,"za.sel.type2")
    for i,(x,y) in enumerate([(342,618),(48,706),(644,706),(342,794)]):
        w += [rect(x,y,556,76,bg=PANEL_HI,color=LINE,frame=2),
              image(x+12,y+14,48,48,src_format="module:type/%d",bind=f"za.sel.m{i}.type"),
              bound_text(x+70,y+8,f"za.sel.m{i}.name",7,TEXT,wrap_width=440,max_lines=1,fit_text=True),
              value(x+532,y+48,f"za.sel.m{i}.pp",5,DIM,text="PP ",align="right",need_bind="za.sel.moves_ready")]
        for t,color in enumerate(TYPE_COLORS):
            gate=dict(hide_bind=f"za.sel.m{i}.type",keep_min=t,keep_max=t)
            w += [rect(x,y,5,76,bg=color,frame=0,**gate),label(x+72,y+46,TYPES[t],5,color,**gate)]
    # Memory failures need a readable explanation on a phone, not a tiny footer.
    problem={"need_bind":"za.live.problem"}
    w += [rect(40,618,1160,210,bg=PANEL_HI,color=LINE,frame=2,**problem),
          label(64,640,"DONNÉES EN DIRECT INDISPONIBLES",6,GOLD,**problem),
          bound_text(64,692,"za.live.status",6,TEXT,wrap_width=1100,max_lines=4,line_gap=8,**problem)]
    w += not_ready()+tabbar("team")
    return {"id":"team","title":"Équipe","nav_order":nav_order(*[f"party_{i}" for i in range(PARTY_SLOTS)],"send_out"),"widgets":w}


# ---- POKEDEX --------------------------------------------------------------------------------

DEX_STATES = ["MANQUANT", "VU", "CAPTURÉ", "EN STOCK"]
DEX_FILTERS = ["TOUS", "CAPTURÉS", "MANQUANTS", "VUS SEULEMENT", "EN STOCK"]
POUCHES = ["SOINS", "BALLS", "OBJETS", "TRÉSORS", "OBJETS CLÉS", "BAIES", "CT", "MÉGA-GEMMES", "AUTRES"]


def dex_page():
    w = header("dex")
    w += [value(24, 132, "za.dex.caught", 6, GOLD, text="CAPTURÉS "),
          value(440, 132, "za.dex.known", 6, BLUE, text="VUS "),
          value(850, 142, "za.dex.gen", 4, TEXT, text="GÉNÉRATION ")]
    for gen in range(1,10):
        w.append(button(24+(gen-1)*132, 194, 124, 48, f"GEN {gen}", f"dex_gen_{gen}",
                        wid=f"dex_gen_{gen}", scale=3))
        w.append(rect(24+(gen-1)*132,194,124,48,color=GOLD,frame=3,
                      hide_bind="za.dex.gen",keep_min=gen,keep_max=gen))
    for f, caption in enumerate(DEX_FILTERS):
        w.append(button(24+f*240,254,228,48,caption,f"dex_filter_{f}",
                        wid=f"dex_filter_{f}",scale=3))
        w.append(rect(24+f*240,254,228,48,color=GOLD,frame=3,
                      hide_bind="za.dex.filter",keep_min=f,keep_max=f))
    grid={"repeat":12,"repeat_cols":3,"repeat_dx":408,"repeat_row_dy":144}
    gate={"need_bind":"za.d{i}.v"}
    w += [rect(24,322,388,136,bg=PANEL,color=LINE,**grid,**gate),
          image(28,326,128,128,src_format="module:icon/%d",bind="za.d{i}.icon",
                hide_bind="za.d{i}.available",hide_eq=0,**grid,**gate),
          bound_text(164,338,"za.d{i}.name",5,TEXT,wrap_width=234,max_lines=1,fit_text=True,**grid,**gate),
          value(164,380,"za.d{i}.sp",3,DIM,text="NO.",pad=3,**grid,**gate),
          value(164,406,"za.d{i}.state",3,DIM,names=DEX_STATES,**grid,**gate),
          value(164,432,"za.d{i}.owned",2,GREEN,text="EN STOCK ",hide_bind="za.d{i}.owned",hide_eq=0,**grid,**gate),
          button(24,322,388,136,"","dex_pick",wid="dex_row_{i}",bg=CLEAR,color=CLEAR,border=0,
                 payload="{i}",**grid,**gate),
          value(24,914,"za.dex.rows",3,DIM,text="RÉSULTATS "),
          value(400,910,"za.dex.page",4,TEXT,text="PAGE ",max_bind="za.dex.pages",max_sep=" / "),
          button(850,900,170,44,"PRÉCÉDENT","dex_prev",wid="dex_prev",scale=3),
          button(1038,900,174,44,"SUIVANT","dex_next",wid="dex_next",scale=3)]
    w += not_ready()+tabbar("dex")
    return {"id":"dex","title":"Pokédex", "nav_order":nav_order(
        *[f"dex_gen_{i}" for i in range(1,10)],*[f"dex_filter_{i}" for i in range(5)],
        "dex_prev","dex_next",*[f"dex_row_{i}" for i in range(12)]),"widgets":w}


def dex_detail_page():
    w=header("dex")
    w += [image(52,206,344,344,src_format="module:icon/%d",bind="za.detail.icon"),
          bound_text(442,144,"za.detail.name",10,TEXT,wrap_width=748,max_lines=1,fit_text=True),
          value(442,212,"za.detail.sp",5,GOLD,text="NO. ",pad=3),
          value(754,218,"za.detail.state",4,DIM,names=DEX_STATES),
          value(442,270,"za.detail.owned",4,GREEN,text="EN STOCK "),
          panel(420,396,792,394),
          bound_text(444,418,"za.detail.description",5,TEXT,wrap_width=740,max_lines=8,line_gap=10),
          bound_text(52,630,"za.detail.height",5,TEXT,wrap_width=300,max_lines=1),
          bound_text(52,714,"za.detail.weight",5,TEXT,wrap_width=300,max_lines=1),
          button(24,836,320,76,"RETOUR AU POKÉDEX","go_dex",wid="dex_back",scale=5),bound_text(48,876,"za.send.status",4,DIM,wrap_width=770,max_lines=1),bound_text(48,908,"za.live.status",4,DIM,wrap_width=770,max_lines=2,line_gap=2)]
    w += type_badge(442,328,238,"za.detail.type1",4)
    w += type_badge(698,328,238,"za.detail.type2",4)
    w += not_ready()+tabbar("dex")
    return {"id":"dex_detail","title":"Fiche Pokémon","nav_order":nav_order("dex_back"),"widgets":w}


def bag_page():
    w=header("bag")
    w += [value(24,130,"za.bag.total",5,GOLD,text="TYPES D OBJETS "),
          value(800,136,"za.bag.category",4,TEXT,names=["TOUT LE SAC"]+POUCHES)]
    captions=["TOUT","SOINS","BALLS","OBJETS","TRÉSORS","CLÉS","BAIES","CT","MÉGA","AUTRES"]
    for i,caption in enumerate(captions):
        x=24+(i%5)*240; y=190+(i//5)*58
        w += [button(x,y,228,50,caption,f"bag_category_{i}",wid=f"bag_category_{i}",scale=5),
              rect(x,y,228,50,color=GOLD,frame=3,hide_bind="za.bag.category",keep_min=i,keep_max=i)]
    grid={"repeat":8,"repeat_cols":2,"repeat_dx":608,"repeat_row_dy":142}
    gate={"need_bind":"za.i{i}.v"}
    w += [rect(24,320,584,132,bg=PANEL,color=LINE,**grid,**gate),
          image(34,338,96,96,src_format="module:item/%d",bind="za.i{i}.id",**grid,**gate),
          bound_text(148,332,"za.i{i}.name",6,TEXT,wrap_width=290,max_lines=2,**grid,**gate),
          value(586,334,"za.i{i}.count",7,GOLD,text="x",align="right",**grid,**gate),
          value(148,414,"za.i{i}.pouch",5,DIM,names=POUCHES,**grid,**gate),
          label(390,414,"FAVORI",4,GOLD,need_bind="za.i{i}.favorite",**grid),
          label(492,414,"ÉQUIPÉ",4,BLUE,need_bind="za.i{i}.held",**grid),
          value(24,914,"za.bag.rows",5,DIM,text="RÉSULTATS "),
          value(400,910,"za.bag.page",5,TEXT,text="PAGE ",max_bind="za.bag.pages",max_sep=" / "),
          button(850,900,170,44,"PRÉCÉDENT","bag_prev",wid="bag_prev",scale=4),
          button(1038,900,174,44,"SUIVANT","bag_next",wid="bag_next",scale=4)]
    w += not_ready()+tabbar("bag")
    return {"id":"bag","title":"Sac","nav_order":nav_order(
        *[f"bag_category_{i}" for i in range(10)],"bag_prev","bag_next"),"widgets":w}


# ---- BOITE ----------------------------------------------------------------------------------

CELL_W, CELL_H = 190, 132
CELL_DX, CELL_DY = 202, 138
GRID_X, GRID_Y = 20, 196


def box_page():
    w = header("box")
    w += [
        value(24, 128, "za.box.view1", 6, TEXT, text="BOÎTE ",
              max_bind="za.box.count", max_sep=" / "),
        value(420, 134, "za.box.view.used", 4, DIM, pad=2),
        label(420 + 2 * 16, 134, f" / {BOX_SLOTS} PLACES", 4, DIM),
        value(700, 134, "za.box.current1", 4, DIM, text="JEU ",
              max_bind="za.box.count", max_sep=" / "),
        button(960, 124, 118, 56, "PREC", "box_prev", wid="box_prev", scale=3),
        button(1090, 124, 126, 56, "SUIV", "box_next", wid="box_next", scale=3),
    ]
    grid = {"repeat": BOX_SLOTS, "repeat_cols": 6,
            "repeat_dx": CELL_DX, "repeat_row_dy": CELL_DY}
    present = {"need_bind": "za.b{i}.v"}
    w += [rect(GRID_X,GRID_Y,CELL_W,CELL_H,bg=PANEL,color=LINE,frame=2,**grid),
          image(GRID_X+46,GRID_Y+2,100,100,src_format="module:icon/%d",bind="za.b{i}.icon",**grid,**present),
          bound_text(GRID_X+10,GRID_Y+102,"za.b{i}.name",4,TEXT,wrap_width=170,max_lines=1,fit_text=True,**grid,**present),
          value(GRID_X+10,GRID_Y+10,"za.b{i}.lvl",3,DIM,text="N.",**grid,**present),
          rect(GRID_X+164,GRID_Y+10,12,12,bg=GOLD,frame=0,need_bind="za.b{i}.shiny",**grid),
          label(GRID_X+CELL_W//2,GRID_Y+60,"VIDE",3,DIM,align="center",hide_bind="za.b{i}.v",hide_eq=1,**grid)]
    w += [
        value(24, 908, "za.box.used", 4, DIM, text="TOTAL "),
        value(300, 908, "za.box.count", 4, DIM, text="BOÎTES "),
        value(560, 908, "za.box.unlocked", 4, DIM, text="DÉBLOQUÉES "),
        label(820, 908, "PREC ET SUIV CHANGENT DE BOITE", 3, DIM),
    ]
    w += not_ready() + tabbar("box")
    return {"id": "box", "title": "Boite",
            "nav_order": nav_order("box_prev", "box_next"), "widgets": w}


# ---- CARTE ----------------------------------------------------------------------------------

RADAR_X, RADAR_Y, RADAR_W, RADAR_H = 96, 300, 1048, 560
DOT = 18


def map_page():
    w = header("map")
    w += [
        label(40, 134, "PLAN D ILLUMIS", 5, GOLD),
        image(40, 200, 640, 640, src="module:map"),
        rect(-9,-9,18,18,bg=BLUE,color=TEXT,frame=3,x_bind="za.pmx",y_bind="za.pmy",need_bind="za.map.marker"),
        label(740, 210, "SECTEUR", 3, DIM),
        bound_text(740, 242, "za.map", 5, TEXT),
        bound_text(740, 320, "za.map.status", 3, DIM, wrap_width=450,max_lines=2),
        value(740, 366, "za.xi", 4, TEXT, text="X "),
        value(740, 412, "za.yi", 4, TEXT, text="Y "),
        value(740, 458, "za.zi", 4, TEXT, text="Z "),
        button(740,540,450,58,"CARTE DU JEU","game_map",wid="game_map",scale=4),
        button(740,612,450,58,"DESTINATIONS","game_destinations",wid="game_destinations",scale=4,hide_bind="za.travel.active",hide_eq=1),
        button(740,612,450,58,"ANNULER LE VOYAGE","travel_cancel",scale=4,need_bind="za.travel.active"),
        button(740,684,218,58,"ZOOM +","game_zoom_in",wid="game_zoom_in",scale=4),
        button(972,684,218,58,"ZOOM -","game_zoom_out",wid="game_zoom_out",scale=4),
        label(740, 768, "CURSEUR DE LA CARTE DU JEU", 3, DIM),
        button(740,806,140,42,"GAUCHE","game_left",scale=3),
        button(895,806,140,42,"HAUT","game_up",scale=3),
        button(1050,806,140,42,"DROITE","game_right",scale=3),
        button(740,864,140,42,"RETOUR","game_back",scale=3),
        button(895,864,140,42,"BAS","game_down",scale=3),
        button(1050,864,140,42,"VALIDER","game_confirm",scale=3),
        bound_text(40, 916, "za.travel.status", 4, GOLD, wrap_width=1188,max_lines=1),
    ]
    for i in range(20):
        gate=dict(need_bind=f"za.zone{i}.v",x_bind=f"za.zone{i}.x",y_bind=f"za.zone{i}.y")
        w += [rect(-12,-12,24,24,bg="#E0156042",color="#FF41E995",frame=2,**gate),
              value(0,-8,f"za.zone{i}.number",3,"#FFFFFFB0",align="center",**gate)]
    for i in range(12):
        gate=dict(need_bind=f"za.center{i}.v",x_bind=f"za.center{i}.x",y_bind=f"za.center{i}.y")
        w += [image(-14,-14,28,28,src="module:center",**gate),
              button(-19,-19,38,38,"",f"center_pick_{i}",wid=f"center_{i}",bg=CLEAR,color=CLEAR,**gate)]
    # The player stays above zones and destination icons.
    w += [rect(-9,-9,18,18,bg=BLUE,color=TEXT,frame=3,x_bind="za.pmx",y_bind="za.pmy",need_bind="za.map.marker")]
    popup=dict(need_bind="za.center.open")
    w += [rect(0,108,W,850,bg="#CC070A11",frame=0,input_block=True,**popup),
          panel(250,340,740,330),]
    # Gate every popup surface so an unopened destination leaves the map visible.
    w[-1]["need_bind"]="za.center.open"
    w += [bound_text(620,380,"za.center.name",5,TEXT,align="center",**popup),
          label(620,440,"DESTINATION DU VOYAGE RAPIDE",3,DIM,align="center",**popup),
          button(290,500,660,58,"SE TÉLÉPORTER ICI","center_travel",scale=5,**popup),
          button(440,584,360,52,"ANNULER","center_close",scale=4,**popup)]
    w += not_ready() + tabbar("map")
    return {"id": "map", "title": "Carte", "nav_order": nav_order(), "widgets": w}


# ---- MISSIONS ------------------------------------------------------------------------------

QUEST_CATEGORIES=["PRINCIPALES","EXTRAS","SECONDAIRES"]
QUEST_STATES=["ÉTAT INCONNU", "DISPONIBLE", "EN COURS", "TERMINÉE", "À DÉCOUVRIR"]


def missions_page():
    w=header("missions")
    w += [bound_text(24,132,"za.quest.status",5,DIM,wrap_width=1188,max_lines=1,fit_text=True)]
    for i,caption in enumerate(QUEST_CATEGORIES):
        x=24+i*408
        w += [button(x,190,388,64,caption,f"quest_category_{i}",wid=f"quest_category_{i}",scale=6),
              rect(x,190,388,64,color=GOLD,frame=3,hide_bind="za.quest.category",keep_min=i,keep_max=i)]
    grid={"repeat":4,"repeat_cols":1,"repeat_dx":0,"repeat_row_dy":154}
    gate={"need_bind":"za.q{i}.v"}
    w += [rect(24,284,1188,140,bg=PANEL,color=LINE,**grid,**gate),
          bound_text(44,296,"za.q{i}.title",7,TEXT,wrap_width=1140,max_lines=1,hide_bind="za.q{i}.state",hide_eq=3,**grid,**gate),
          bound_text(44,296,"za.q{i}.title",7,DIM,wrap_width=1140,max_lines=1,hide_bind="za.q{i}.state",keep_min=3,keep_max=3,**grid,**gate),
          rect(24,284,1188,140,color=GOLD,frame=4,hide_bind="za.q{i}.state",keep_min=2,keep_max=2,**grid,**gate),
          bound_text(44,346,"za.q{i}.description",6,DIM,wrap_width=1140,max_lines=1,**grid,**gate),
          value(44,394,"za.q{i}.state",5,DIM,names=QUEST_STATES,hide_bind="za.q{i}.state",hide_eq=2,**grid,**gate),
          value(44,394,"za.q{i}.state",5,GOLD,names=QUEST_STATES,hide_bind="za.q{i}.state",keep_min=2,keep_max=2,**grid,**gate),
          button(24,284,1188,140,"","quest_pick",wid="quest_row_{i}",bg=CLEAR,color=CLEAR,border=0,payload="{i}",**grid,**gate),
          value(24,914,"za.quest.rows",5,DIM,text="MISSIONS "),
          value(400,910,"za.quest.page",5,TEXT,text="PAGE ",max_bind="za.quest.pages",max_sep=" / "),
          button(850,900,170,44,"PRÉCÉDENT","quest_prev",wid="quest_prev",scale=4),
          button(1038,900,174,44,"SUIVANT","quest_next",wid="quest_next",scale=4)]
    w += not_ready()+tabbar("missions")
    return {"id":"missions","title":"Missions","nav_order":nav_order(
        *[f"quest_category_{i}" for i in range(3)],"quest_prev","quest_next",*[f"quest_row_{i}" for i in range(4)]),"widgets":w}


def mission_detail_page():
    w=header("missions")
    w += [bound_text(32,140,"za.quest.detail.title",8,TEXT,wrap_width=1176,max_lines=2,fit_text=True),
          value(32,258,"za.quest.detail.state",5,GOLD,names=QUEST_STATES),
          panel(24,320,1188,250),
          bound_text(48,344,"za.quest.detail.description",6,TEXT,wrap_width=1120,max_lines=128,line_gap=10,scroll="quest.description"),
          label(32,600,"OBJECTIFS",5,GOLD),
          bound_text(48,648,"za.quest.detail.objectives",6,TEXT,wrap_width=1120,max_lines=128,line_gap=8,scroll="quest.objectives"),
          button(24,888,420,58,"RETOUR AUX MISSIONS","go_missions",wid="quest_back",scale=4)]
    w += tabbar("missions")
    return {"id":"mission_detail","title":"Détail de mission","nav_order":nav_order("quest_back"),"scrolls":[
        {"id":"quest.description","rect":[40,336,1152,218],"count_bind":"za.quest.detail.description.rows","row_h":40,"pad":12,"reset_bind":"za.quest.detail.title","bar":GOLD,"bar_w":6},
        {"id":"quest.objectives","rect":[40,640,1152,216],"count_bind":"za.quest.detail.objectives.rows","row_h":40,"pad":12,"reset_bind":"za.quest.detail.title","bar":GOLD,"bar_w":6}],"widgets":w}


# ---- manifest -------------------------------------------------------------------------------

actions = {f"go_{pid}": {"kind": "page", "page": pid, "transition": "fade", "duration_ms": 150}
           for pid, _ in TABS}
actions["box_prev"] = {"kind": "module", "action": "box_prev"}
actions["box_next"] = {"kind": "module", "action": "box_next"}
actions["box_follow"] = {"kind": "module", "action": "box_follow"}
actions["refresh"] = {"kind": "module", "action": "refresh"}
actions["team_select"]={"kind":"module","action":"team_select","argument":"$payload"}
actions["quest_pick"]={"kind":"module","action":"quest_pick","argument":"$payload"}
actions["dex_pick"]={"kind":"module","action":"dex_pick","argument":"$payload"}
actions["send_out"]={"kind":"module","action":"send_out"}
for command,token in [("left","DLeft"),("right","DRight"),("send","DUp"),("map_open","Plus"),("map_destinations","Y"),("map_up","DUp"),("map_down","DDown"),("map_confirm","A")]:
    actions[f"input_{command}"]={"kind":"button","button":token,"frames":4 if command!="send" else 8,"enabled_bind":f"za.cmd.{command}"}
for name,token,frames in [("party_left","DLeft",8),("party_right","DRight",8),("game_map","Plus",10),("game_back","B",10),("game_zoom_in","RY+",30),("game_zoom_out","RY-",30),("game_left","LX-",30),("game_right","LX+",30),("game_up","LY+",30),("game_down","LY-",30),("game_confirm","A",10),("game_destinations","Y",10)]:
    actions[name]={"kind":"button","button":token,"frames":frames}
for i in range(3):
    actions[f"quest_category_{i}"]={"kind":"module","action":"quest_category","argument":i}
for action in ["quest_prev","quest_next"]:
    actions[action]={"kind":"module","action":action}
for gen in range(1,10):
    actions[f"dex_gen_{gen}"]={"kind":"module","action":"dex_gen","argument":gen}
for f in range(5):
    actions[f"dex_filter_{f}"]={"kind":"module","action":"dex_filter","argument":f}
for i in range(10):
    actions[f"bag_category_{i}"]={"kind":"module","action":"bag_category","argument":i-1}
for action in ["dex_prev","dex_next","bag_prev","bag_next"]:
    actions[action]={"kind":"module","action":action}

for i in range(12):
    actions[f"center_pick_{i}"]={"kind":"module","action":"center_pick","argument":i}
actions["center_travel"]={"kind":"module","action":"center_travel"}
actions["travel_cancel"]={"kind":"module","action":"travel_cancel"}
actions["center_close"]={"kind":"module","action":"center_close"}

# ---- 0.9: persistent detail panes and touch scrolling ------------------------
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
          bound_text(46,332,"za.q{i}.description",5,DIM,wrap_width=1140,max_lines=2,line_gap=4,**grid,**gate),
          value(46,382,"za.q{i}.state",5,DIM,names=QUEST_STATES,hide_bind="za.q{i}.state",hide_eq=2,**grid,**gate),
          value(46,382,"za.q{i}.state",5,GOLD,names=QUEST_STATES,hide_bind="za.q{i}.state",keep_min=2,keep_max=2,**grid,**gate),
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
           label(736,330,"Touche une zone verte\npour voir ses Pokémon.",5,DIM,hide_bind="za.zone.selected",keep_min=0,keep_max=0),
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

_old_box_page=box_page
def box_page():
    page=_old_box_page();w=page["widgets"]
    w=[v for v in w if v.get("text")!="PREC ET SUIV CHANGENT DE BOITE"]
    slot=next(i for i,v in enumerate(w) if v.get("need_bind")=="!za.ready")
    extra=[button(GRID_X,GRID_Y,CELL_W,CELL_H,"","box_pick",payload="{i}",bg=CLEAR,color=CLEAR,border=0,
                  repeat=30,repeat_cols=6,repeat_dx=CELL_DX,repeat_row_dy=CELL_DY,need_bind="za.b{i}.v"),
           button(884,896,328,48,"COMMANDES BOÎTES","go_box_controls",scale=4)]
    gate=dict(need_bind="za.swap.open")
    extra += [rect(0,108,W,850,bg="#F0070A11",frame=0,input_block=True,**gate),
              panel(56,156,1128,784),
              label(86,180,"ÉCHANGE BOÎTE / ÉQUIPE",6,GOLD,**gate),
              image(86,234,106,106,src_format="module:icon/%d",bind="za.swap.icon",**gate),
              bound_text(216,242,"za.swap.name",7,TEXT,wrap_width=936,max_lines=1,fit_text=True,**gate),
              label(216,296,"Ouvre les boîtes du jeu avant de confirmer.",4,DIM,**gate)]
    extra[-5]["need_bind"]="za.swap.open"
    for i in range(6):
        x=86+i%2*538;y=364+i//2*136
        extra += [rect(x,y,514,124,bg=PANEL_HI,color=LINE,**gate),
                  image(x+8,y+8,100,100,src_format="module:icon/%d",bind=f"za.p{i}.icon",**gate),
                  bound_text(x+124,y+20,f"za.p{i}.name",6,TEXT,wrap_width=366,max_lines=1,fit_text=True,**gate),
                  value(x+124,y+72,f"za.p{i}.lvl",4,DIM,text="N. ",**gate),
                  rect(x,y,514,124,color=GOLD,frame=3,hide_bind="za.swap.party",keep_min=i,keep_max=i,**gate),
                  button(x,y,514,124,"","box_swap_pick",payload=i,bg=CLEAR,color=CLEAR,border=0,**gate)]
    extra += [button(86,786,732,64,"EFFECTUER L'ÉCHANGE","box_swap_confirm",scale=5,**gate),
              button(838,786,322,64,"ANNULER","box_swap_cancel",scale=5,**gate),
              bound_text(86,870,"za.swap.status",5,GOLD,wrap_width=1060,max_lines=2,line_gap=5,**gate)]
    w[slot:slot]=extra;page["widgets"]=w;return page

def box_controls_page():
    w=header("box")+[label(620,140,"COMMANDES DES BOÎTES DU JEU",6,GOLD,align="center")]
    for x,y,text,action in [(450,300,"HAUT","game_box_up"),(120,430,"GAUCHE","game_box_left"),(780,430,"DROITE","game_box_right"),(450,560,"BAS","game_box_down"),
                            (120,720,"Y - PRENDRE","game_destinations"),(450,720,"A - POSER","game_confirm"),(780,720,"B - RETOUR","game_back"),
                            (120,840,"BOÎTE -","game_box_previous"),(450,840,"SECOND ÉCRAN","go_box"),(780,840,"BOÎTE +","game_box_next")]:
        w.append(button(x,y,300,80,text,action,scale=5))
    w+=tabbar("box")
    return dict(id="box_controls",title="Commandes des boîtes",nav_order=nav_order(),widgets=w)

actions["go_box_controls"]={"kind":"page","page":"box_controls"}
for name in ["box_pick","box_swap_pick"]:actions[name]={"kind":"module","action":name,"argument":"$payload"}
for name in ["box_swap_confirm","box_swap_cancel"]:actions[name]={"kind":"module","action":name}
for name,key in [("left","DLeft"),("right","DRight"),("up","DUp"),("down","DDown"),("next","R"),("previous","L")]:
    actions[f"game_box_{name}"]={"kind":"button","button":key,"frames":6}
for name,key in [("left","DLeft"),("right","DRight"),("up","DUp"),("down","DDown"),("pick","Y"),("confirm","A"),("back","B"),("next","R"),("previous","L")]:
    actions[f"input_swap_{name}"]={"kind":"button","button":key,"frames":6 if name in ["left","right","up","down"] else 8,"enabled_bind":f"za.cmd.swap_{name}"}

manifest = {
    "format": 1,
    "title_id": "0100F43008C44000",
    "name": "Pokemon Legends Z-A dual screen",
    "_about": "Six tabs (equipe, pokedex, boite, carte, sac, missions) read from the player's own "
              "save by the native module. Game art and French names are decoded from the player romfs. "
              "Only a redistributable font is included.",
    "min_runtime": 19,
    "font": "file:font/za.mfnt",
    "font_atlas": "file:font/za.png",
    "requires_module": True,
    "canvas_w": W,
    "canvas_h": H,
    "background": BG,
    "nav": {"color": RED, "frame": 4},
    "tables": {"natures": NATURES},
    "actions": actions,
    "page_binds": [                   {"point":"za.quest.open","equals":0,"ready_bind":"za.ready",
                    "when_equal":{"page":"mission_detail","transition":"fade","duration_ms":120},
                    "when_not_equal":{"page":"mission_detail","transition":"fade","duration_ms":120}}],
    "enforce":[{"action":f"input_{command}","every_ms":1} for command in ["left","right","send","map_open","map_destinations","map_up","map_down","map_confirm"]]+[{"action":f"input_swap_{name}","every_ms":1} for name in ["left","right","up","down","pick","confirm","back","next","previous"]],
    "pages": [team_page(), dex_page(), dex_detail_page(), box_page(), map_page(), bag_page(), missions_page(), mission_detail_page(),box_controls_page()],
}

def round_texture(bg, edge, border, radius=12):
    name=hashlib.sha256(f"{bg}/{edge}/{border}/{radius}".encode()).hexdigest()[:16]+".png"
    directory=OUT.parent/"shapes";directory.mkdir(parents=True,exist_ok=True)
    target=directory/name
    def rgba(color):
        n=int(color.lstrip("#"),16);return [(n>>16)&255,(n>>8)&255,n&255,(n>>24)&255]
    fill,frame=rgba(bg),rgba(edge);raw=bytearray()
    for y in range(48):
        raw.append(0)
        for x in range(48):
            qx=max(radius-(x+.5),0,(x+.5)-(48-radius));qy=max(radius-(y+.5),0,(y+.5)-(48-radius))
            distance=math.hypot(qx,qy)-radius
            coverage=min(1,max(0,.5-distance))
            color=frame if border and distance>-border else fill
            raw.extend(color[:3]+[round(color[3]*coverage)])
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    target.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',48,48,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(raw),9))+chunk(b'IEND',b''))
    return "file:shapes/"+name


def round_pages(pages):
    for page in pages:
        out=[]
        for w in page["widgets"]:
            kind=w.get("type");width,height=w["rect"][2:]
            # Keep small accents and the player marker sharp; round the surfaces around them.
            if kind in ("rect","button") and width>=48 and height>=24:
                bg=w.get("bg",CLEAR);edge=w.get("color",CLEAR);border=w.get("border",2) if kind=="button" else w.get("frame",2)
                if bg!=CLEAR or (edge!=CLEAR and border):
                    surface={k:v for k,v in w.items() if k not in {"type","id","text","text_scale","align","on_tap","on_hold","nav","border","color","bg","frame","bind_text","table","max_lines","wrap_width","fit_text","input_block"}}
                    surface.update(type="image",src=round_texture(bg,edge,border),fill="slice",slice=[12,12,12,12],color="#FFFFFFFF")
                    if kind=="rect":
                        if w.get("input_block"):surface["input_block"]=True
                        out.append(surface);continue
                    out.append(surface);w=dict(w,bg=CLEAR,border=0)
            elif kind=="bar":
                radius=max(1,min(6,height//2));bg=w.get("bg",WELL)
                background={k:v for k,v in w.items() if k in {"rect","repeat","repeat_cols","repeat_dx","repeat_dy","repeat_row_dy","need_bind","hide_bind","keep_min","keep_max","hide_eq"}}
                background.update(type="image",src=round_texture(bg,CLEAR,0,radius),fill="slice",slice=[radius]*4,color="#FFFFFFFF")
                out.append(background)
                w=dict(w,bg=CLEAR,image=round_texture(w["color"],CLEAR,0,radius),fill="slice",slice=[radius]*4,frame=0)
            out.append(w)
        page["widgets"]=out


def write_send_arrow():
    raw=bytearray()
    for y in range(64):
        raw.append(0)
        for x in range(64):
            on=(12<=y<=34 and abs(x-31.5)<=y-11) or (27<=x<=36 and 30<=y<=54)
            raw.extend([255,255,255,255 if on else 0])
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    target=OUT.parent/"shapes/send-arrow.png";target.parent.mkdir(parents=True,exist_ok=True)
    target.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',64,64,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(raw),9))+chunk(b'IEND',b''))

write_send_arrow()
round_pages(manifest["pages"])

if __name__ == "__main__":
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(manifest, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print("wrote", OUT)
