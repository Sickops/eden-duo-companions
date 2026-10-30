from pathlib import Path
import re, struct
p=Path(__file__).parent/'reference-v9'
def numbers(s):
    return list(map(int,re.findall(r'\b\d+\b',re.sub(r'//[^\n]*','',s))))
items=(p/'ItemStorage9ZA.cs').read_text(encoding='utf-8-sig')
ids=numbers(items.split('TM =>',1)[1].split('[',1)[1].split(']',1)[0])
moves=numbers((p/'PersonalInfo9ZA.cs').read_text(encoding='utf-8-sig').split('MachineMoves =>',1)[1].split('[',1)[1].split(']',1)[0])
assert len(ids)==len(moves), (len(ids),len(moves))
loc=(p/'text_za_00000_fr.txt').read_text(encoding='utf-8-sig').splitlines()
b=(p/'encounter_za.pkl').read_bytes(); encounters=[]
for i in range(struct.unpack_from('<H',b,2)[0]):
    start,end=struct.unpack_from('<HH',b,4+i*2)
    location=struct.unpack_from('<H',b,start)[0]
    if location>=len(loc) or 'Zone sauvage' not in loc[location]:continue
    zone=int(re.findall(r'\d+',loc[location])[-1]); rows={}
    for at in range(start+4,end,8):
        sp,form,gender,low,high,alpha,shiny=struct.unpack_from('<HBBBBBB',b,at)
        key=(sp,form)
        if key in rows:
            old=rows[key]; low=min(low,old[0]);high=max(high,old[1]);alpha|=old[2]
        rows[key]=(low,high,alpha)
    encounters += [(zone,sp,form,*values) for (sp,form),values in sorted(rows.items())]
out='''// SPDX-License-Identifier: GPL-3.0-or-later
// Factual Z-A TM and encounter metadata derived from kwsch/PKHeX (GPL-3.0).
// Sources: ItemStorage9ZA.cs, PersonalInfo9ZA.cs, encounter_za.pkl,
// text_za_00000_fr.txt; retrieved 2026-10-30. No game art or message text.
#pragma once
namespace za {
struct TMEntry {int item,move;};
inline constexpr TMEntry kTMs[]={
'''
out+='\n'.join(f' {{{i},{m}}},' for i,m in zip(ids,moves))
out+='\n};\nstruct ZoneEncounter {int zone,species,form,low,high,alpha;};\ninline constexpr ZoneEncounter kZoneEncounters[]={\n'
out+='\n'.join(' {'+','.join(map(str,row))+'},' for row in encounters)
out+='\n};\n}\n'
target=Path('companions/eden-duo-companions-super-mario-odyssey-1.0.0/native/modules/za_catalog.h')
target.write_text(out,encoding='utf8');print('TM:',len(ids),'zone entries:',len(encounters),'zones:',len({e[0] for e in encounters}))
