// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Pokémon Legends Z-A (0100F43008C44000) dual-screen module.
//
// Verified build 2.0.2: read-only live party/coordinates through the encrypted SCBlock table.
// Boxes, bag, Pokedex and trainer data come from the player's save. Other builds use that
// snapshot for the party too. Art and French messages are decoded from the player's romfs.
// No game files or memory are modified.
//
// Threading: configure()/sample()/tick() may run on different threads, so the save state sits
// behind one mutex and the file read happens outside it.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include "core/mods/dsmod_module_abi.h"
#include "core/mods/dsmod_module_extensions.h"
#include "core/mods/modules/dsmod_module_sdk.h"

#include "za_save.h"
#include "za_assets.h"
#include "za_live.h"
#include "za_travel.h"
#include "za_map.h"
#include "za_catalog.h"
#include "za_box_swap.h"
#include <memory>

namespace {

using namespace dsmod_sdk;
using namespace dsmod_sdk::int_types;

constexpr u64 kTitleId = 0x0100F43008C44000ull;
constexpr std::size_t kMaxSaveBytes = 16u << 20;
constexpr int kDexPageSize=12;
constexpr int kQuestPageSize=4,kBagPageSize=8;
constexpr u64 kPollTicks = 300; // ~5 s at 60 fps

// The save data directory holds whichever name the title used; probe the plausible ones once and
// keep the one that answered.
constexpr const char* kSaveNames[] = {
    "main", "save", "save_data", "Main", "main.bin", "save.bin", "GameData.bin",
};

struct Instance {
    EdenDsmodHostApi host{};
    EdenDsmodHostSaveApi save_api{};
    za::Assets assets;
    za::LiveReader live_reader;
    za::LiveSnapshot live;
    std::unordered_map<std::uint64_t,za::Entity> confirmed_combat;
    int selected=0, detail=0, dex_open=0, quest_category=0, quest_page=0, quest_selected=0, quest_open=0;
    std::string quest_selected_id;
    std::string send_status;
    za::TravelController travel;
    std::array<bool,5> map_commands{};
    za::BoxSwapController box_swap;
    int swap_slot=-1,swap_party=-1,swap_box=0;
    za::Entity swap_source;
    std::array<bool,9> swap_commands{};
    std::string swap_status;
    int map_center=-1;
    int send_target=-1;
    std::uint64_t command_tick=0,command_next=0,command_deadline=0;
    bool command_left=false,command_right=false,command_send=false;
    std::shared_ptr<const std::vector<std::string>> species_names;
    std::shared_ptr<const std::vector<std::string>> item_names;
    std::shared_ptr<const std::vector<bool>> available_species;
    int dex_gen = 1, dex_filter = 0, dex_page = 0;
    int bag_category = -1, bag_page = 0, bag_selected=0, map_zone=0;

    std::mutex mutex;
    std::mutex poll_mutex;
    za::Save save;
    std::string save_name;
    std::string status = "recherche de la sauvegarde";
    u64 next_poll = 0;
    bool polled_once = false;
    // Box browsing: the page shows one box at a time. Until the player browses, the view follows
    // the box the game itself has selected; after that it stays where the player put it.
    int box_index = 0;
    bool box_follow = true;
    // Cheap change detection: the container's SHA256 trailer changes on every save, so hashing the
    // last 0x20 bytes tells us whether re-decrypting the whole file is worth it.
    u64 last_tail = 0;
    bool have_tail = false;
    std::size_t last_size = 0;
};

u64 TailHash(const u8* tail, std::size_t tail_size, std::size_t file_size) {
    // FNV-1a over the trailer and the size; enough to notice a rewritten save.
    u64 hash = 1469598103934665603ull;
    for (std::size_t i = 0; i < tail_size; ++i) {
        hash ^= tail[i];
        hash *= 1099511628211ull;
    }
    hash ^= static_cast<u64>(file_size);
    return hash * 1099511628211ull;
}

void Log(const EdenDsmodHostApi& host, u32 level, const std::string& message) {
    if (host.log)
        host.log(host.userdata, level, message.c_str());
}

// The save's size, or 0 when the name does not answer (or answers with something that cannot be
// a save). One stat-like call, no payload.
std::size_t SaveFileSize(const Instance& inst, const char* name) {
    if (!inst.save_api.read_save_file)
        return 0;
    const std::size_t size =
        inst.save_api.read_save_file(inst.save_api.userdata, name, 0, nullptr, 0);
    return (size < 0x1000 || size > kMaxSaveBytes) ? 0 : size;
}

// The container's last 0x20 bytes: the SHA256 the game writes over the whole file.
bool ReadSaveTail(const Instance& inst, const char* name, std::size_t size, u8* out) {
    if (!inst.save_api.read_save_file || size < 0x20)
        return false;
    return inst.save_api.read_save_file(inst.save_api.userdata, name, size - 0x20, out, 0x20) ==
           0x20;
}

bool ReadSaveBytes(const Instance& inst, const char* name, std::size_t size,
                   std::vector<u8>& out) {
    out.assign(size, 0);
    const std::size_t read =
        inst.save_api.read_save_file(inst.save_api.userdata, name, 0, out.data(), size);
    if (read != size)
        out.clear();
    return read == size;
}

void Poll(Instance& inst, const EdenDsmodHostApi& host, bool force) {
    std::scoped_lock poll_lock{inst.poll_mutex};
    bool follow;
    int box;
    bool save_valid;
    {
        std::scoped_lock lock{inst.mutex};
        follow = inst.box_follow; box = inst.box_index; save_valid = inst.save.valid;
    }
    const u64 now = host.get_tick ? host.get_tick(host.userdata) : 0;
    if (!force && inst.polled_once && now < inst.next_poll)
        return;
    inst.polled_once = true;
    inst.next_poll = now + kPollTicks;
    if (!inst.save_api.read_save_file)
        return;

    // Which file? The one that answered last time first, then the candidate list.
    std::string name;
    std::size_t size = 0;
    if (!inst.save_name.empty()) {
        size = SaveFileSize(inst, inst.save_name.c_str());
        if (size != 0)
            name = inst.save_name;
    }
    if (name.empty()) {
        for (const char* candidate : kSaveNames) {
            size = SaveFileSize(inst, candidate);
            if (size != 0) {
                name = candidate;
                break;
            }
        }
    }

    // Change detection without reading the payload: the container ends with the SHA256 of its own
    // contents, so the last 0x20 bytes plus the size say whether a save happened. An unchanged save
    // therefore costs two small reads instead of 3 MiB on the timing thread.
    u8 tail_bytes[0x20] = {};
    const bool probed = !name.empty() && ReadSaveTail(inst, name.c_str(), size, tail_bytes);
    const u64 tail = probed ? TailHash(tail_bytes, sizeof(tail_bytes), size) : 0;
    if (!force && probed && inst.have_tail && save_valid && size == inst.last_size &&
        tail == inst.last_tail) {
        std::scoped_lock lock{inst.mutex};
        inst.save_name = name;
        return;
    }

    std::vector<u8> bytes;
    const bool have_bytes = !name.empty() && ReadSaveBytes(inst, name.c_str(), size, bytes);

    za::Save parsed;
    std::string status;
    if (!have_bytes) {
        status = "sauvegarde introuvable";
        parsed.status = status;
    } else {
        parsed = za::Parse(bytes, follow ? -1 : box);
        if (!parsed.valid) {
            status = parsed.status;
        } else if (!parsed.hash_ok) {
            status = "somme de controle invalide";
        } else {
            status = "sauvegarde a jour";
        }
    }

    std::scoped_lock lock{inst.mutex};
    const bool changed = status != inst.status || parsed.valid != inst.save.valid;
    if (parsed.valid)
        inst.box_index = parsed.box_view_index;
    inst.save = std::move(parsed);
    inst.save_name = name;
    inst.status = status;
    inst.have_tail = probed;
    inst.last_tail = tail;
    inst.last_size = size;
    if (changed && host.log) {
        char line[192];
        std::snprintf(line, sizeof(line),
                      "ZA save: %s (%s, %zu octets, %zu blocs, equipe %d, dex %d/%d)",
                      inst.status.c_str(), name.empty() ? "-" : name.c_str(), inst.save.file_size,
                      inst.save.block_count, inst.save.party_count, inst.save.dex_caught,
                      inst.save.dex_seen);
        host.log(host.userdata, EDEN_DSMOD_LOG_INFO, line);
    }
}

std::vector<int> DexRows(za::Assets& assets,const za::Save& save, int gen, int filter) {
    std::vector<int> rows;
    for (const auto& entry:assets.Pokedex(gen-1)) {
        const int sp=za::NationalSpecies(entry.species);
        const bool caught = save.dex_state[sp] == 2 || save.owned[sp] > 0;
        const bool seen = save.dex_state[sp] > 0 || caught;
        if ((filter == 1 && !caught) || (filter == 2 && caught) ||
            (filter == 3 && (!seen || caught)) || (filter == 4 && !save.owned[sp])) continue;
        rows.push_back(sp);
    }
    return rows;
}

// Scroll values are published after module sampling on runtime 19. Publish the
// active collection in full so offscreen rows are present when a fast fling lands;
// the renderer clips images and repeated widgets to the actual viewport.
std::pair<int,int> VisibleRows(const EdenDsmodHostApi& host,const char* region,int count){
    const char* page=host.get_text?host.get_text(host.userdata,"__page"):nullptr;
    const char* expected=std::strcmp(region,"za_dex")==0?"dex":std::strcmp(region,"za_bag")==0?"bag":std::strcmp(region,"za_quests")==0?"missions":"map";
    return {0,!page||std::strcmp(page,expected)==0?count:0};
}
int TMMove(int item){for(const auto& tm:za::kTMs)if(tm.item==item)return tm.move;return 0;}

std::vector<za::Save::Item> BagRows(const za::Save& save, int category) {
    std::vector<za::Save::Item> rows;
    for (const auto& item : save.items)
        if (category < 0 || item.pouch == static_cast<unsigned>(category)) rows.push_back(item);
    return rows;
}

int InternalId(int national) {
    static const auto ids = [] {
        std::array<int, 1026> table{};
        for (int i = 1; i <= 1025; ++i) {
            const auto n = za::NationalSpecies(static_cast<za::u16>(i));
            if (n <= 1025) table[n] = i;
        }
        return table;
    }();
    return national >= 1 && national <= 1025 ? ids[national] : 0;
}

std::uint64_t QuestHash(const std::string& value){std::uint64_t hash=14695981039346656837ull;for(unsigned char c:value){hash^=c;hash*=1099511628211ull;}return hash;}
int MissionState(const za::Assets::Mission& mission,const za::Save& save){
    auto pos=mission.id.find("_title_");if(pos==std::string::npos)return 0;
    auto suffix=mission.id.substr(pos+7);std::string work=mission.work;
    if(!work.empty()){}
    else if(mission.id.starts_with("msg_subquest_")){int id=0;char extra=0;if(std::sscanf(suffix.c_str(),"%d%c",&id,&extra)!=1)return 0;work="SYS_WORK_QUEST_"+std::to_string(id);}
    else if(mission.id.starts_with("msg_mainquest_")){int chapter=0,part=0;char extra=0;if(std::sscanf(suffix.c_str(),"%d_%d%c",&chapter,&part,&extra)!=2)return 0;char key[80];std::snprintf(key,sizeof(key),"SYS_WORK_MAINQUEST_%02d_%02d",chapter,part);work=key;}
    else return 0;
    auto value=save.quest_work.find(QuestHash(work));if(value==save.quest_work.end())return 0;
    if(value->second==255)return 3;if(value->second>=10&&value->second<255)return 2;if(value->second==1)return 1;return 4;
}
std::vector<za::Assets::Mission> MissionRows(Instance& inst,int category,const za::Save& save){auto rows=inst.assets.Missions(category);
    auto rank=[&](const auto& m){switch(MissionState(m,save)){case 2:return 0;case 1:return 1;case 3:return 4;default:return 2;}};
    std::stable_sort(rows.begin(),rows.end(),[&](const auto& a,const auto& b){return rank(a)<rank(b);});return rows;
}

// ---- publishing -------------------------------------------------------------------------------

void PublishText(const EdenDsmodHostApi& host, const char* name, const std::string& value) {
    if (host.publish_text)
        host.publish_text(host.userdata, name, value.c_str());
}

void PublishI64(const EdenDsmodHostApi& host, const char* name, s64 value) {
    if (host.publish_i64)
        host.publish_i64(host.userdata, name, value);
}

void PublishF64(const EdenDsmodHostApi& host, const char* name, double value) {
    if (host.publish_f64)
        host.publish_f64(host.userdata, name, value);
}

void PublishEntity(const EdenDsmodHostApi& host, const char* prefix, int slot,
                   const za::Entity& entity, const std::vector<std::string>& names) {
    char key[48];
    auto name = [&](const char* suffix) {
        std::snprintf(key, sizeof(key), "%s%d.%s", prefix, slot, suffix);
        return key;
    };
    // The game's own French species name, once the name table is in memory (it is decoded on the
    // asset worker, so the first frames show nothing rather than stalling).
    const std::string& sname =
        entity.internal_species >= 1 && static_cast<std::size_t>(entity.internal_species) <= names.size()
            ? names[static_cast<std::size_t>(entity.internal_species) - 1]
            : std::string{};
    PublishText(host, name("sname"), sname);
    PublishI64(host, name("v"), entity.present ? 1 : 0);
    if (!entity.present) {
        PublishText(host, name("name"), "");
        PublishI64(host, name("sp"), 0);
        PublishI64(host, name("lvl"), 0);
        PublishI64(host, name("hp"), 0);
        PublishI64(host, name("hpmax"), 0);
        PublishI64(host, name("g"), 0);
        PublishI64(host, name("shiny"), 0);
        return;
    }
    PublishText(host, name("name"), entity.nickname);
    PublishI64(host, name("sp"), entity.species);
    PublishI64(host, name("nat"), entity.internal_species);
    PublishI64(host, name("icon"), entity.internal_species * 256 + entity.form + (entity.shiny ? 524288 : 0));
    PublishI64(host, name("lvl"), entity.level);
    PublishI64(host, name("hp"), entity.hp);
    PublishI64(host, name("hpmax"), entity.hp_max);
    PublishI64(host, name("g"), entity.gender);
    PublishI64(host, name("shiny"), entity.shiny ? 1 : 0);
    PublishI64(host, name("item"), entity.held_item);
    PublishI64(host, name("form"), entity.form);
    PublishI64(host, name("ball"), entity.ball);
    PublishI64(host, name("nature"), entity.nature);
}

void PublishSlot(const EdenDsmodHostApi& host, int slot, const za::Entity& entity,
                 const std::vector<std::string>& names) {
    PublishEntity(host, "za.p", slot, entity, names);
}

void Sample(void* p, const EdenDsmodHostApi* host) {
    if (!p || !host)
        return;
    auto& inst = *static_cast<Instance*>(p);

    za::Save save;
    std::string status,send_status,travel_status;
    bool swap_active=false;int swap_slot,swap_party,swap_box;za::Entity swap_source;std::string swap_status;std::array<bool,9> swap_commands{};
    bool travel_active=false;
    std::array<bool,5> map_commands{};
    bool box_follow = true;
    int dex_gen, dex_filter, dex_page, bag_category, bag_page, selected, detail, dex_open;
    za::LiveSnapshot live;
    int quest_category,quest_page,quest_selected,quest_open,map_center,bag_selected,map_zone;
    bool command_left,command_right,command_send;
    std::unordered_map<std::uint64_t,za::Entity> confirmed_combat;
    std::string quest_selected_id;
    std::shared_ptr<const std::vector<std::string>> item_names;
    std::shared_ptr<const std::vector<bool>> available;
    std::shared_ptr<const std::vector<std::string>> names;
    {
        std::scoped_lock lock{inst.mutex};
        swap_slot=inst.swap_slot;swap_party=inst.swap_party;swap_box=inst.swap_box;swap_source=inst.swap_source;swap_commands=inst.swap_commands;swap_active=inst.box_swap.Active();swap_status=swap_active||!inst.box_swap.Status().empty()?inst.box_swap.Status():inst.swap_status;
        save = inst.save;bag_selected=inst.bag_selected;map_zone=inst.map_zone;map_center=inst.map_center;send_status=inst.send_status;travel_status=inst.travel.Status();travel_active=inst.travel.Active();map_commands=inst.map_commands;
        command_left=inst.command_left;command_right=inst.command_right;command_send=inst.command_send;
        quest_category=inst.quest_category;quest_page=inst.quest_page;quest_selected=inst.quest_selected;quest_open=inst.quest_open;quest_selected_id=inst.quest_selected_id;
        live=inst.live; confirmed_combat=inst.confirmed_combat; selected=inst.selected; detail=inst.detail; dex_open=inst.dex_open;
        status = inst.status;
        box_follow = inst.box_follow;
        names = inst.species_names;
        item_names = inst.item_names;
        available = inst.available_species;
        dex_gen = inst.dex_gen; dex_filter = inst.dex_filter; dex_page = inst.dex_page;
        bag_category = inst.bag_category; bag_page = inst.bag_page;
    }
    if(!live.quest_work.empty())save.quest_work=live.quest_work;
    if(live.party_valid){std::copy(live.party.begin(),live.party.end(),save.party);save.party_count=live.party_count;}
    if(live.box_valid&&live.box_view_index==save.box_view_index){std::copy(live.box_view.begin(),live.box_view.end(),save.box_view);save.boxes_used[save.box_view_index]=std::count_if(live.box_view.begin(),live.box_view.end(),[](const auto& e){return e.present;});}
    for(auto& entity:save.party){auto hp=confirmed_combat.find((std::uint64_t(entity.pid)<<32)|entity.encryption_constant);if(hp!=confirmed_combat.end()&&hp->second.hp<=entity.hp_max){entity.hp=hp->second.hp;entity.exp=hp->second.exp;}}
    if(live.coordinates_valid){save.map_name=live.area;save.x=live.x;save.y=live.y;save.z=live.z;save.rotation=live.rotation;}
    static const std::vector<std::string> kNoNames;
    const std::vector<std::string>& species_names = names ? *names : kNoNames;

    const auto dex_rows= DexRows(inst.assets,save,dex_gen,dex_filter);
    if(!dex_rows.empty()&&std::find(dex_rows.begin(),dex_rows.end(),detail)==dex_rows.end())detail=dex_rows.front();
    const auto dex_catalog=inst.assets.Pokedex(dex_gen-1);
    auto dex_entry=[&](int national){for(const auto& e:dex_catalog)if(za::NationalSpecies(e.species)==national)return e;return za::Assets::DexEntry{};};
    if (host->begin_output)
        host->begin_output(host->userdata);

    const auto live_hp=std::count(live.hp_valid.begin(),live.hp_valid.end(),true);
    PublishI64(*host,"za.live.ready",live_hp==save.party_count&&live_hp>0);
    PublishI64(*host,"za.map.live",live.position_live?1:0);
    PublishI64(*host,"za.map.mode",live.map_mode);PublishI64(*host,"za.map.destination",live.map_destination);
    PublishText(*host,"za.travel.status",travel_status);PublishI64(*host,"za.travel.active",travel_active);
    constexpr const char* map_keys[]={"za.cmd.map_open","za.cmd.map_destinations","za.cmd.map_up","za.cmd.map_down","za.cmd.map_confirm"};
    for(int i=0;i<5;++i)PublishI64(*host,map_keys[i],map_commands[i]);
    PublishText(*host,"za.map.status",live.position_live?"Position du joueur en direct":"Position de sauvegarde - suivi en direct indisponible");
    PublishI64(*host,"za.cmd.left",command_left);PublishI64(*host,"za.cmd.right",command_right);PublishI64(*host,"za.cmd.send",command_send);
    PublishI64(*host,"za.game.selected",live.selected_index);
    PublishText(*host,"za.send.status",send_status);
    std::string live_status=live.diagnostic,pending;
    if(live.party_valid)for(int i=0;i<save.party_count&&i<6;++i)if(save.party[i].present&&!live.hp_valid[i]){const auto internal=save.party[i].internal_species;
        if(!pending.empty())pending+=", ";pending+=internal>0&&internal<=species_names.size()?species_names[internal-1]:"Pokémon "+std::to_string(i+1);}
    if(!pending.empty())live_status+="\nPV à confirmer : "+pending;
    PublishText(*host,"za.live.status",live_status);
    PublishI64(*host,"za.live.problem",live.diagnostic.starts_with("Build chargé")||live.diagnostic.starts_with("Mémoire :"));
    PublishI64(*host,"za.swap.open",swap_slot>=0);PublishI64(*host,"za.swap.active",swap_active);
    PublishI64(*host,"za.swap.party",swap_party);PublishI64(*host,"za.swap.ready",swap_party>=0&&!swap_active);
    PublishText(*host,"za.swap.status",swap_status);
    PublishText(*host,"za.swap.name",swap_source.nickname);
    PublishI64(*host,"za.swap.icon",swap_source.internal_species*256+swap_source.form+(swap_source.shiny?524288:0));
    constexpr const char* swap_keys[]={"left","right","up","down","pick","confirm","back","next","previous"};
    for(int i=0;i<9;++i)PublishI64(*host,(std::string("za.cmd.swap_")+swap_keys[i]).c_str(),swap_commands[i]);
    PublishI64(*host,"za.box.live",live.box_valid&&live.box_view_index==save.box_view_index);
    PublishI64(*host,"za.box.input",live.boxes.valid);
    PublishI64(*host,"za.box.cursor",live.boxes.cursor);PublishI64(*host,"za.box.party_cursor",live.boxes.party_cursor);
    PublishI64(*host,"za.box.drag.source_box",live.boxes.source_box);PublishI64(*host,"za.box.drag.source_slot",live.boxes.source_slot);
    PublishI64(*host,"za.box.drag.target_box",live.boxes.target_box);PublishI64(*host,"za.box.drag.target_slot",live.boxes.target_slot);
    PublishI64(*host,"za.team.selected",selected);
    const auto& chosen=save.party[selected];
    PublishEntity(*host,"za.selected",0,chosen,species_names);
    // Selected panel uses a non-indexed binding namespace.
    for(int i=0;i<6;++i){const auto& e=save.party[i];const auto info=inst.assets.SpeciesInfo(e.internal_species,e.form);
        const auto low=inst.assets.ExperienceAt(info.growth,e.level),high=inst.assets.ExperienceAt(info.growth,std::min(100,int(e.level)+1));
        const auto base="za.p"+std::to_string(i)+".";
        PublishI64(*host,(base+"xp").c_str(),e.level==100?1:e.exp>=low?std::min(e.exp-low,high-low):0);
        PublishI64(*host,(base+"xpmax").c_str(),e.level==100?1:std::max(1u,high-low));
    }
    PublishI64(*host,"za.sel.v",chosen.present);
    PublishI64(*host,"za.sel.icon",chosen.internal_species*256+chosen.form+(chosen.shiny?524288:0));
    PublishText(*host,"za.sel.name",chosen.nickname);
    PublishText(*host,"za.sel.sname",chosen.internal_species>0 && chosen.internal_species<=species_names.size()?species_names[chosen.internal_species-1]:"");
    PublishI64(*host,"za.sel.lvl",chosen.level);PublishI64(*host,"za.sel.hp",chosen.hp);PublishI64(*host,"za.sel.hpmax",chosen.hp_max);
    PublishI64(*host,"za.sel.sp",chosen.species);PublishI64(*host,"za.sel.g",chosen.gender);PublishI64(*host,"za.sel.nature",chosen.nature);PublishI64(*host,"za.sel.item",chosen.held_item);
    const auto selected_info=inst.assets.SpeciesInfo(chosen.internal_species,chosen.form);
    PublishI64(*host,"za.sel.type1",selected_info.type1);PublishI64(*host,"za.sel.type2",selected_info.type2==selected_info.type1?-1:selected_info.type2);
    PublishI64(*host,"za.sel.moves_ready",chosen.present);
    for(int i=0;i<4;++i){const auto prefix="za.sel.m"+std::to_string(i)+".";
        PublishI64(*host,(prefix+"id").c_str(),chosen.moves[i]);
        PublishText(*host,(prefix+"name").c_str(),inst.assets.MoveName(chosen.moves[i]));
        PublishI64(*host,(prefix+"type").c_str(),inst.assets.MoveType(chosen.moves[i]));
        PublishI64(*host,(prefix+"pp").c_str(),chosen.move_pp[i]);
        PublishI64(*host,(prefix+"ppmax").c_str(),0);
    }
    PublishI64(*host,"za.dex.open",dex_open);
    const auto detail_entry=dex_entry(detail);
    const int internal_detail=detail_entry.species;
    PublishI64(*host,"za.detail.number",detail_entry.number-(dex_gen==2?232:0));
    PublishI64(*host,"za.detail.sp",detail);PublishI64(*host,"za.detail.icon",internal_detail*256+detail_entry.form);
    PublishText(*host,"za.detail.name",internal_detail>0 && internal_detail<=species_names.size()?species_names[internal_detail-1]:"");
    PublishI64(*host,"za.detail.state",save.owned[detail]?3:save.dex_state[detail]);PublishI64(*host,"za.detail.owned",save.owned[detail]);
    const auto dex_info=inst.assets.SpeciesInfo(internal_detail,detail_entry.form);
    PublishI64(*host,"za.detail.type1",dex_info.type1);PublishI64(*host,"za.detail.type2",dex_info.type1==dex_info.type2?-1:dex_info.type2);
    PublishText(*host,"za.detail.height","");PublishText(*host,"za.detail.weight","");
    const auto dex_description=inst.assets.SpeciesDescription(internal_detail);
    PublishText(*host,"za.detail.description",dex_description);PublishI64(*host,"za.detail.lines",std::max(1,int(dex_description.size()/48)+3));
    const auto missions=MissionRows(inst,quest_category,save);
    const int quest_pages=std::max(1,int((missions.size()+kQuestPageSize-1)/kQuestPageSize));quest_page=std::clamp(quest_page,0,quest_pages-1);
    PublishI64(*host,"za.quest.category",quest_category);
    PublishI64(*host,"za.quest.rows",missions.size());PublishI64(*host,"za.quest.page",quest_page+1);PublishI64(*host,"za.quest.pages",quest_pages);
    PublishText(*host,"za.quest.status",save.quest_work.empty()?"Progression indisponible":"Missions actives en premier - touche une mission pour ses details");
    PublishI64(*host,"za.quest.open",quest_open);
    const auto quest_window=VisibleRows(*host,"za_quests",missions.size());
    for(int i=quest_window.first;i<quest_window.second;++i){auto row=i;auto prefix="za.q"+std::to_string(i)+".";bool exists=row<missions.size();
        PublishI64(*host,(prefix+"v").c_str(),exists);PublishI64(*host,(prefix+"state").c_str(),exists?MissionState(missions[row],save):0);
        PublishText(*host,(prefix+"title").c_str(),exists?missions[row].title:"");
        PublishText(*host,(prefix+"description").c_str(),exists?missions[row].description:"");
    }
    if(!missions.empty()){
        auto selected_mission=std::find_if(missions.begin(),missions.end(),[&](const auto& m){return m.id==quest_selected_id;});
        const auto& mission=selected_mission!=missions.end()?*selected_mission:missions.front();
        PublishText(*host,"za.quest.detail.title",mission.title);PublishText(*host,"za.quest.detail.description",mission.description);
        auto text_rows=[](const std::string& text){int rows=1,column=0;for(unsigned char ch:text){if(ch=='\n'){++rows;column=0;}else if((ch&0xC0)!=0x80&&++column>=64){++rows;column=0;}}return rows+1;};
        PublishI64(*host,"za.quest.detail.description.rows",text_rows(mission.description));
        PublishI64(*host,"za.quest.detail.objectives.rows",text_rows(mission.objectives));
        PublishText(*host,"za.quest.detail.objectives",mission.objectives);PublishI64(*host,"za.quest.detail.state",MissionState(mission,save));
    }
    PublishI64(*host, "za.assets", names ? 1 : 0);
    PublishI64(*host, "za.ready", save.valid ? 1 : 0);
    PublishI64(*host, "za.hash", save.hash_ok ? 1 : 0);
    PublishText(*host, "za.status", status);

    PublishText(*host, "za.ot", save.trainer.name);
    PublishI64(*host, "za.id", static_cast<s64>(save.trainer.id32));
    PublishI64(*host, "za.tid", save.trainer.tid);
    PublishI64(*host, "za.sid", save.trainer.sid);
    PublishI64(*host, "za.gender", save.trainer.gender);
    PublishI64(*host, "za.lang", save.trainer.language);
    PublishI64(*host, "za.money", static_cast<s64>(save.money));
    PublishI64(*host, "za.play", static_cast<s64>(save.play_seconds));
    PublishI64(*host, "za.play.h", static_cast<s64>(save.play_seconds / 3600));
    PublishI64(*host, "za.play.m", static_cast<s64>((save.play_seconds / 60) % 60));
    // The save's clock: seconds past midnight, and the same split into hours and minutes so the
    // page can print "14:07" with two padded values.
    PublishI64(*host, "za.day", static_cast<s64>(save.day_seconds));
    PublishI64(*host, "za.hour", static_cast<s64>(save.day_seconds) / 3600);
    PublishI64(*host, "za.minute", (static_cast<s64>(save.day_seconds) / 60) % 60);

    PublishI64(*host, "za.party.count", save.party_count);
    for (int i = 0; i < static_cast<int>(za::kPartySlots); ++i)
        PublishSlot(*host, i, save.party[i], species_names);

    PublishI64(*host, "za.dex.caught", save.dex_caught);
    PublishI64(*host, "za.dex.seen", save.dex_seen);
    PublishI64(*host, "za.dex.known", save.dex_caught + save.dex_seen);
    PublishI64(*host, "za.dex.total", 1025);
    {
        // Per-generation caught counts and their National Dex ceilings, so the Pokédex page can
        // draw one bar per generation with the plain `value` widget (max_bind + max_sep).
        static constexpr int kGenSize[9] = {151, 100, 135, 107, 156, 72, 88, 96, 120};
        char key[24];
        for (int gen = 0; gen < 9; ++gen) {
            std::snprintf(key, sizeof(key), "za.dex.g%d", gen);
            PublishI64(*host, key, save.dex_caught_by_gen[gen]);
            std::snprintf(key, sizeof(key), "za.dex.g%d.max", gen);
            PublishI64(*host, key, kGenSize[gen]);
        }
    }


    const int dex_pages = std::max(1, static_cast<int>((dex_rows.size() + kDexPageSize-1) / kDexPageSize));
    dex_page = std::clamp(dex_page, 0, dex_pages - 1);
    PublishI64(*host,"za.dex.reset",dex_gen*8+dex_filter);
    PublishI64(*host, "za.dex.gen", dex_gen);
    PublishI64(*host, "za.dex.filter", dex_filter);
    PublishI64(*host, "za.dex.rows", dex_rows.size());
    PublishI64(*host, "za.dex.page", dex_page + 1);
    PublishI64(*host, "za.dex.pages", dex_pages);
    const auto dex_window=VisibleRows(*host,"za_dex",dex_rows.size());
    for (int i=dex_window.first;i<dex_window.second;++i) {
        const auto row = static_cast<size_t>(i);
        const int sp = row < dex_rows.size() ? dex_rows[row] : 0;
        const auto entry=dex_entry(sp);const int internal=entry.species;
        char key[48];
        auto point = [&](const char* suffix) { std::snprintf(key,sizeof(key),"za.d%d.%s",i,suffix);return key; };
        PublishI64(*host,point("v"),sp ? 1 : 0);
        PublishI64(*host,point("sp"),sp);
        PublishI64(*host,point("icon"),internal * 256+entry.form);
        PublishI64(*host,point("number"),entry.number-(dex_gen==2?232:0));
        PublishI64(*host,point("selected"),sp==detail);
        PublishI64(*host,point("owned"),save.owned[sp]);
        PublishI64(*host,point("state"),save.owned[sp] ? 3 : save.dex_state[sp]);
        PublishI64(*host,point("available"),available && internal < static_cast<int>(available->size()) && (*available)[internal]);
        const auto display = internal > 0 && internal <= static_cast<int>(species_names.size())
            ? species_names[internal-1] : "Pokemon " + std::to_string(sp);
        PublishText(*host,point("name"),sp ? display : "");
    }
    const auto bag_rows = BagRows(save, bag_category);
    const int bag_pages = std::max(1, static_cast<int>((bag_rows.size() + kBagPageSize-1) / kBagPageSize));
    bag_page = std::clamp(bag_page, 0, bag_pages-1);
    PublishI64(*host,"za.bag.total",save.items.size());
    PublishI64(*host,"za.bag.rows",bag_rows.size());
    PublishI64(*host,"za.bag.category",bag_category + 1);
    PublishI64(*host,"za.bag.page",bag_page + 1);
    PublishI64(*host,"za.bag.pages",bag_pages);
    PublishI64(*host,"za.bag.reset",bag_category+1);
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
        const za::Save::Item item=bag_rows[row];
        char key[48];
        auto point=[&](const char* suffix){std::snprintf(key,sizeof(key),"za.i%d.%s",i,suffix);return key;};
        PublishI64(*host,point("v"),item.id ? 1 : 0);
        PublishI64(*host,point("id"),item.id);
        PublishI64(*host,point("count"),item.count);
        PublishI64(*host,point("pouch"),item.pouch);
        PublishI64(*host,point("favorite"),(item.flags & 2) != 0);
        PublishI64(*host,point("held"),(item.flags & 16) != 0);
        std::string display;
        if (item_names && item.id < item_names->size()) display=(*item_names)[item.id];
        if (display.empty() && item.id) display="Objet " + std::to_string(item.id);
        if(const int move=TMMove(item.id))display+=" - "+inst.assets.MoveName(move);
        PublishI64(*host,point("selected"),item.id==picked_item.id);
        PublishText(*host,point("name"),display);
    }

    PublishI64(*host, "za.box.used", save.box_used);
    PublishI64(*host, "za.box.slots", static_cast<s64>(za::kBoxCount * za::kBoxSlotCount));
    PublishI64(*host, "za.box.unlocked", save.boxes_unlocked);
    PublishI64(*host, "za.box.current", save.current_box);
    PublishI64(*host, "za.box.current1", save.current_box + 1);
    PublishI64(*host, "za.box.count", save.boxes_available);
    PublishI64(*host, "za.box.view", save.box_view_index);
    PublishI64(*host, "za.box.view1", save.box_view_index + 1);
    PublishI64(*host, "za.box.view.used", save.boxes_used[save.box_view_index]);
    PublishI64(*host, "za.box.follow", box_follow ? 1 : 0);
    for (int i = 0; i < static_cast<int>(za::kBoxSlotCount); ++i)
        PublishEntity(*host, "za.b", i, save.box_view[i], species_names);

    PublishText(*host, "za.map", save.map_name);
    PublishText(*host, "za.street", save.street);
    PublishF64(*host, "za.x", save.x);
    PublishF64(*host, "za.y", save.y);
    PublishF64(*host, "za.z", save.z);
    PublishF64(*host, "za.rot", save.rotation);
    // Whole metres, for the `value` widget (which shows integers only).
    PublishI64(*host, "za.xi", static_cast<s64>(std::lround(save.x)));
    PublishI64(*host, "za.yi", static_cast<s64>(std::lround(save.y)));
    PublishI64(*host, "za.zi", static_cast<s64>(std::lround(save.z)));

    auto zones=inst.assets.Zones();auto centers=inst.assets.Centers();
    for(int i=0;i<20;++i){auto prefix="za.zone"+std::to_string(i)+".";const bool exists=i<zones.size();
        PublishI64(*host,(prefix+"v").c_str(),exists);PublishI64(*host,(prefix+"number").c_str(),exists?zones[i].number:0);
        PublishF64(*host,(prefix+"x").c_str(),exists?40+(2048+zones[i].x)*640/4096:0);
        PublishF64(*host,(prefix+"y").c_str(),exists?200+(2048-zones[i].y)*640/4096:0);
    }
    for(int i=0;i<12;++i){auto prefix="za.center"+std::to_string(i)+".";const bool exists=i<centers.size();float u=0,v=0;
        if(exists)za::MapPoint("t1",centers[i].x,centers[i].z,u,v);
        PublishI64(*host,(prefix+"v").c_str(),exists);PublishI64(*host,(prefix+"destination").c_str(),exists?centers[i].destination:0);PublishF64(*host,(prefix+"x").c_str(),40+u);PublishF64(*host,(prefix+"y").c_str(),200+v);
    }
    PublishI64(*host,"za.center.open",map_center>=0&&map_center<centers.size());
    PublishText(*host,"za.center.name",map_center>=0&&map_center<centers.size()?(!centers[map_center].name.empty()?centers[map_center].name:"Centre Pokémon "+std::to_string(map_center+1)):"");
    PublishI64(*host,"za.zone.selected",map_zone);
    std::vector<za::ZoneEncounter> encounters;for(const auto& e:za::kZoneEncounters)if(e.zone==map_zone)encounters.push_back(e);
    PublishI64(*host,"za.zone.rows",encounters.size());
    const auto zone_window=VisibleRows(*host,"za_zone",encounters.size());
    for(int i=zone_window.first;i<zone_window.second;++i){const auto& e=encounters[i];const int internal=InternalId(e.species);const auto prefix="za.zp"+std::to_string(i)+".";
        PublishI64(*host,(prefix+"v").c_str(),1);PublishI64(*host,(prefix+"icon").c_str(),internal*256+e.form);
        PublishText(*host,(prefix+"name").c_str(),internal>0&&internal<=species_names.size()?species_names[internal-1]:"");
        PublishText(*host,(prefix+"levels").c_str(),"N. "+std::to_string(e.low)+"-"+std::to_string(e.high)+(e.alpha?" / Baron possible":""));
        PublishI64(*host,(prefix+"state").c_str(),save.owned[e.species]?3:save.dex_state[e.species]);
    }
    float map_u=0,map_v=0;
    const bool map_marker=live.position_live&&za::MapPoint(live.area,live.x,live.z,map_u,map_v);
    PublishI64(*host,"za.map.marker",map_marker?1:0);
    PublishF64(*host,"za.nx",map_u/640.0f);PublishF64(*host,"za.ny",map_v/640.0f);
    PublishF64(*host,"za.pmx",40.0f+map_u);PublishF64(*host,"za.pmy",200.0f+map_v);

    if (host->end_output)
        host->end_output(host->userdata);
}

void Tick(void* p, const EdenDsmodHostApi* host) {
    if (!p || !host)
        return;
    try {
        auto& inst=*static_cast<Instance*>(p);
        bool inspect_boxes=false;int requested_box=0;
        {std::scoped_lock lock{inst.mutex};inspect_boxes=inst.swap_slot>=0||inst.box_swap.Active();requested_box=inst.save.box_view_index;}
        auto live=inst.live_reader.Poll(*host,inspect_boxes,requested_box);
        {std::scoped_lock lock{inst.mutex};
            for(int i=0;i<6;++i)if(live.hp_valid[i]){const auto& e=live.party[i];inst.confirmed_combat[(std::uint64_t(e.pid)<<32)|e.encryption_constant]=e;}
            inst.command_left=inst.command_right=inst.command_send=false;++inst.command_tick;
            inst.swap_commands.fill(false);
            if(inst.box_swap.Active()){
                const auto command=inst.box_swap.Poll(inst.command_tick,live);
                if(command!=za::BoxSwapController::Command::None)inst.swap_commands[int(command)-1]=true;
                if(inst.box_swap.Completed())inst.swap_status="Échange confirmé dans le jeu";
            }
            if(inst.send_target>=0){
                if(inst.command_tick>inst.command_deadline){inst.send_target=-1;inst.send_status="Sélection impossible : fermez les menus du jeu.";}
                else if(inst.command_tick>=inst.command_next&&live.selected_index>=0){
                    if(live.selected_index==inst.send_target){inst.command_send=true;inst.send_target=-1;inst.send_status="Commande envoyée au jeu";}
                    else {inst.command_right=live.selected_index<inst.send_target;inst.command_left=!inst.command_right;}
                    inst.command_next=inst.command_tick+18;
                }
            }
            inst.map_commands.fill(false);
            const auto travel=inst.travel.Poll(inst.command_tick,live.map_mode,live.map_destination);
            if(inst.travel.Active()&&(inst.command_tick%120==0||travel!=za::TravelController::Command::None)){char trace[160];std::snprintf(trace,sizeof(trace),"ZA travel: mode=%d destination=%d command=%d",live.map_mode,live.map_destination,int(travel));Log(*host,EDEN_DSMOD_LOG_INFO,trace);}
            switch(travel){case za::TravelController::Command::OpenMap:inst.map_commands[0]=true;break;
                case za::TravelController::Command::Destinations:inst.map_commands[1]=true;break;
                case za::TravelController::Command::Up:inst.map_commands[2]=true;break;
                case za::TravelController::Command::Down:inst.map_commands[3]=true;break;
                case za::TravelController::Command::Confirm:inst.map_commands[4]=true;break;default:break;}
            inst.live=std::move(live);}
        Poll(inst, *host, false);
    } catch (...) {
        // A save the module cannot read is a state, never a crash.
    }
}

EdenDsmodBool SupportsBuild(const char*) {
    // The save container's block list and the PA9 layout are the same across the retail builds the
    // package is for; the manifest's build list decides which ones load it.
    return EDEN_DSMOD_TRUE;
}

void* Create(const EdenDsmodHostApi* host, const char*) {
    if (!dsmod_sdk::HostAbiMatches(host))
        return nullptr;
    auto* inst = new (std::nothrow) Instance;
    if (!inst)
        return nullptr;
    inst->host = *host;
    inst->assets.SetReader(host->userdata, host->read_romfs);
    return inst;
}

void Destroy(void* p) {
    delete static_cast<Instance*>(p);
}

void ConfigureSave(void* p, const EdenDsmodHostSaveApi* host) {
    if (!p || !host || host->version != EDEN_DSMOD_SAVE_EXT_VERSION)
        return;
    auto& inst = *static_cast<Instance*>(p);
    std::scoped_lock poll_lock{inst.poll_mutex};
    std::scoped_lock lock{inst.mutex};
    inst.save_api = *host;
    inst.polled_once = false;
    inst.next_poll = 0;
    inst.have_tail = false;
    inst.last_size = 0;
}

// The base extensions are exported for one reason: `on_action`, so the box page's arrow buttons
// (and the controller's A on them) can browse the player's boxes without the package having to
// juggle flags. Nothing here needs the guest mailbox or the ASTC decoder.
void ConfigureExtensions(void*, const EdenDsmodHostExtensions*) {}

EdenDsmodBool OnAction(void* p, const char* action, int64_t argument) {
    if (!p || !action)
        return EDEN_DSMOD_FALSE;
    auto& inst = *static_cast<Instance*>(p);
    const std::string name = action;

    if(name=="box_pick"){
        std::scoped_lock lock{inst.mutex};if(inst.box_swap.Active()||argument<0||argument>=30)return EDEN_DSMOD_FALSE;
        const auto& source=inst.live.box_valid&&inst.live.box_view_index==inst.save.box_view_index?inst.live.box_view[argument]:inst.save.box_view[argument];
        if(!source.present)return EDEN_DSMOD_FALSE;
        inst.swap_slot=int(argument);inst.swap_box=inst.save.box_view_index;inst.swap_party=-1;inst.swap_source=source;inst.swap_status="Choisissez le Pokémon de l'équipe à échanger.";inst.box_swap=za::BoxSwapController{};return EDEN_DSMOD_TRUE;
    }
    if(name=="box_swap_pick"){std::scoped_lock lock{inst.mutex};if(inst.box_swap.Active()||argument<0||argument>=6||inst.swap_slot<0)return EDEN_DSMOD_FALSE;const auto& e=inst.live.party_valid?inst.live.party[argument]:inst.save.party[argument];if(!e.present)return EDEN_DSMOD_FALSE;inst.swap_party=int(argument);return EDEN_DSMOD_TRUE;}
    if(name=="box_swap_confirm"){
        std::scoped_lock lock{inst.mutex};if(inst.swap_slot<0||inst.swap_party<0||inst.box_swap.Active())return EDEN_DSMOD_FALSE;
        if(!inst.live.party_valid||!inst.live.box_valid||inst.live.box_view_index!=inst.swap_box){inst.swap_status="Données en direct indisponibles : Z-A 2.0.2 requis.";return EDEN_DSMOD_FALSE;}
        const auto& source=inst.live.box_view[inst.swap_slot];
        if(source.encryption_constant!=inst.swap_source.encryption_constant||source.pid!=inst.swap_source.pid){inst.swap_status="La boîte a changé. Sélectionnez à nouveau le Pokémon.";return EDEN_DSMOD_FALSE;}
        inst.travel.Cancel();inst.send_target=-1;
        inst.box_swap.Start(inst.swap_box,inst.swap_slot,inst.swap_party,source,inst.live.party[inst.swap_party],inst.command_tick);return EDEN_DSMOD_TRUE;
    }
    if(name=="box_swap_cancel"){std::scoped_lock lock{inst.mutex};if(inst.box_swap.Active())inst.box_swap.Cancel();else inst.swap_slot=-1;return EDEN_DSMOD_TRUE;}
    if(name=="bag_pick"){std::scoped_lock lock{inst.mutex};const auto rows=BagRows(inst.save,inst.bag_category);if(argument<0||argument>=rows.size())return EDEN_DSMOD_FALSE;inst.bag_selected=rows[argument].id;return EDEN_DSMOD_TRUE;}
    if(name=="zone_pick"){const auto zones=inst.assets.Zones();if(argument<0||argument>=zones.size())return EDEN_DSMOD_FALSE;std::scoped_lock lock{inst.mutex};inst.map_zone=zones[argument].number;return EDEN_DSMOD_TRUE;}
    if(name=="map_tap"){const auto x=int(std::uint64_t(argument)>>32),y=int(std::uint32_t(argument));const auto number=inst.assets.MapZoneAt(x,y);if(number>0){std::scoped_lock lock{inst.mutex};inst.map_zone=number;}return EDEN_DSMOD_TRUE;}
    if(name=="center_pick"){auto centers=inst.assets.Centers();if(argument<0||argument>=centers.size())return EDEN_DSMOD_FALSE;std::scoped_lock lock{inst.mutex};inst.map_center=int(argument);return EDEN_DSMOD_TRUE;}
    if(name=="center_travel"){auto centers=inst.assets.Centers();std::scoped_lock lock{inst.mutex};if(inst.box_swap.Active()||inst.map_center<0||inst.map_center>=centers.size()||centers[inst.map_center].destination<=0)return EDEN_DSMOD_FALSE;
        inst.travel.Start(centers[inst.map_center].destination,inst.command_tick);inst.send_target=-1;inst.map_center=-1;return EDEN_DSMOD_TRUE;}
    if(name=="travel_cancel"){std::scoped_lock lock{inst.mutex};inst.travel.Cancel();inst.map_commands.fill(false);return EDEN_DSMOD_TRUE;}
    if(name=="center_close"){std::scoped_lock lock{inst.mutex};inst.map_center=-1;return EDEN_DSMOD_TRUE;}
    if(name=="send_out"){
        std::scoped_lock lock{inst.mutex};if(inst.box_swap.Active())return EDEN_DSMOD_FALSE;if(inst.live.selected_index<0){inst.send_status="Sélection en jeu indisponible. Vérifiez Z-A 2.0.2.";return EDEN_DSMOD_FALSE;}
        const auto& party=inst.live.party_valid?inst.live.party[inst.selected]:inst.save.party[inst.selected];
        if(!party.present)return EDEN_DSMOD_FALSE;
        inst.travel.Cancel();inst.send_status="Sélection du Pokémon en cours...";
        inst.send_target=inst.selected;inst.command_next=inst.command_tick;inst.command_deadline=inst.command_tick+480;
        return EDEN_DSMOD_TRUE;
    }
    if(name=="team_select"){std::scoped_lock lock{inst.mutex};inst.selected=static_cast<int>(std::clamp<int64_t>(argument,0,5));return EDEN_DSMOD_TRUE;}
    if(name=="dex_pick"){std::scoped_lock lock{inst.mutex};auto rows=DexRows(inst.assets,inst.save,inst.dex_gen,inst.dex_filter);auto row=argument;if(row>=0 && row<rows.size()){inst.detail=rows[row];}return EDEN_DSMOD_TRUE;}
    if(name.starts_with("quest_")){std::scoped_lock lock{inst.mutex};
        if(name=="quest_category"){inst.quest_category=static_cast<int>(std::clamp<int64_t>(argument,0,2));inst.quest_page=0;}
        else if(name=="quest_pick"){
            auto state=inst.save;if(!inst.live.quest_work.empty())state.quest_work=inst.live.quest_work;
            auto rows=MissionRows(inst,inst.quest_category,state);auto row=argument;
            if(row<0||row>=rows.size())return EDEN_DSMOD_FALSE;inst.quest_selected=static_cast<int>(row);inst.quest_selected_id=rows[row].id;inst.quest_open^=1;
        }
        else if(name=="quest_prev")inst.quest_page=std::max(0,inst.quest_page-1);
        else if(name=="quest_next")++inst.quest_page;
        return EDEN_DSMOD_TRUE;}
    if (name.starts_with("dex_") || name.starts_with("bag_")) {
        std::scoped_lock lock{inst.mutex};
        if (name == "dex_gen") { inst.dex_gen=static_cast<int>(std::clamp<int64_t>(argument,1,2)); inst.dex_page=0; }
        else if (name == "dex_filter") { inst.dex_filter=static_cast<int>(std::clamp<int64_t>(argument,0,4)); inst.dex_page=0; }
        else if (name == "dex_prev" || name == "dex_next") {
            const int pages=std::max(1,static_cast<int>((DexRows(inst.assets,inst.save,inst.dex_gen,inst.dex_filter).size()+kDexPageSize-1)/kDexPageSize));
            inst.dex_page=std::clamp(inst.dex_page+(name=="dex_next"?1:-1),0,pages-1);
        } else if (name == "bag_category") { inst.bag_category=static_cast<int>(std::clamp<int64_t>(argument,-1,8)); inst.bag_page=0; }
        else if (name == "bag_prev" || name == "bag_next") {
            const int pages=std::max(1,static_cast<int>((BagRows(inst.save,inst.bag_category).size()+kBagPageSize-1)/kBagPageSize));
            inst.bag_page=std::clamp(inst.bag_page+(name=="bag_next"?1:-1),0,pages-1);
        } else return EDEN_DSMOD_FALSE;
        return EDEN_DSMOD_TRUE;
    }

    int target = -1;
    bool follow = false;
    if (name == "box_prev") {
        std::scoped_lock lock{inst.mutex};
        target = std::max(0, inst.save.box_view_index - 1);
    } else if (name == "box_next") {
        std::scoped_lock lock{inst.mutex};
        const int last = std::max(0, inst.save.boxes_available - 1);
        target = std::min(last, inst.save.box_view_index + 1);
    } else if (name == "box_set") {
        std::scoped_lock lock{inst.mutex};
        target = static_cast<int>(argument);
    } else if (name == "box_follow") {
        follow = true;
    } else if (name == "refresh") {
        // A forced re-read, so the page picks up a save made a moment ago.
    } else {
        return EDEN_DSMOD_FALSE;
    }

    {
        std::scoped_lock lock{inst.mutex};
        if (target >= 0) {
            inst.box_index = target;
            inst.box_follow = false;
        }
        if (follow)
            inst.box_follow = true;
    }

    try {
        Poll(inst, inst.host, true);
    } catch (...) {
        return EDEN_DSMOD_FALSE;
    }
    return EDEN_DSMOD_TRUE;
}

EdenDsmodBool LoadImage(void* p, const EdenDsmodHostApi* asset_host, const char* key,
                        void* receiver, EdenDsmodImageSink sink) {
    if (!p || !key || !sink) return EDEN_DSMOD_FALSE;
    auto& inst = *static_cast<Instance*>(p);
    try {
        za::Picture image;
        bool ok = false;
        if (std::strcmp(key, "module:ready") == 0) {
            ok = inst.assets.EnsureNames();
            image.width=1; image.height=1; image.rgba.assign(4,0);
        } else if (std::strcmp(key, "module:map") == 0) {
            ok = inst.assets.Map(640, image);
        } else if(std::strncmp(key,"module:zone/",12)==0){
            ok=inst.assets.ZoneIcon(std::atoi(key+12),image);
        } else if(std::strcmp(key,"module:center")==0){
            ok=inst.assets.CenterIcon(image);
        } else if(std::strncmp(key,"module:type/",12)==0){
            ok=inst.assets.TypeIcon(std::atoi(key+12),image);
        } else if(std::strncmp(key,"module:item/",12)==0){
            int item=0;char extra=0;if(std::sscanf(key+12,"%d%c",&item,&extra)!=1 || item<1 || item>=3000)return EDEN_DSMOD_FALSE;
            ok=inst.assets.ItemIcon(item,image);
        } else if (std::strncmp(key, "module:icon/", 12) == 0) {
            int species = 0;
            char extra = 0;
            if (std::sscanf(key + 12, "%d%c", &species, &extra) != 1 || species < 1 || species > 524288 + 1025 * 256 + 255)
                return EDEN_DSMOD_FALSE;
            ok = inst.assets.Icon((species & 524287) / 256, species % 256, image, (species & 524288)!=0);
        } else return EDEN_DSMOD_FALSE;
        if (inst.assets.NamesReady()) {
            bool need_names;
            { std::scoped_lock lock{inst.mutex}; need_names = !inst.species_names; }
            if (need_names) {
                auto names = std::make_shared<std::vector<std::string>>();
                names->reserve(1025);
                for (int i = 1; i <= 1025; ++i) names->emplace_back(inst.assets.SpeciesName(i));
                auto items = std::make_shared<std::vector<std::string>>();
                for (int i=0;i<3000;++i) items->push_back(inst.assets.ItemName(i));
                auto available = std::make_shared<std::vector<bool>>(1026,false);
                for (int i=1;i<=1025;++i) (*available)[i]=inst.assets.HasSpecies(i);
                if (asset_host && asset_host->log)
                    asset_host->log(asset_host->userdata,EDEN_DSMOD_LOG_INFO,"ZA assets: archives, noms et catalogue charges");
                std::scoped_lock lock{inst.mutex};
                inst.item_names=std::move(items);
                inst.available_species=std::move(available);
                inst.species_names = std::move(names);
            }
        }
        if (!ok || !image.Valid()) return EDEN_DSMOD_FALSE;
        sink(receiver, image.width, image.height, image.rgba.data(), image.rgba.size());
        return EDEN_DSMOD_TRUE;
    } catch (...) { return EDEN_DSMOD_FALSE; }
}

const EdenDsmodModuleExtensions Extensions{
    EDEN_DSMOD_EXT_VERSION,
    sizeof(EdenDsmodModuleExtensions),
    EDEN_DSMOD_EXT_HASH,
    &ConfigureExtensions,
    &OnAction,
    &LoadImage,
};

const EdenDsmodModuleApi ModuleApi{
    EDEN_DSMOD_MODULE_ABI_VERSION,
    sizeof(EdenDsmodModuleApi),
    0,
    EDEN_DSMOD_MODULE_ABI_HASH,
    kTitleId,
    "Pokemon Legends Z-A DSMod",
    EDEN_DSMOD_CAP_EXTENSIONS | EDEN_DSMOD_CAP_TICK_WHEN_HIDDEN,
    &SupportsBuild,
    &Create,
    &Destroy,
    &Sample,
    &Tick,
};

const EdenDsmodModuleSaveExtensions SaveExtensions{
    EDEN_DSMOD_SAVE_EXT_VERSION,
    sizeof(EdenDsmodModuleSaveExtensions),
    EDEN_DSMOD_SAVE_EXT_HASH,
    &ConfigureSave,
};

} // namespace

DSMOD_SDK_EXPORT_MODULE(ModuleApi)
DSMOD_SDK_EXPORT_EXTENSIONS(Extensions)
// No DSMOD_SDK_EXPORT_SAVE_EXTENSIONS macro exists; the generic getter is the same expansion.
DSMOD_SDK_DEFINE_GETTER(eden_dsmod_get_save_extensions, EdenDsmodModuleSaveExtensions,
                        EDEN_DSMOD_SAVE_EXT_VERSION, EDEN_DSMOD_SAVE_EXT_HASH, SaveExtensions)
