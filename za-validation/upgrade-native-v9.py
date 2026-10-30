from pathlib import Path
p=Path('companions/eden-duo-companions-super-mario-odyssey-1.0.0/native/modules/0100F43008C44000.cpp')
s=p.read_text(encoding='utf8')
s=s.replace('#include "za_map.h"','#include "za_map.h"\n#include "za_catalog.h"')
s=s.replace('int bag_category = -1, bag_page = 0;','int bag_category = -1, bag_page = 0, bag_selected=0, map_zone=0;')
s=s.replace('std::vector<int> DexRows(const za::Save& save, int gen, int filter) {','std::vector<int> DexRows(za::Assets& assets,const za::Save& save, int gen, int filter) {')
s=s.replace('    static constexpr int ends[] = {0,151,251,386,493,649,721,809,905,1025};\n','')
s=s.replace('    for (int sp = ends[gen-1]+1; sp <= ends[gen]; ++sp) {','    for (const auto& entry:assets.Pokedex(gen-1)) {\n        const int sp=za::NationalSpecies(entry.species);')
s=s.replace('DexRows(save,','DexRows(inst.assets,save,').replace('DexRows(inst.save,','DexRows(inst.assets,inst.save,')
i=s.index('std::vector<za::Save::Item> BagRows')
s=s[:i]+'''// Publish only the scroll viewport, while row arguments remain absolute indexes.
// This keeps thousands of offscreen text/image bindings out of every 60 Hz snapshot.
std::pair<int,int> VisibleRows(const EdenDsmodHostApi& host,const char* region,int count){
    const auto key=std::string("@scroll_first:")+region;
    const int first=host.get_i64?int(host.get_i64(host.userdata,key.c_str(),0)):0;
    return {std::clamp(first-2,0,std::max(0,count)),std::min(count,std::max(0,first)+16)};
}
int TMMove(int item){for(const auto& tm:za::kTMs)if(tm.item==item)return tm.move;return 0;}

'''+s[i:]
s=s.replace('int quest_category,quest_page,quest_selected,quest_open,map_center;','int quest_category,quest_page,quest_selected,quest_open,map_center,bag_selected,map_zone;')
s=s.replace('save = inst.save;map_center=inst.map_center;','save = inst.save;bag_selected=inst.bag_selected;map_zone=inst.map_zone;map_center=inst.map_center;')
# The two catalogues come from the game personal data, not national-generation ranges.
marker='    if (host->begin_output)'
i=s.index(marker,s.index('void Sample('))
s=s[:i]+'''    const auto dex_rows= DexRows(inst.assets,save,dex_gen,dex_filter);
    if(!dex_rows.empty()&&std::find(dex_rows.begin(),dex_rows.end(),detail)==dex_rows.end())detail=dex_rows.front();
    const auto dex_catalog=inst.assets.Pokedex(dex_gen-1);
    auto dex_entry=[&](int national){for(const auto& e:dex_catalog)if(za::NationalSpecies(e.species)==national)return e;return za::Assets::DexEntry{};};
'''+s[i:]
s=s.replace('    const int internal_detail=InternalId(detail);','    const auto detail_entry=dex_entry(detail);\n    const int internal_detail=detail_entry.species;\n    PublishI64(*host,"za.detail.number",detail_entry.number-(dex_gen==2?232:0));')
s=s.replace('"za.detail.icon",internal_detail*256','"za.detail.icon",internal_detail*256+detail_entry.form')
s=s.replace('inst.assets.SpeciesInfo(internal_detail,0)','inst.assets.SpeciesInfo(internal_detail,detail_entry.form)')
s=s.replace('    const auto dex_rows = DexRows(inst.assets,save, dex_gen, dex_filter);','')
s=s.replace('    for (int i = 0; i < kDexPageSize; ++i) {\n        const auto row = static_cast<size_t>(dex_page * kDexPageSize + i);','    const auto dex_window=VisibleRows(*host,"za_dex",dex_rows.size());\n    for (int i=dex_window.first;i<dex_window.second;++i) {\n        const auto row = static_cast<size_t>(i);')
s=s.replace('        const int internal = InternalId(sp);','        const auto entry=dex_entry(sp);const int internal=entry.species;')
s=s.replace('PublishI64(*host,point("icon"),internal * 256);','PublishI64(*host,point("icon"),internal * 256+entry.form);\n        PublishI64(*host,point("number"),entry.number-(dex_gen==2?232:0));\n        PublishI64(*host,point("selected"),sp==detail);')
s=s.replace('    for (int i=0;i<16;++i) {\n        const auto row=static_cast<size_t>(bag_page*kBagPageSize+i);\n        const za::Save::Item item=i<kBagPageSize&&row<bag_rows.size()?bag_rows[row]:za::Save::Item{};', '''    PublishI64(*host,"za.bag.reset",bag_category+1);
    auto picked=std::find_if(bag_rows.begin(),bag_rows.end(),[&](const auto& e){return e.id==bag_selected;});
    if(picked==bag_rows.end()&&!bag_rows.empty())picked=bag_rows.begin();
    const auto picked_item=picked!=bag_rows.end()?*picked:za::Save::Item{};
    const int tm_move=TMMove(picked_item.id);
    auto picked_name=inst.assets.ItemName(picked_item.id);
    if(tm_move)picked_name+=" - "+inst.assets.MoveName(tm_move);
    PublishText(*host,"za.item.name",picked_name);
    PublishI64(*host,"za.item.id",picked_item.id);PublishI64(*host,"za.item.count",picked_item.count);
    PublishI64(*host,"za.item.type",tm_move?inst.assets.MoveType(tm_move):-1);
    const auto description=tm_move?inst.assets.MoveDescription(tm_move):inst.assets.ItemDescription(picked_item.id);
    PublishText(*host,"za.item.description",description.empty()?"Description indisponible dans les ressources chargées.":description);
    PublishI64(*host,"za.item.lines",std::max(1,int(description.size()/44)+3));
    const auto bag_window=VisibleRows(*host,"za_bag",bag_rows.size());
    for (int i=bag_window.first;i<bag_window.second;++i) {
        const auto row=static_cast<size_t>(i);
        const za::Save::Item item=bag_rows[row];''')
s=s.replace('        PublishText(*host,point("name"),display);','        if(const int move=TMMove(item.id))display+=" - "+inst.assets.MoveName(move);\n        PublishI64(*host,point("selected"),item.id==picked_item.id);\n        PublishText(*host,point("name"),display);')
a=s.index('    for(int i=0;i<8;++i){auto row=quest_page*kQuestPageSize+i;');b=s.index('        PublishI64(*host,(prefix+"v")',a)
s=s[:a]+'''    const auto quest_window=VisibleRows(*host,"za_quests",missions.size());
    for(int i=quest_window.first;i<quest_window.second;++i){auto row=i;auto prefix="za.q"+std::to_string(i)+".";bool exists=row<missions.size();
'''+s[b:]
# Zone catalogue with validated factual encounter IDs, names/art from the player's game.
i=s.index('    float map_u=0,map_v=0;')
s=s[:i]+'''    PublishI64(*host,"za.zone.selected",map_zone);
    std::vector<za::ZoneEncounter> encounters;for(const auto& e:za::kZoneEncounters)if(e.zone==map_zone)encounters.push_back(e);
    PublishI64(*host,"za.zone.rows",encounters.size());
    const auto zone_window=VisibleRows(*host,"za_zone",encounters.size());
    for(int i=zone_window.first;i<zone_window.second;++i){const auto& e=encounters[i];const int internal=InternalId(e.species);const auto prefix="za.zp"+std::to_string(i)+".";
        PublishI64(*host,(prefix+"v").c_str(),1);PublishI64(*host,(prefix+"icon").c_str(),internal*256+e.form);
        PublishText(*host,(prefix+"name").c_str(),internal>0&&internal<=species_names.size()?species_names[internal-1]:"");
        PublishText(*host,(prefix+"levels").c_str(),"N. "+std::to_string(e.low)+"-"+std::to_string(e.high)+(e.alpha?" / Baron possible":""));
        PublishI64(*host,(prefix+"state").c_str(),save.owned[e.species]?3:save.dex_state[e.species]);
    }
'''+s[i:]
s=s.replace('auto row=inst.dex_page*kDexPageSize+argument;','auto row=argument;').replace('inst.detail=rows[row];inst.dex_open^=1;','inst.detail=rows[row];')
s=s.replace('auto row=inst.quest_page*kQuestPageSize+argument;','auto row=argument;')
s=s.replace('argument,1,9','argument,1,2')
i=s.index('    if(name=="center_pick")')
s=s[:i]+'''    if(name=="bag_pick"){std::scoped_lock lock{inst.mutex};const auto rows=BagRows(inst.save,inst.bag_category);if(argument<0||argument>=rows.size())return EDEN_DSMOD_FALSE;inst.bag_selected=rows[argument].id;return EDEN_DSMOD_TRUE;}
    if(name=="zone_pick"){const auto zones=inst.assets.Zones();if(argument<0||argument>=zones.size())return EDEN_DSMOD_FALSE;std::scoped_lock lock{inst.mutex};inst.map_zone=zones[argument].number;return EDEN_DSMOD_TRUE;}
    if(name=="map_tap"){const auto x=int(std::uint64_t(argument)>>32),y=int(std::uint32_t(argument));const auto number=inst.assets.MapZoneAt(x,y);if(number>0){std::scoped_lock lock{inst.mutex};inst.map_zone=number;}return EDEN_DSMOD_TRUE;}
'''+s[i:]
p.write_text(s,encoding='utf8')
print('native upgraded')
