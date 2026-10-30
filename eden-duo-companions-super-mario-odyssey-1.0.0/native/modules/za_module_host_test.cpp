// SPDX-License-Identifier: GPL-3.0-or-later
// Drives the shipped Pokemon Legends Z-A module DLL through its exported C ABI, with a host
// that only offers the save extension pointed at a copy of the player's real save.
// Validates the exact binary the package ships, without booting the game.
#include "core/mods/dsmod_module_abi.h"
#include "core/mods/dsmod_module_extensions.h"

#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <array>
#include <algorithm>
#include <set>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct FakeHost {
    std::map<std::string, long long> ints;
    std::map<std::string, double> floats;
    std::map<std::string, std::string> texts;
    unsigned long long tick = 0;
    std::string save_path;
    std::string romfs;
    int logs = 0;
    int active_slot=-1;
    unsigned long long bytes_read = 0;
};

bool ReadSelection(FakeHost& h,uint64_t address,void* out,size_t size){
    if(h.active_slot<0)return false;std::array<uint8_t,16> bytes{};uint64_t value=0;
    if(address==0x100000+0x48891E0&&size==8)value=0x80000;
    else if(address==0x80000&&size==8)value=0x100000+0x3DE1A20;
    else if(address==0x800B8&&size==8)value=0x81000;
    else if(address==0x81368&&size==8)value=0x82000;
    else if(address==0x82000&&size==16){value=0x100000+0x3BD3B30;std::memcpy(bytes.data(),&value,8);uint32_t index=h.active_slot,count=6;std::memcpy(bytes.data()+8,&index,4);std::memcpy(bytes.data()+12,&count,4);if(out)std::memcpy(out,bytes.data(),size);return true;}
    else return false;if(out)std::memcpy(out,&value,size);return true;
}

EdenDsmodHostApi MakeHost(FakeHost& h) {
    EdenDsmodHostApi api{};
    api.abi_version = EDEN_DSMOD_MODULE_ABI_VERSION;
    api.struct_size = sizeof(EdenDsmodHostApi);
    api.abi_hash = EDEN_DSMOD_MODULE_ABI_HASH;
    api.userdata = &h;
    api.title_id = 0x0100F43008C44000ull;
    api.main_base=0x100000;const uint8_t build[]={0xB1,0xF1,0x2F,0xD9,0x19,0xEA,0xE8,0x6A};std::memcpy(api.build_id,build,8);
    api.read_memory=[](void* p,uint64_t at,void* out,size_t size)->EdenDsmodBool{return ReadSelection(*static_cast<FakeHost*>(p),at,out,size);};
    api.is_mapped=[](void* p,uint64_t at,size_t size)->EdenDsmodBool{return ReadSelection(*static_cast<FakeHost*>(p),at,nullptr,size);};
    api.get_tick = [](void* p) { return static_cast<FakeHost*>(p)->tick; };
    api.get_i64=[](void* p,const char* name,int64_t fallback)->int64_t{const auto& values=static_cast<FakeHost*>(p)->ints;const auto found=values.find(name);return found==values.end()?fallback:found->second;};
    api.publish_i64 = [](void* p, const char* n, long long v) { static_cast<FakeHost*>(p)->ints[n] = v; };
    api.publish_f64 = [](void* p, const char* n, double v) { static_cast<FakeHost*>(p)->floats[n] = v; };
    api.publish_text = [](void* p, const char* n, const char* v) { static_cast<FakeHost*>(p)->texts[n] = v; };
    api.log = [](void* p, uint32_t, const char* m) {
        ++static_cast<FakeHost*>(p)->logs;
        std::printf("  module log: %s\n", m);
    };
    api.read_romfs = [](void* p, const char* path, uint64_t offset, void* out, size_t size) -> size_t {
        auto& host = *static_cast<FakeHost*>(p);
        const auto full = host.romfs + "/" + path;
        FILE* f = std::fopen(full.c_str(), "rb");
        if (!f) return 0;
        _fseeki64(f, 0, SEEK_END);
        const auto length = _ftelli64(f);
        if (length < 0) { std::fclose(f); return 0; }
        if (!out) { std::fclose(f); return static_cast<size_t>(length); }
        if (offset >= static_cast<uint64_t>(length)) { std::fclose(f); return 0; }
        _fseeki64(f, static_cast<int64_t>(offset), SEEK_SET);
        size = std::min(size, static_cast<size_t>(length - offset));
        const auto got = std::fread(out, 1, size, f);
        std::fclose(f);
        return got;
    };
    return api;
}

EdenDsmodHostSaveApi MakeSaveApi(FakeHost& h) {
    EdenDsmodHostSaveApi api{};
    api.version = EDEN_DSMOD_SAVE_EXT_VERSION;
    api.struct_size = sizeof(EdenDsmodHostSaveApi);
    api.abi_hash = EDEN_DSMOD_SAVE_EXT_HASH;
    api.userdata = &h;
    api.read_save_file = [](void* p, const char* name, unsigned long long offset, void* out,
                            size_t size) -> size_t {
        auto& host = *static_cast<FakeHost*>(p);
        if (std::strcmp(name, "main") != 0)
            return 0;
        FILE* f = std::fopen(host.save_path.c_str(), "rb");
        if (!f)
            return 0;
        std::fseek(f, 0, SEEK_END);
        const long long total = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (out == nullptr) {
            std::fclose(f);
            return static_cast<size_t>(total);
        }
        if (offset >= static_cast<unsigned long long>(total)) {
            std::fclose(f);
            return 0;
        }
        std::fseek(f, static_cast<long>(offset), SEEK_SET);
        const size_t read = std::fread(out, 1, size, f);
        std::fclose(f);
        host.bytes_read += read;
        return read;
    };
    return api;
}

int failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

template <typename T>
void Show(const char* key, const T& value) {
    std::printf("  %-18s %s\n", key, std::to_string(value).c_str());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: za_module_host <module.dll> <save-copy.bin> <romfs-dir>\n");
        return 77;
    }
    HMODULE lib = LoadLibraryA(argv[1]);
    if (!lib) {
        std::printf("LoadLibrary failed: %lu\n", GetLastError());
        return 2;
    }
    using GetModuleFn = const EdenDsmodModuleApi* (*)(uint32_t, uint64_t);
    using GetSaveFn = const EdenDsmodModuleSaveExtensions* (*)(uint32_t, uint64_t);
    using GetExtFn = const EdenDsmodModuleExtensions* (*)(uint32_t, uint64_t);
    auto get_module = reinterpret_cast<GetModuleFn>(
        reinterpret_cast<void*>(GetProcAddress(lib, "eden_dsmod_get_module")));
    auto get_save = reinterpret_cast<GetSaveFn>(
        reinterpret_cast<void*>(GetProcAddress(lib, "eden_dsmod_get_save_extensions")));
    auto get_ext = reinterpret_cast<GetExtFn>(
        reinterpret_cast<void*>(GetProcAddress(lib, "eden_dsmod_get_extensions")));
    Check(get_module && get_save && get_ext, "exports present");
    if (!get_module || !get_save || !get_ext)
        return 1;

    const EdenDsmodModuleApi* api =
        get_module(EDEN_DSMOD_MODULE_ABI_VERSION, EDEN_DSMOD_MODULE_ABI_HASH);
    const EdenDsmodModuleSaveExtensions* save_ext =
        get_save(EDEN_DSMOD_SAVE_EXT_VERSION, EDEN_DSMOD_SAVE_EXT_HASH);
    const EdenDsmodModuleExtensions* ext =
        get_ext(EDEN_DSMOD_EXT_VERSION, EDEN_DSMOD_EXT_HASH);
    Check(api != nullptr, "module api negotiates");
    Check(save_ext != nullptr, "save extensions negotiate");
    Check(ext != nullptr, "base extensions negotiate");
    if (!api || !save_ext || !ext)
        return 1;
    std::printf("module: %s, title %016llX, caps %llX\n", api->name,
                static_cast<unsigned long long>(api->title_id),
                static_cast<unsigned long long>(api->capabilities));

    FakeHost h;
    h.save_path = argv[2];
    h.romfs = argv[3];
    const EdenDsmodHostApi host = MakeHost(h);
    const EdenDsmodHostSaveApi save_api = MakeSaveApi(h);

    void* m = api->create(&host, nullptr);
    Check(m != nullptr, "create");
    if (!m)
        return 1;
    save_ext->configure(m, &save_api);

    h.tick += 10;
    api->tick(m, &host);
    api->sample(m, &host);

    Check(h.ints["za.ready"] == 1, "za.ready");
    Check(h.ints["za.hash"] == 1, "za.hash");
    Check(h.texts["za.status"] == "sauvegarde a jour", "za.status text");
    std::printf("status: %s | ot: %s | money %lld | play %lld s\n", h.texts["za.status"].c_str(),
                h.texts["za.ot"].c_str(), h.ints["za.money"], h.ints["za.play"]);
    Show("za.party.count", h.ints["za.party.count"]);
    for (int i = 0; i < 6; ++i) {
        const std::string p = "za.p" + std::to_string(i);
        if (h.ints[p + ".v"])
            std::printf("  slot %d: sp %lld lvl %lld hp %lld/%lld nature %lld name %s\n", i,
                        h.ints[p + ".sp"], h.ints[p + ".lvl"], h.ints[p + ".hp"],
                        h.ints[p + ".hpmax"], h.ints[p + ".nature"], h.texts[p + ".name"].c_str());
    }
    Show("za.dex.caught", h.ints["za.dex.caught"]);
    Show("za.dex.known", h.ints["za.dex.known"]);
    Show("za.dex.g0", h.ints["za.dex.g0"]);
    Show("za.dex.g8.max", h.ints["za.dex.g8.max"]);
    Show("za.box.used", h.ints["za.box.used"]);
    Show("za.box.view1", h.ints["za.box.view1"]);
    Show("za.box.count", h.ints["za.box.count"]);
    Show("za.box.unlocked", h.ints["za.box.unlocked"]);
    Show("za.box.view.used", h.ints["za.box.view.used"]);
    std::printf("  pmx %.1f pmy %.1f | xi %lld yi %lld zi %lld | street '%s' map '%s'\n",
                h.floats["za.pmx"], h.floats["za.pmy"], h.ints["za.xi"], h.ints["za.yi"],
                h.ints["za.zi"], h.texts["za.street"].c_str(), h.texts["za.map"].c_str());

    struct ImageResult { unsigned width = 0, height = 0; size_t size = 0; std::vector<uint8_t> pixels; } image;
    auto sink = +[](void* p, uint32_t w, uint32_t h, const uint8_t* data, size_t size) {
        *static_cast<ImageResult*>(p) = {w, h, size};
        static_cast<ImageResult*>(p)->pixels.assign(data,data+size);
    };
    Check(ext->load_image != nullptr, "image extension offered");
    if (ext->load_image) {
        Check(ext->load_image(m, &host, "module:icon/6400", &image, sink) == EDEN_DSMOD_TRUE,
              "Pikachu icon through exported ABI");
        Check(image.width == 160 && image.height == 160 && image.size == 160*160*4,
              "icon RGBA size");
        api->sample(m, &host);
        Check(!h.texts["za.p0.sname"].empty(), "species names published after asset load");
        Check(ext->load_image(m, &host, "module:map", &image, sink) == EDEN_DSMOD_TRUE,
              "map through exported ABI");
        Check(image.width == 640 && image.height == 640 && image.size == 640*640*4,
              "map RGBA size");
        Check(ext->load_image(m, &host, "module:icon/nonsense", &image, sink) == EDEN_DSMOD_FALSE,
              "invalid image key rejected");
    }

    Check(ext->load_image(m,&host,"module:icon/114688",&image,sink),"normal Lucario icon loads");
    const auto normal_lucario=image.pixels;
    Check(ext->load_image(m,&host,"module:icon/638976",&image,sink),"shiny Lucario icon loads");
    Check(image.pixels!=normal_lucario,"normal and shiny Lucario use different textures");
    Check(h.ints["za.zone0.v"]==1&&h.ints["za.zone19.v"]==1,"twenty game-authored wild zones");
    Check(h.ints["za.center8.v"]==1&&h.ints["za.center9.v"]==0,"nine Pokemon centers resolved from field meshes");
    Check(ext->load_image(m,&host,"module:zone/0",&image,sink),"zone material loads");
    Check(image.pixels.size()>4&&image.pixels[1]==0,"zone material coverage channel is empty outside its contour");
    Check(ext->load_image(m,&host,"module:center",&image,sink),"Pokemon center icon loads");
    Check(image.pixels.size()>4&&image.pixels[3]==0,"Pokemon center icon has a transparent background");
    ext->on_action(m,"dex_gen",1);ext->on_action(m,"dex_filter",0);api->sample(m,&host);
    Check(h.ints["za.dex.rows"]==232,"Illumis catalogue has exactly 232 game-native species");
    Check(h.ints["za.d0.sp"]==152&&h.ints["za.d0.number"]==1,"regional order starts with Chikorita, not Bulbasaur");
    std::set<long long> regional;
    for(int start=0;start<232;start+=12){h.ints["@scroll_first:za_dex"]=start;api->sample(m,&host);
        for(int i=start;i<std::min(start+12,232);++i){const auto k="za.d"+std::to_string(i);Check(h.ints[k+".v"]&&h.ints[k+".number"]==i+1,"all scrolled dex rows retain regional numbering");Check(regional.insert(h.ints[k+".sp"]).second,"no duplicate regional species");}}
    Check(regional.size()==232,"entire regional list reachable by scrolling");
    Check(ext->on_action(m,"dex_pick",155),"absolute scrolled dex row accepted");api->sample(m,&host);Check(h.ints["za.detail.number"]==156,"far row updates persistent detail pane");
    ext->on_action(m,"dex_gen",2);api->sample(m,&host);Check(h.ints["za.dex.gen"]==2,"Mega Dimension catalogue selector");
    h.ints["@scroll_first:za_dex"]=0;
    Check(h.ints["za.center0.destination"]>0,"center has a real game travel destination");
    Check(ext->on_action(m,"center_pick",0)==EDEN_DSMOD_TRUE,"center opens its confirmation");api->sample(m,&host);
    Check(!h.texts["za.center.name"].empty()&&h.texts["za.center.name"]!="Centre Pokémon 1","center localized name from romfs");
    ext->on_action(m,"center_close",0);
    Check(ext->on_action(m,"team_select",5)==EDEN_DSMOD_TRUE,"team selection accepted");
    api->sample(m,&host);
    Check(h.ints["za.team.selected"]==5 && h.ints["za.sel.sp"]==h.ints["za.p5.sp"],"selected panel follows selected party slot");
    Check(!h.texts["za.sel.m0.name"].empty(),"selected move names from romfs");
    ext->on_action(m,"team_select",0);
    ext->on_action(m,"dex_filter",0);ext->on_action(m,"dex_gen",1);ext->on_action(m,"dex_pick",1);api->sample(m,&host);
    Check(h.ints["za.detail.sp"]==153 && h.ints["za.dex.open"]==0,"dex row updates persistent detail pane without page toggle");
    Check(!h.texts["za.detail.description"].empty(),"dex description from romfs");
    ext->on_action(m,"dex_pick",2);api->sample(m,&host);
    Check(h.ints["za.detail.sp"]==154 && h.ints["za.dex.open"]==0,"next dex tap changes detail and navigation edge");
    for(int category=0;category<3;++category){ext->on_action(m,"quest_category",category);api->sample(m,&host);Check(h.ints["za.quest.category"]==category,"mission category binds");Check(h.ints["za.quest.rows"]>0,"mission catalogue available");}
    ext->on_action(m,"quest_category",0);api->sample(m,&host);
    auto first_mission=h.texts["za.q0.title"];
    Check(ext->on_action(m,"quest_pick",0),"mission tap accepted");api->sample(m,&host);
    Check(h.ints["za.quest.open"]==1 && h.texts["za.quest.detail.title"]==first_mission,"mission detail opens selected title");
    Check(!h.texts["za.quest.detail.description"].empty(),"mission detail has description");
    Check(h.ints["za.quest.detail.state"]==2,"active main quest highlighted");
    ext->on_action(m,"quest_pick",0);api->sample(m,&host);Check(h.ints["za.quest.open"]==0,"mission detail closes");

    ext->on_action(m,"team_select",0);api->sample(m,&host);
    Check(h.ints["za.sel.type1"]==17 && h.ints["za.sel.type2"]==8,"Forgelina Fairy/Steel from personal table");
    Check(h.ints["za.sel.m0.type"]==8,"Metal Claw Steel from move table");
    Check(h.ints["za.p0.xpmax"]>1 && h.ints["za.p0.xp"]>0,"XP progress uses species growth table");
    image={};Check(ext->load_image(m,&host,"module:type/17",&image,sink)==EDEN_DSMOD_TRUE&&image.width==60,"type glyph decoded from game");
    ext->on_action(m,"quest_category",1);api->sample(m,&host);
    Check(h.ints["za.q0.state"]==2 && !h.texts["za.q0.description"].empty(),"active Extras mapped to Mable work and description");
    h.active_slot=0;api->tick(m,&host);ext->on_action(m,"team_select",5);
    Check(ext->on_action(m,"send_out",0),"send request accepted with valid HUD selection");
    api->tick(m,&host);api->sample(m,&host);Check(h.ints["za.cmd.right"]==1&&h.ints["za.cmd.send"]==0,"send selects before dispatching Up");
    for(int frame=0;frame<18;++frame)api->tick(m,&host);api->sample(m,&host);
    Check(h.ints["za.cmd.send"]==0,"never send while selected slot differs");
    h.active_slot=5;for(int frame=0;frame<18;++frame)api->tick(m,&host);api->sample(m,&host);
    Check(h.ints["za.cmd.send"]==1,"dispatch Up only after target selection confirmed");
    api->tick(m,&host);api->sample(m,&host);Check(h.ints["za.cmd.send"]==0,"send pulse not repeated");
    h.active_slot=-1;api->tick(m,&host);Check(!ext->on_action(m,"send_out",0),"refuse send with unknown HUD selection");
    image={};Check(ext->load_image(m,&host,"module:item/1",&image,sink)==EDEN_DSMOD_TRUE && image.width==160,"item icon through exported ABI");
    ext->on_action(m,"dex_gen",1);
    for(int filter=1;filter<=4;++filter) {
        ext->on_action(m,"dex_filter",filter); api->sample(m,&host);
        for(int i=0;i<std::min<long long>(12,h.ints["za.dex.rows"]);++i) {
            const auto k="za.d"+std::to_string(i); if(!h.ints[k+".v"])continue;
            const auto state=h.ints[k+".state"];
            Check(filter==1 ? state>=2 : filter==2 ? state<2 : filter==3 ? state==1 : state==3,
                  "dex filter matches displayed state");
        }
    }
    ext->on_action(m,"bag_category",-1);api->sample(m,&host);
    const auto item_total=h.ints["za.bag.total"];
    std::set<long long> item_ids;
    Check(!h.texts["za.item.description"].empty()&&!h.texts["za.item.description"].starts_with("Description indisponible"),"item description decoded despite reversed text/labels pack order");
    for(int start=0;start<item_total;start+=12){h.ints["@scroll_first:za_bag"]=start;api->sample(m,&host);
        for(int i=start;i<std::min<long long>(start+12,item_total);++i){const auto k="za.i"+std::to_string(i);Check(h.ints[k+".v"],"scrolled bag row present");Check(item_ids.insert(h.ints[k+".id"]).second,"no duplicate bag item");Check(h.ints[k+".count"]>0||h.ints[k+".held"],"only owned/equipped bag rows");}}
    Check(item_ids.size()==item_total,"entire bag reachable by touch scrolling");
    ext->on_action(m,"bag_pick",100);api->sample(m,&host);Check(h.ints["za.item.id"]==h.ints["za.i100.id"],"absolute bag row selects correct object after scrolling");
    h.ints["@scroll_first:za_bag"]=0;
    long long category_total=0;
    for(int category=0;category<=8;++category) {
        ext->on_action(m,"bag_category",category);api->sample(m,&host);
        category_total+=h.ints["za.bag.rows"];
        for(int i=0;i<std::min<long long>(16,h.ints["za.bag.rows"]);++i) {
            const auto k="za.i"+std::to_string(i);
            if(h.ints[k+".v"])Check(h.ints[k+".pouch"]==category,"bag category filter");
        }
    }
    Check(category_total==item_total,"bag categories account for all items");
    ext->on_action(m,"bag_category",6);api->sample(m,&host);ext->on_action(m,"bag_pick",0);api->sample(m,&host);
    Check(h.texts["za.item.name"].find(" - ")!=std::string::npos&&h.ints["za.item.type"]>=0,"TM includes move name and type");
    Check(!h.texts["za.item.description"].empty(),"TM move detail available");
    ext->on_action(m,"zone_pick",0);api->sample(m,&host);Check(h.ints["za.zone.rows"]>0&&!h.texts["za.zp0.name"].empty(),"zone tap publishes a real encounter list");
    std::printf("regional dex + scroll windows + item/TM detail + zone encounters: OK; bag %lld items\n",item_total);

    // An unchanged save must cost a size query plus the 0x20-byte trailer, never a 3 MiB read.
    const auto before = h.bytes_read;
    h.tick += 300;
    api->tick(m, &host);
    const auto unchanged = h.bytes_read - before;
    std::printf("unchanged save: %llu byte(s) re-read\n", unchanged);
    Check(unchanged <= 0x40, "unchanged save costs a tail read only");
    api->sample(m, &host);
    Check(h.ints["za.ready"] == 1, "ready survives a skipped poll");

    // Browsing: next, then back, then follow the game's box again.
    const long long first = h.ints["za.box.view1"];
    Check(ext->on_action(m, "box_next", 0) == EDEN_DSMOD_TRUE, "box_next accepted");
    api->sample(m, &host);
    Check(h.ints["za.box.view1"] == first + 1, "box_next advances");
    Check(h.ints["za.box.follow"] == 0, "browsing stops following");
    Check(ext->on_action(m, "box_prev", 0) == EDEN_DSMOD_TRUE, "box_prev accepted");
    api->sample(m, &host);
    Check(h.ints["za.box.view1"] == first, "box_prev returns");
    Check(ext->on_action(m, "box_follow", 0) == EDEN_DSMOD_TRUE, "box_follow accepted");
    api->sample(m, &host);
    Check(h.ints["za.box.follow"] == 1, "follow restored");
    Check(ext->on_action(m, "refresh", 0) == EDEN_DSMOD_TRUE, "refresh accepted");
    Check(ext->on_action(m, "nonsense", 0) == EDEN_DSMOD_FALSE, "unknown action refused");

    // A rewritten save (same size, different trailer) must trigger a full re-read.
    {
        const std::string changed = h.save_path + ".changed";
        std::FILE* in = std::fopen(h.save_path.c_str(), "rb");
        std::vector<unsigned char> all;
        if (in) {
            std::fseek(in, 0, SEEK_END);
            all.resize(static_cast<size_t>(std::ftell(in)));
            std::fseek(in, 0, SEEK_SET);
            const size_t got = std::fread(all.data(), 1, all.size(), in);
            all.resize(got);
            std::fclose(in);
        }
        Check(!all.empty(), "save copy readable");
        all.back() ^= 0xFF;
        std::FILE* out = std::fopen(changed.c_str(), "wb");
        if (out) {
            std::fwrite(all.data(), 1, all.size(), out);
            std::fclose(out);
        }
        h.save_path = changed;
        const auto before_change = h.bytes_read;
        h.tick += 300;
        api->tick(m, &host);
        api->sample(m, &host);
        Check(h.bytes_read - before_change >= all.size(), "rewritten save is read in full");
        Check(h.texts["za.status"] == "somme de controle invalide", "rewritten save flagged");
        std::printf("rewritten save status: %s\n", h.texts["za.status"].c_str());
    }

    // A save that is not there must degrade to "introuvable", never crash.
    h.save_path = "Z:/does/not/exist";
    ext->on_action(m, "refresh", 0);
    api->sample(m, &host);
    Check(h.ints["za.ready"] == 0, "missing save clears ready");
    std::printf("missing save status: %s\n", h.texts["za.status"].c_str());

    api->destroy(m);
    std::printf(failures == 0 ? "za-module-host: ok\n" : "za-module-host: %d failure(s)\n",
                failures);
    return failures == 0 ? 0 : 1;
}
