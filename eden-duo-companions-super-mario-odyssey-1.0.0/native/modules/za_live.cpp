// SPDX-License-Identifier: GPL-3.0-or-later
// Z-A 2.0.2: read-only access to the game's encrypted live SCBlock table.
// Root independently documented by SysBot LZA; entry layout and actual party/coordinate
// records verified in the running Windows Eden process. Other builds fail closed.
#include "za_live.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
#include <unordered_set>
namespace za { namespace {
template<class T> T At(const std::uint8_t* bytes){T out{};std::memcpy(&out,bytes,sizeof(out));return out;}
bool Read(const EdenDsmodHostApi& host,std::uint64_t at,void* out,std::size_t count){
    if(at<0x10000||at>=(1ull<<39)||count>(1u<<20)||at+count<at||!out||!host.is_mapped)return false;
    // ReadBlock synchronizes rasterizer pages, including unrelated GPU allocations
    // encountered by a scan. CPU-owned script data needs no GPU download. The host
    // promises a contiguous pointer only within one page: validate/copy each page.
    if(host.get_read_pointer){
        for(std::size_t offset=0;offset<count;){
            const auto n=std::min<std::size_t>(count-offset,4096-((at+offset)&4095));
            if(!host.is_mapped(host.userdata,at+offset,n))return false;
            const auto* source=host.get_read_pointer(host.userdata,at+offset,n);
            if(!source)return false;
            std::memcpy(static_cast<std::uint8_t*>(out)+offset,source,n);offset+=n;
        }
        return true;
    }
    return host.read_memory&&host.is_mapped(host.userdata,at,count)&&host.read_memory(host.userdata,at,out,count);
}
// A scan window may straddle unmapped guest pages even when its useful objects
// are valid. Preserve readable pages and leave holes zeroed; never read through a hole.
bool ScanRead(const EdenDsmodHostApi& host,std::uint64_t at,std::vector<std::uint8_t>& out){
    std::fill(out.begin(),out.end(),0);
    if(Read(host,at,out.data(),out.size()))return true;
    bool any=false;
    for(std::size_t offset=0;offset<out.size();){const auto count=std::min<std::size_t>(out.size()-offset,4096-((at+offset)&4095));
        any=Read(host,at+offset,out.data()+offset,count)||any;offset+=count;}
    return any;
}
std::uint64_t Pointer(const EdenDsmodHostApi& host,std::uint64_t at){
    std::uint64_t out{};return Read(host,at,&out,sizeof(out)) ? out : 0;
}
struct Cipher {
    std::uint32_t state;unsigned byte=0;
    static std::uint32_t Step(std::uint32_t v){v^=v<<2;v^=v>>15;v^=v<<13;return v;}
    explicit Cipher(std::uint32_t key):state(key){for(int i=std::popcount(key);i;--i)state=Step(state);}
    std::uint8_t Next(){auto result=static_cast<std::uint8_t>(state>>(byte*8));if(++byte==4){state=Step(state);byte=0;}return result;}
};
bool Block(const EdenDsmodHostApi& host,std::uint64_t entry,std::uint32_t key,std::size_t expected,std::vector<std::uint8_t>& out){
    std::array<std::uint8_t,32> descriptor{};
    if(!Read(host,entry,descriptor.data(),descriptor.size()) || At<std::uint32_t>(descriptor.data())!=key)return false;
    const auto data=At<std::uint64_t>(descriptor.data()+8), size=At<std::uint64_t>(descriptor.data()+16);
    if(size!=expected+5)return false;
    out.resize(size);
    if(!Read(host,data,out.data(),out.size()))return false;
    Cipher cipher(key);for(auto& b:out)b^=cipher.Next();
    if(out[0]!=4 || At<std::uint32_t>(out.data()+1)!=expected)return false;
    out.erase(out.begin(),out.begin()+5);return true;
}
}
bool LiveReader::LastHP(const Entity& entity,std::uint16_t& hp) const {
    const auto found=confirmed_hp_.find((std::uint64_t(entity.pid)<<32)|entity.encryption_constant);
    if(found==confirmed_hp_.end()||found->second>entity.hp_max)return false;
    hp=found->second;return true;
}
LiveSnapshot LiveReader::Poll(const EdenDsmodHostApi& host,bool inspect_boxes,int requested_box){
    LiveSnapshot result;
    constexpr std::uint8_t build[]={0xB1,0xF1,0x2F,0xD9,0x19,0xEA,0xE8,0x6A};
    if(!host.main_base){result.diagnostic="Mémoire : base du jeu indisponible";return result;}
    if(std::memcmp(host.build_id,build,sizeof(build))!=0){
        // Some hosts may expose stale metadata. Accept that only if all nine relative
        // function pointers in three independent 2.0.2 vtables match the verified image.
        constexpr std::uint64_t signatures[][4]={{0x3da4348,0x2add89c,0x269fe70,0x269fe7c},{0x3de1a20,0x2c05980,0x269fe70,0x269fe7c},{0x3bd3b30,0xfe6214,0xfe6290,0xfe56f4}};
        bool verified=true;
        for(const auto& sig:signatures){std::array<std::uint64_t,3> words{};
            if(!Read(host,host.main_base+sig[0],words.data(),sizeof(words))){verified=false;break;}
            for(int i=0;i<3;++i)if(words[i]!=host.main_base+sig[i+1])verified=false;
        }
        if(!verified){char id[17];for(int i=0;i<8;++i)std::snprintf(id+i*2,3,"%02X",host.build_id[i]);
            result.diagnostic=std::string("Build chargé : ")+id+". Activez la mise à jour Z-A 2.0.2 puis redémarrez Eden.";return result;}
    }
    result.diagnostic="Mémoire : gestionnaire du jeu indisponible";
    const auto tick=host.get_tick?host.get_tick(host.userdata):0;
    auto selected_object=[&](std::uint64_t hud){
        if(!hud||Pointer(host,hud)!=host.main_base+0x3DE1A20)return false;
        const auto view=Pointer(host,hud+0xB8);
        const auto selector=view?Pointer(host,view+0x368):0;
        std::array<std::uint8_t,16> selection{};
        if(selector&&Read(host,selector,selection.data(),selection.size())&&
           At<std::uint64_t>(selection.data())==host.main_base+0x3BD3B30){
            const auto index=At<std::uint32_t>(selection.data()+8),count=At<std::uint32_t>(selection.data()+12);
            if(count>=1&&count<=6&&index<count){result.selected_index=int(index);return true;}
        }return false;
    };
    auto position=[&](std::uint64_t object){
        std::array<std::uint8_t,80> data{};
        if(!Read(host,object,data.data(),data.size())||At<std::uint64_t>(data.data())!=host.main_base+0x3DA4348)return false;
        const auto* area=reinterpret_cast<const char*>(data.data()+0x18);
        auto length=std::find(area,area+24,0)-area;
        const auto x=At<float>(data.data()+0x38),y=At<float>(data.data()+0x3C),z=At<float>(data.data()+0x40);
        if(!length||length>=24||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||std::abs(x)>100000||std::abs(y)>100000||std::abs(z)>100000)return false;
        result.area.assign(area,length);result.x=x;result.y=y;result.z=z;
        result.coordinates_valid=result.position_live=true;return true;
    };
    // These are entries in runtime pointer registries, whose slots move between sessions.
    // Validate cached objects by their build-specific vtable, rediscover only on failure.
    auto selected=[&](std::uint64_t slot){const auto object=Pointer(host,slot);if(selected_object(object)){selector_object_=object;return true;}return false;};
    const bool have_selection=selected_object(selector_object_)||selected(selector_slot_?selector_slot_:host.main_base+0x48891E0);
    const bool have_position=position(position_object_);
    if((!have_selection||!have_position)&&tick>=discovery_retry_){
        discovery_retry_=tick+300;
        const auto heap_begin=host.get_heap_begin?host.get_heap_begin(host.userdata):0;
        const auto heap_end=host.get_heap_end?host.get_heap_end(host.userdata):0;
        auto scan=[&](std::uint64_t offset,std::size_t size,auto&& accept){
            std::vector<std::uint8_t> pointers(size);
            if(!ScanRead(host,host.main_base+offset,pointers))return;
            for(std::size_t at=0;at+8<=size;at+=8){const auto object=At<std::uint64_t>(pointers.data()+at);
                // NCE and Dynarmic place the guest heap at different addresses.
                const bool in_heap=heap_begin&&heap_end>heap_begin ? object>=heap_begin&&object<heap_end : object>=0x10000&&object<(1ull<<39);
                const bool mapped_alias=object>=0x10000&&object<(1ull<<39)&&host.is_mapped&&host.is_mapped(host.userdata,object,8);
                if((in_heap||mapped_alias)&&accept(host.main_base+offset+at,object))break;
            }
        };
        // BSS holds allocator/owner roots; an object's offset within its allocation varies.
        // Registry references in the data segment can move by megabytes across launches.
        // Inspect only the first 512 bytes of distinct validated roots, not the guest heap.
        bool found_position=have_position,found_selection=have_selection;
        std::unordered_set<std::uint64_t> visited;
        scan(0x610B000,0x70000,[&](auto,auto root){
            if(!visited.insert(root).second)return false;
            for(std::uint64_t offset=0;offset<512;offset+=8){
                const auto object=root+offset,kind=Pointer(host,object);
                if(!found_position&&kind==host.main_base+0x3DA4348&&position(object)){position_object_=object;found_position=true;}
                if(!found_selection&&kind==host.main_base+0x3DE1A20&&selected_object(object)){selector_object_=object;found_selection=true;}
                if(found_position&&found_selection)return true;
            }return false;
        });
        if(!found_position)scan(0x40F0000,0x20000,[&](auto,auto object){if(position(object)){position_object_=object;return true;}return false;});
        if(!found_selection)scan(0x4800000,0x100000,[&](auto slot,auto){if(selected(slot)){selector_slot_=slot;return true;}return false;});
    }
    auto map_object=[&](std::uint64_t object){
        std::array<std::uint8_t,0x588> data{};
        if(!Read(host,object,data.data(),data.size())||At<std::uint64_t>(data.data())!=host.main_base+0x3DF1D48||At<std::uint64_t>(data.data()+0x10)!=object)return false;
        const auto mode=At<std::uint32_t>(data.data()+0x208),destination=At<std::uint32_t>(data.data()+0x2A8);
        if(mode>3||destination>10000)return false;
        // The view remains allocated after closing/warping. Focus is relinquished
        // both when closed and to the travel dialog. Mode 4 is an inactive cached
        // destination list; the controller primes it with Plus before navigation.
        const bool focused=(At<std::uint32_t>(data.data()+0x584)&0x100)!=0;
        result.map_mode=focused?int(mode):(mode==2?4:3);result.map_destination=int(destination);return true;
    };
    if(!map_object(map_object_)){
        map_object_=0;
        const auto system=Pointer(host,host.main_base+0x610A000),heaps=system?Pointer(host,system+0x5F8):0;
        const auto ui_heap=heaps?Pointer(host,heaps+0x6A8):0;
        const auto begin=Pointer(host,ui_heap)==host.main_base+0x3BCE178?Pointer(host,ui_heap+0x38):0;
        if(begin>=0x10000&&begin<(1ull<<39)){
            if(begin!=ui_scan_begin_){ui_scan_begin_=begin;ui_scan_cursor_=0;ui_scan_retry_=0;}
            constexpr std::uint64_t Window=128ull<<20,Chunk=1ull<<20;
            if(ui_scan_cursor_>=Window&&tick>=ui_scan_retry_)ui_scan_cursor_=0;
            std::vector<std::uint8_t> bytes(Chunk);
            bool scanned=false;
            for(int budget=0;budget<4&&ui_scan_cursor_<Window&&!map_object_;++budget){
                scanned=true;
                const auto address=begin+ui_scan_cursor_;ui_scan_cursor_+=Chunk;
                if(!ScanRead(host,address,bytes))continue;
                for(std::size_t at=0;at+8<=bytes.size();at+=8)if(At<std::uint64_t>(bytes.data()+at)==host.main_base+0x3DF1D48&&map_object(address+at)){map_object_=address+at;break;}
            }
            if(scanned&&ui_scan_cursor_>=Window)ui_scan_retry_=tick+300;
        }
    }
    const auto* page=host.get_text?host.get_text(host.userdata,"__page"):nullptr;
    const bool want_boxes=inspect_boxes||(page&&std::strcmp(page,"box")==0);
    auto box_object=[&](std::uint64_t object){
        std::array<std::uint8_t,0x100> b{};
        if(!Read(host,object,b.data(),b.size())||At<std::uint64_t>(b.data())!=host.main_base+0x3DB4880||At<std::uint64_t>(b.data()+0x10)!=object)return false;
        const auto box=At<std::uint64_t>(b.data()+0xE0),party=At<std::uint64_t>(b.data()+0xE8);
        auto index=[&](std::uint64_t view,int max){
            if(Pointer(host,view)!=host.main_base+0x3BD0E60)return -1;
            const auto selector=Pointer(host,view+0x368);std::array<std::uint8_t,16> data{};
            if(!Read(host,selector,data.data(),data.size())||At<std::uint64_t>(data.data())!=host.main_base+0x3BD3B30||At<std::uint32_t>(data.data()+12)!=6)return -1;
            const int value=int(At<std::uint32_t>(data.data()+8));return value>=0&&value<max?value:-1;
        };
        const int cursor=index(box,30),team=index(party,6);if(cursor<0||team<0)return false;
        result.boxes.cursor=cursor;result.boxes.party_cursor=team;return true;
    };
    auto drag_object=[&](std::uint64_t drag){
        std::array<std::uint8_t,0xD8> state{};
        if(!Read(host,drag,state.data(),state.size())||At<std::uint64_t>(state.data())!=host.main_base+0x3DB4D18||At<std::uint64_t>(state.data()+0x10)!=drag)return false;
        const int from=int(At<std::uint32_t>(state.data()+0xC8)),slot=int(At<std::uint32_t>(state.data()+0xCC));
        const int to=int(At<std::uint32_t>(state.data()+0xD0)),target=int(At<std::uint32_t>(state.data()+0xD4));
        auto kind=[](int v){return (v>=0&&v<32)||v==998;};
        if(!kind(from)||!kind(to)||slot<0||slot>=30||target<0||target>=(to==998?6:30))return false;
        result.boxes.source_box=from;result.boxes.source_slot=slot;result.boxes.target_box=to;result.boxes.target_slot=target;return true;
    };
    if(want_boxes){
        bool pane_ok=box_object(box_object_),drag_ok=drag_object(box_drag_);
        if(!pane_ok)box_object_=0;if(!drag_ok)box_drag_=0;
        if(!pane_ok||!drag_ok){
        const auto system=Pointer(host,host.main_base+0x610A000),heaps=system?Pointer(host,system+0x5F8):0;
        const auto ui_heap=heaps?Pointer(host,heaps+0x6A8):0;
        const auto begin=Pointer(host,ui_heap)==host.main_base+0x3BCE178?Pointer(host,ui_heap+0x38):0;
        if(begin>=0x10000&&begin<(1ull<<39)){
            if(begin!=box_scan_begin_){box_scan_begin_=begin;box_scan_cursor_=0;box_scan_retry_=0;}
            constexpr std::uint64_t Window=4ull<<30,Chunk=1ull<<20;
            if(box_scan_cursor_>=Window&&tick>=box_scan_retry_)box_scan_cursor_=0;
            std::vector<std::uint8_t> data(Chunk);bool scanned=false;
            for(int budget=0;budget<4&&box_scan_cursor_<Window&&(!pane_ok||!drag_ok);++budget){
                scanned=true;const auto address=begin+box_scan_cursor_;box_scan_cursor_+=Chunk;
                if(!ScanRead(host,address,data))continue;
                for(std::size_t at=0;at+8<=data.size();at+=8){
                    const auto type=At<std::uint64_t>(data.data()+at);
                    if(!pane_ok&&type==host.main_base+0x3DB4880&&box_object(address+at)){box_object_=address+at;pane_ok=true;}
                    if(!drag_ok&&type==host.main_base+0x3DB4D18&&drag_object(address+at)){box_drag_=address+at;drag_ok=true;}
                    if(pane_ok&&drag_ok)break;
                }
            }
            if(scanned&&box_scan_cursor_>=Window)box_scan_retry_=tick+300;
        }
        }
        result.boxes.valid=pane_ok&&drag_ok;
    }
    const auto root=Pointer(host,host.main_base+0x610A670);
    const auto manager=root?Pointer(host,root+0x30):0;
    const auto begin=manager?Pointer(host,manager+8):0;
    const auto end=manager?Pointer(host,manager+16):0;
    if(!begin || end<begin || (end-begin)%32 || end-begin>2048*32)return result;
    result.diagnostic="Mémoire : table des données en cours de lecture";
    if(begin!=table_ || tick>=next_index_ || !party_entry_){
        table_=begin;next_index_=tick+60;party_entry_=coordinates_entry_=quest_entry_=mable_entry_=box_entry_=0;
        std::vector<std::uint8_t> entries(end-begin);
        if(!Read(host,begin,entries.data(),entries.size()))return result;
        std::uint64_t previous=0;
        for(std::size_t i=0;i<entries.size();i+=32){
            const auto key=At<std::uint32_t>(entries.data()+i);
            if((i&&key<=previous)){table_=party_entry_=coordinates_entry_=quest_entry_=mable_entry_=box_entry_=0;return result;}
            previous=key;
            if(key==KParty)party_entry_=begin+i;
            if(key==KBox)box_entry_=begin+i;
            if(key==KCoordinates)coordinates_entry_=begin+i;
            if(key==0xB9B223B9)quest_entry_=begin+i;
            if(key==0x03913534)mable_entry_=begin+i;
        }
    }
    std::vector<std::uint8_t> bytes;
    if(party_entry_ && Block(host,party_entry_,KParty,0xB40,bytes)){
        bool consistent=true;
        for(std::size_t i=0;i<6;++i){
            const auto* record=bytes.data()+i*kPartySlotSize;
            if(!At<std::uint32_t>(record))continue;
            Entity entity;
            if(!DecryptEntity(record,kPartySize,entity) || !entity.checksum_ok || entity.species>1025 ||
                entity.level<1 || entity.level>100 || entity.hp_max==0 || entity.hp>entity.hp_max){consistent=false;break;}
            result.party[i]=std::move(entity);++result.party_count;
        }
        result.party_valid=consistent&&result.party_count>0;
    }
    if(want_boxes&&box_entry_){
        const int wanted=std::clamp(requested_box>=0?requested_box:(host.get_i64?int(host.get_i64(host.userdata,"za.box.view",0)):0),0,31);
        if(wanted!=cached_box_index_||tick>=next_box_poll_){
            next_box_poll_=tick+15;std::vector<std::uint8_t> data;
            if(Block(host,box_entry_,KBox,kBoxCount*kBoxSlotCount*kBoxSlotSize,data)){
                std::array<Entity,30> rows{};bool valid=true;
                for(int i=0;i<30;++i){const auto* record=data.data()+(wanted*30+i)*kBoxSlotSize;
                    if(!At<std::uint32_t>(record))continue;
                    if(!DecryptEntity(record,kPartySize,rows[i])||!rows[i].checksum_ok||rows[i].species>1025){valid=false;break;}
                }
                if(valid){cached_box_=std::move(rows);cached_box_index_=wanted;cached_box_valid_=true;}
            }
        }
        if(cached_box_valid_&&cached_box_index_==wanted){result.box_valid=true;result.box_view_index=wanted;result.box_view=cached_box_;}
    }
    if(quest_entry_ && Block(host,quest_entry_,0xB9B223B9,16384,bytes)){
        for(std::size_t at=0;at<bytes.size();at+=16){auto key=At<std::uint64_t>(bytes.data()+at);if(key!=14695981039346656837ull)result.quest_work[key]=At<std::uint64_t>(bytes.data()+at+8);}
    }
    if(mable_entry_ && Block(host,mable_entry_,0x03913534,16384,bytes)){
        for(std::size_t at=0;at<bytes.size();at+=16){auto key=At<std::uint64_t>(bytes.data()+at);if(key!=14695981039346656837ull)result.quest_work[key]=At<std::uint64_t>(bytes.data()+at+8);}
    }
    // The encrypted SCBlocks are save snapshots. Combat uses separate 0x148-byte PA9
    // records in the script-object allocator. Match EC + PID + species + checksum, never
    // a naked HP value. Search a bounded allocator window once, then read six cached records.
    if(result.party_valid){
        const auto access=Pointer(host,host.main_base+0x610A710);
        const auto boxes=access?Pointer(host,access+0xB0):0;
        const auto allocator=boxes?Pointer(host,boxes+8):0;
        const auto pool=allocator>=0x100000?(allocator&~0xfffffull):0;
        bool changed=pool!=pool_begin_;
        for(int i=0;i<6;++i)if(hp_identity_[i]!=result.party[i].encryption_constant)changed=true;
        if(changed){pool_begin_=pool;pool_cursor_=0;hp_records_.fill(0);for(int i=0;i<6;++i)hp_identity_[i]=result.party[i].encryption_constant;}
        auto decode=[&](const std::uint8_t* data,int slot,Entity& entity){return DecryptEntity(data,kStoredSize,entity)&&entity.checksum_ok&&entity.encryption_constant==result.party[slot].encryption_constant&&entity.pid==result.party[slot].pid&&entity.species==result.party[slot].species&&entity.hp<=result.party[slot].hp_max;};
        bool missing=false;
        for(int i=0;i<6;++i)if(result.party[i].present){
            std::array<std::uint8_t,kStoredSize> record{};Entity entity;
            if(hp_records_[i]&&Read(host,hp_records_[i],record.data(),record.size())&&decode(record.data(),i,entity)){result.party[i].hp=entity.hp;result.party[i].exp=entity.exp;result.hp_valid[i]=true;}
            else{hp_records_[i]=0;missing=true;}
        }
        constexpr std::uint64_t Window=32u<<20,Chunk=256u<<10;
        if(missing&&pool_begin_&&(pool_cursor_<Window||tick>=pool_retry_)){
            if(pool_cursor_>=Window)pool_cursor_=0;
            std::vector<std::uint8_t> chunk(Chunk+kStoredSize);
            // Search outward on both sides of the allocator page: allocation order differs
            // between NCE and Dynarmic, and the sixth PA9 can precede the anchor.
            const auto index=pool_cursor_/Chunk,distance=((index+1)/2)*Chunk;
            const auto address=(index&1)?(distance<pool_begin_?pool_begin_-distance:0):pool_begin_+distance;pool_cursor_+=Chunk;
            if(address&&ScanRead(host,address,chunk)){
                for(std::size_t at=0;at<Chunk;at+=4){auto ec=At<std::uint32_t>(chunk.data()+at);if(!ec)continue;
                    for(int i=0;i<6;++i)if(!hp_records_[i]&&ec==hp_identity_[i]){Entity entity;if(decode(chunk.data()+at,i,entity)){hp_records_[i]=address+at;result.party[i].hp=entity.hp;result.party[i].exp=entity.exp;result.hp_valid[i]=true;}}
                }
            }
            if(pool_cursor_>=Window)pool_retry_=tick+300;
        }
    }
    if(result.party_valid)for(int i=0;i<6;++i)if(result.party[i].present){
        auto& entity=result.party[i];
        const auto key=(std::uint64_t(entity.pid)<<32)|entity.encryption_constant;
        if(result.hp_valid[i]){confirmed_hp_[key]=entity.hp;confirmed_exp_[key]=entity.exp;}
        else {LastHP(entity,entity.hp);if(auto found=confirmed_exp_.find(key);found!=confirmed_exp_.end())entity.exp=found->second;}
    }
    if(!result.position_live && coordinates_entry_ && Block(host,coordinates_entry_,KCoordinates,0x70,bytes)){
        const auto length=std::find(bytes.begin(),bytes.begin()+0x20,0)-bytes.begin();
        result.area.assign(reinterpret_cast<const char*>(bytes.data()),length);
        result.x=At<float>(bytes.data()+0x20);result.y=At<float>(bytes.data()+0x24);
        result.z=At<float>(bytes.data()+0x28);result.rotation=At<float>(bytes.data()+0x2c);
        result.coordinates_valid=!result.area.empty()&&result.area.size()<24&&
            std::isfinite(result.x)&&std::isfinite(result.y)&&std::isfinite(result.z)&&
            std::abs(result.x)<100000&&std::abs(result.y)<100000&&std::abs(result.z)<100000;
    }
    int hp=0;for(bool valid:result.hp_valid)hp+=valid;
    result.diagnostic="Direct : PV "+std::to_string(hp)+"/"+std::to_string(result.party_count)+" · position "+(result.position_live?"OK":"introuvable")+" · sélection "+(result.selected_index>=0?"OK":"introuvable");
    return result;
}
}
