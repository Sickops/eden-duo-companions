// SPDX-License-Identifier: GPL-3.0-or-later
// Uses read-only encrypted records captured from Eden. No game is needed.
#include "za_live.cpp"
#include "za_map.h"
#include "za_travel.h"
#include "za_box_swap.h"
#include <cassert>
#include <fstream>
#include <map>
#include <iterator>
struct Memory {
 std::map<uint64_t,std::vector<uint8_t>> ranges;
 uint64_t tick=0;
 void Put(uint64_t address,uint64_t value){auto& b=ranges[address];b.resize(8);std::memcpy(b.data(),&value,8);}
 bool Read(uint64_t at,void* out,size_t size){auto it=ranges.upper_bound(at);if(it==ranges.begin())return false;--it;auto offset=at-it->first;if(offset>it->second.size()||size>it->second.size()-offset)return false;if(out)std::memcpy(out,it->second.data()+offset,size);return true;}
};
int main(int argc,char** argv){
 if(argc<2)return 77;
 float u=0,v=0;
 // Independent landmark measurements in map-calibration-v7.json (texture pixels).
 assert(za::MapPoint("t1",-344.725525f,-648.544922f,u,v)&&std::abs(u-221.55f)<3&&std::abs(v-410.73f)<3);
 assert(za::MapPoint("t1",-584.487366f,-717.530029f,u,v)&&std::abs(u-372.07f)<3&&std::abs(v-455.24f)<3);
 assert(za::MapPoint("t1",-779.930359f,-900.439331f,u,v)&&std::hypot(u-491.71f,v-566.08f)<4);
 assert(!za::MapPoint("t4_06",-344,-648,u,v));
 assert(!za::MapPoint("t1",10000,-648,u,v));
 assert(!za::MapPoint("t1",NAN,-648,u,v));
 Memory m;auto load=[&](const char* name){std::ifstream f(std::string(argv[1])+"/"+name,std::ios::binary);return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{});};
 m.ranges[0x50000]=load("live-party.enc");m.ranges[0x60000]=load("live-coordinates.enc");assert(m.ranges[0x50000].size()==0xB45&&m.ranges[0x60000].size()==0x75);
 m.Put(0x100000+0x610A670,0x20000);m.Put(0x20030,0x30000);m.Put(0x30008,0x40000);m.Put(0x30010,0x40040);
 std::vector<uint8_t> descriptors(64);auto put=[&](size_t off,uint64_t v){std::memcpy(descriptors.data()+off,&v,8);};
 put(0,(0xaabbccddull<<32)|za::KParty);put(8,0x50000);put(16,0xB45);put(32,(0x12345678ull<<32)|za::KCoordinates);put(40,0x60000);put(48,0x75);m.ranges[0x40000]=descriptors;
 EdenDsmodHostApi host{};host.main_base=0x100000;host.userdata=&m;uint8_t build[]={0xB1,0xF1,0x2F,0xD9,0x19,0xEA,0xE8,0x6A};std::memcpy(host.build_id,build,8);
 host.is_mapped=[](void* p,uint64_t at,size_t size)->EdenDsmodBool{return static_cast<Memory*>(p)->Read(at,nullptr,size);};
 host.read_memory=[](void* p,uint64_t at,void* out,size_t size)->EdenDsmodBool{return static_cast<Memory*>(p)->Read(at,out,size);};
 // Production pointer reads must never enter the host GPU-synchronizing ReadBlock path.
 auto production=host;
 production.get_read_pointer=[](void* p,uint64_t at,size_t size)->const uint8_t*{auto& ranges=static_cast<Memory*>(p)->ranges;auto it=ranges.upper_bound(at);if(it==ranges.begin())return nullptr;--it;const auto off=at-it->first;if(size>4096-(at&4095)||off>it->second.size()||size>it->second.size()-off)return nullptr;return it->second.data()+off;};
 production.read_memory=[](void*,uint64_t,void*,size_t)->EdenDsmodBool{assert(false&&"GPU read must not be invoked");return false;};
 auto safe=za::LiveReader{}.Poll(production);assert(safe.party_valid&&safe.party_count==6);
 za::LiveReader reader;auto first=reader.Poll(host);assert(first.party_valid&&first.coordinates_valid&&first.party_count==6);assert(first.party[0].species==716&&first.party[0].hp==318);
 // Modify encrypted HP in each shuffled word until the verified logical HP field is reached.
 // XOR streams preserve the delta; checksum is adjusted for the changed plaintext word.
 const auto original=m.ranges[0x50000];bool found=false;
 for(size_t at=8;at<0x148;at+=2){
  auto& encrypted=m.ranges[0x50000];encrypted=original;
  const uint16_t hp_delta=318^100, new_checksum=uint16_t(first.party[0].checksum-318+100), checksum_delta=first.party[0].checksum^new_checksum;
  encrypted[5+at]^=uint8_t(hp_delta);encrypted[5+at+1]^=uint8_t(hp_delta>>8);
  encrypted[5+6]^=uint8_t(checksum_delta);encrypted[5+7]^=uint8_t(checksum_delta>>8);
  auto changed=reader.Poll(host);if(changed.party_valid&&changed.party[0].hp==100){found=true;break;}
 }
 assert(found);
 // A separate active PA9 overrides the unchanged save snapshot, then updates through cache.
 m.ranges[0x50000]=original;
 auto plain=original;za::Cipher cipher(za::KParty);for(auto& b:plain)b^=cipher.Next();
 m.Put(host.main_base+0x610A710,0x70000);m.Put(0x700B0,0x71000);
 auto& pool=m.ranges[0x1000000]; // use a distinct pool, outside fixture pointer metadata
 m.Put(0x71008,0x1000100);pool.resize((256u<<10)+za::kStoredSize);
 std::copy_n(plain.data()+5,za::kStoredSize,pool.data()+0x100);
 auto active=reader.Poll(host);assert(active.hp_valid[0]&&active.party[0].hp==318);
 const auto original_record=pool;found=false;
 for(size_t at=8;at<za::kStoredSize;at+=2){
  pool=original_record;const uint16_t delta=318^182, checksum=uint16_t(first.party[0].checksum-318+182), cd=first.party[0].checksum^checksum;
  pool[0x100+at]^=uint8_t(delta);pool[0x101+at]^=uint8_t(delta>>8);
  pool[0x106]^=uint8_t(cd);pool[0x107]^=uint8_t(cd>>8);
  reader=za::LiveReader{};auto updated=reader.Poll(host);if(updated.hp_valid[0]&&updated.party[0].hp==182){found=true;break;}
 }
 assert(found);const auto damaged_record=pool;
 pool=original_record;assert(reader.Poll(host).party[0].hp==318);
 pool=damaged_record;assert(reader.Poll(host).party[0].hp==182);
 pool[0x108]^=1;auto torn=reader.Poll(host);assert(!torn.hp_valid[0]&&torn.party[0].hp==182);
 uint16_t confirmed=0;assert(reader.LastHP(first.party[0],confirmed)&&confirmed==182);
 auto other=first.party[0];++other.pid;assert(!reader.LastHP(other,confirmed));
 // A readable PA9 page must not be lost because the rest of its scan chunk is unmapped.
 pool=damaged_record;pool.resize(4096);reader=za::LiveReader{};auto sparse=reader.Poll(host);assert(sparse.hp_valid[0]&&sparse.party[0].hp==182);
 // Android allocation order can place a party record before the allocator anchor.
 pool.assign(4096,0);m.ranges[0xFC0000]=damaged_record;m.ranges[0xFC0000].resize(4096);
 reader=za::LiveReader{};assert(!reader.Poll(host).hp_valid[0]);auto preceding=reader.Poll(host);assert(preceding.hp_valid[0]&&preceding.party[0].hp==182);
 pool=damaged_record;pool.resize(4096);m.ranges.erase(0xFC0000);
 // Runtime registry slots differ across boots; discover by structure and validate every read.
 auto& registry=m.ranges[host.main_base+0x40F0000];registry.resize(0x20000);
 uint64_t object=0x1100000000;std::memcpy(registry.data()+0x12340,&object,8);
 auto& pos=m.ranges[object];pos.resize(80);uint64_t vt=host.main_base+0x3DA4348;std::memcpy(pos.data(),&vt,8);pos[0x18]='t';pos[0x19]='1';
 float x=-345,z=-649;std::memcpy(pos.data()+0x38,&x,4);std::memcpy(pos.data()+0x40,&z,4);
 reader=za::LiveReader{};auto located=reader.Poll(host);assert(located.position_live&&located.x==x&&located.z==z);
 x=-330;std::memcpy(pos.data()+0x38,&x,4);auto moved=reader.Poll(host);assert(moved.position_live&&moved.x==x);
 pos[0]^=1;assert(!reader.Poll(host).position_live);
 auto& huds=m.ranges[host.main_base+0x4800000];huds.resize(0x100000);uint64_t hud=0x1101000000;std::memcpy(huds.data()+0x95000,&hud,8);
 m.Put(hud,host.main_base+0x3DE1A20);m.Put(hud+0xB8,0x1102000000);m.Put(0x1102000368,0x1103000000);
 auto& sel=m.ranges[0x1103000000];sel.resize(16);vt=host.main_base+0x3BD3B30;std::memcpy(sel.data(),&vt,8);sel[8]=4;sel[12]=6;
 reader=za::LiveReader{};assert(reader.Poll(host).selected_index==4);sel[8]=5;assert(reader.Poll(host).selected_index==5);
 sel[12]=1;assert(reader.Poll(host).selected_index==-1);
 // NCE heap addresses lie outside the former Windows-only 0x10..0x18 window.
 m.ranges.erase(host.main_base+0x40F0000);m.ranges.erase(host.main_base+0x4800000);
 const uint64_t nce_root=0x2180010000ull,nce_hud=0x2180020000ull;
 host.get_heap_begin=[](void*)->uint64_t{return 0x2180000000ull;};
 host.get_heap_end=[](void*)->uint64_t{return 0x2190000000ull;};
 auto& bss=m.ranges[host.main_base+0x610B000];bss.resize(0x1000);std::memcpy(bss.data()+0x700,&nce_root,8);std::memcpy(bss.data()+0x708,&nce_hud,8);
 auto& embedded=m.ranges[nce_root];embedded.resize(512);std::copy(pos.begin(),pos.end(),embedded.begin()+0xA8);vt=host.main_base+0x3DA4348;std::memcpy(embedded.data()+0xA8,&vt,8);
 auto& embedded_hud=m.ranges[nce_hud];embedded_hud.resize(512);vt=host.main_base+0x3DE1A20;std::memcpy(embedded_hud.data()+0xE8,&vt,8);uint64_t view=0x1102000000;std::memcpy(embedded_hud.data()+0xE8+0xB8,&view,8);sel[12]=6;sel[8]=3;
 reader=za::LiveReader{};auto nce=reader.Poll(host);assert(nce.position_live&&nce.x==x&&nce.selected_index==3);
 // Script objects may live in a mapped alias outside the process's regular heap.
 host.get_heap_begin=[](void*)->uint64_t{return 0x1080000000ull;};host.get_heap_end=[](void*)->uint64_t{return 0x1100000000ull;};
 reader=za::LiveReader{};auto alias=reader.Poll(host);assert(alias.position_live&&alias.selected_index==3);
 // The map presenter has explicit closed/map/destinations states and a real destination ID.
 const uint64_t ui=0x2181000000ull,map=ui+0x180;
 m.Put(host.main_base+0x610A000,0x1104000000);m.Put(0x11040005F8,0x1105000000);m.Put(0x11050006A8,0x1106000000);
 m.Put(0x1106000000,host.main_base+0x3BCE178);m.Put(0x1106000038,ui);
 auto& ui_bytes=m.ranges[ui];ui_bytes.resize(0x1000);vt=host.main_base+0x3DF1D48;std::memcpy(ui_bytes.data()+0x180,&vt,8);std::memcpy(ui_bytes.data()+0x190,&map,8);ui_bytes[0x180+0x208]=2;ui_bytes[0x180+0x2A8]=105;ui_bytes[0x180+0x585]=1;ui_bytes[0x180+0x448]=9;
 reader=za::LiveReader{};auto mapped=reader.Poll(host);assert(mapped.map_mode==2&&mapped.map_destination==105);
 ui_bytes[0x180+0x208]=3;assert(reader.Poll(host).map_mode==3);
 ui_bytes[0x180+0x208]=0;ui_bytes[0x180+0x585]=0;ui_bytes[0x180+0x448]=32;assert(reader.Poll(host).map_mode==3);ui_bytes[0x180+0x208]=2;assert(reader.Poll(host).map_mode==4);ui_bytes[0x180+0x448]=9;assert(reader.Poll(host).map_mode==4);ui_bytes[0x180+0x585]=1;ui_bytes[0x180+0x448]=9;ui_bytes[0x180+0x208]=0;assert(reader.Poll(host).map_mode==0);
 ui_bytes[0x180+0x208]=255;assert(reader.Poll(host).map_mode==-1);
 // A presenter created after the first sweep must be retried; idle ticks cannot
 // keep postponing its retry deadline forever.
 host.get_tick=[](void* p)->uint64_t{return static_cast<Memory*>(p)->tick;};
 reader=za::LiveReader{};ui_bytes[0x180]^=1;
 for(int i=0;i<32;++i)reader.Poll(host);
 m.tick=100;reader.Poll(host);ui_bytes[0x180]^=1;ui_bytes[0x180+0x208]=2;
 m.tick=301;assert(reader.Poll(host).map_mode==2);
 using C=za::TravelController::Command;za::TravelController travel;
 travel.Start(105,0);assert(travel.Poll(0,3,0)==C::OpenMap);assert(travel.Poll(90,0,0)==C::Destinations);
 assert(travel.Poll(150,2,104)==C::Down);assert(travel.Poll(174,2,105)==C::None);assert(travel.Poll(186,2,105)==C::Confirm&&travel.Active());assert(travel.Poll(246,4,105)==C::Confirm&&!travel.Active());
 travel.Start(105,0);assert(travel.Poll(0,2,105)==C::None);assert(travel.Poll(12,2,105)==C::Confirm);assert(travel.Poll(72,3,105)==C::None&&!travel.Active());
 travel.Start(105,0);assert(travel.Poll(0,2,105)==C::None);assert(travel.Poll(12,2,105)==C::Confirm);assert(travel.Poll(72,4,104)==C::None&&!travel.Active());
 travel.Start(105,0);assert(travel.Poll(0,4,105)==C::OpenMap);assert(travel.Poll(90,4,105)==C::None&&!travel.Active());
 travel.Start(105,0);assert(travel.Poll(0,0,0)==C::OpenMap);assert(travel.Poll(90,3,0)==C::OpenMap);assert(travel.Poll(180,0,0)==C::Destinations);assert(travel.Poll(240,2,0)==C::Down);assert(travel.Poll(264,2,102)==C::Down);
 travel.Start(205,0);assert(travel.Poll(0,2,204)==C::Down);assert(travel.Poll(24,2,206)==C::None&&!travel.Active());
 travel.Start(105,0);travel.Cancel();assert(travel.Poll(100,2,105)==C::None&&!travel.Active());
 travel.Start(105,0);assert(travel.Poll(0,1,105)==C::None);assert(travel.Poll(2401,2,105)==C::None&&!travel.Active());
 // Box exchange is a readback-driven state machine: no A before a proven pickup.
 {
  using B=za::BoxSwapController::Command;za::BoxSwapController swap;za::LiveSnapshot state;
  za::Entity source,victim;source.encryption_constant=11;source.pid=12;victim.encryption_constant=21;victim.pid=22;
  state.party_valid=state.box_valid=true;state.box_view_index=1;state.box_view[13]=source;state.party[0]=victim;state.boxes.valid=true;state.boxes.cursor=13;
  swap.Start(1,13,0,source,victim,0);assert(swap.Poll(0,state)==B::Right);
  // A closed/inactive box keeps its cached cursor: focus proof must refuse it.
  assert(swap.Poll(22,state)==B::None&&!swap.Active());
  swap.Start(1,13,0,source,victim,0);assert(swap.Poll(0,state)==B::Right);state.boxes.cursor=14;
  assert(swap.Poll(22,state)==B::None);assert(swap.Poll(23,state)==B::None);assert(swap.Poll(24,state)==B::Left);
  state.boxes.cursor=13;assert(swap.Poll(46,state)==B::Pick);assert(swap.Poll(68,state)==B::Right);
  state.boxes={true,14,0,1,13,1,14};assert(swap.Poll(90,state)==B::None);
  assert(swap.Poll(91,state)==B::Up);state.boxes.cursor=8;state.boxes.target_slot=8;assert(swap.Poll(113,state)==B::Up);
  state.boxes.cursor=2;state.boxes.target_slot=2;assert(swap.Poll(135,state)==B::Up);
  state.boxes.target_box=998;state.boxes.target_slot=state.boxes.party_cursor=2;assert(swap.Poll(157,state)==B::Left);
  state.boxes.target_slot=state.boxes.party_cursor=1;assert(swap.Poll(179,state)==B::Left);
  state.boxes.target_slot=state.boxes.party_cursor=0;assert(swap.Poll(201,state)==B::None);assert(swap.Poll(213,state)==B::Confirm);
  state.party[0]=source;state.box_view[13]={};state.boxes.source_box=998;state.boxes.source_slot=0;
  assert(swap.Poll(253,state)==B::None);assert(swap.Poll(254,state)==B::Down);
  state.boxes.target_box=1;state.boxes.cursor=state.boxes.target_slot=0;assert(swap.Poll(276,state)==B::Down);
  state.boxes.cursor=state.boxes.target_slot=6;assert(swap.Poll(298,state)==B::Down);
  state.boxes.cursor=state.boxes.target_slot=12;assert(swap.Poll(320,state)==B::Right);
  state.boxes.cursor=state.boxes.target_slot=13;assert(swap.Poll(342,state)==B::None);assert(swap.Poll(354,state)==B::Confirm);
  state.boxes={true,13,0,0,0,0,0};state.box_view[13]=victim;assert(swap.Poll(394,state)==B::None&&swap.Completed()&&!swap.Active());
  state.party[0]=victim;state.box_view[13]=source;
  swap.Start(1,13,0,source,victim,0);assert(swap.Poll(0,state)==B::Right);state.boxes.cursor=14;swap.Poll(22,state);swap.Poll(23,state);swap.Poll(24,state);
  state.boxes.cursor=13;swap.Poll(46,state);swap.Poll(68,state);state.boxes.cursor=14;
  assert(swap.Poll(90,state)==B::Back&&!swap.Active()); // an uncertain Y is cancelled, never repeated
  swap.Cancel();assert(swap.Poll(200,state)==B::None&&!swap.Active());
 }
 // The current box is a checksum-validated live SCBlock, not a save-file rewrite.
 {
  Memory bm=m;auto bh=host;bh.userdata=&bm;bm.tick=500;bm.ranges[0x50000]=original;
  std::vector<uint8_t> bd(96);auto set=[&](size_t off,uint64_t value){std::memcpy(bd.data()+off,&value,8);};
  const uint32_t box_size=za::kBoxCount*za::kBoxSlotCount*za::kBoxSlotSize;
  std::vector<uint8_t> enc(box_size+5);enc[0]=4;std::memcpy(enc.data()+1,&box_size,4);
  std::copy_n(plain.data()+5,za::kPartySize,enc.data()+5+(2*30+13)*za::kBoxSlotSize);
  za::Cipher bc(za::KBox);for(auto& v:enc)v^=bc.Next();bm.ranges[0x1800000]=enc;
  set(0,za::KBox);set(8,0x1800000);set(16,enc.size());set(32,za::KParty);set(40,0x50000);set(48,0xB45);set(64,za::KCoordinates);set(72,0x60000);set(80,0x75);
  bm.ranges[0x40000]=bd;bm.Put(0x30010,0x40060);
  auto& ub=bm.ranges[ui];ub.assign(0x5000,0);auto put=[&](size_t off,uint64_t value){std::memcpy(ub.data()+off,&value,8);};
  put(0x180,bh.main_base+0x3DB4880);put(0x190,ui+0x180);put(0x180+0xD0,ui+0x1000);
  put(0x1048,ui+0x1200);put(0x1378,ui+0x1400);put(0x15A0,ui+0x1600);
  put(0x1600,bh.main_base+0x3DB4D18);put(0x1610,ui+0x1600);put(0x16C8,(13ull<<32)|2);put(0x16D0,(1ull<<32)|998);
  put(0x180+0xE0,ui+0x2000);put(0x180+0xE8,ui+0x2800);
  put(0x2000,bh.main_base+0x3BD0E60);put(0x2368,ui+0x2400);put(0x2400,bh.main_base+0x3BD3B30);put(0x2408,(6ull<<32)|1);
  put(0x2800,bh.main_base+0x3BD0E60);put(0x2B68,ui+0x2C00);put(0x2C00,bh.main_base+0x3BD3B30);put(0x2C08,(6ull<<32)|1);
  za::LiveReader boxes;auto result=boxes.Poll(bh,true,2);
  assert(result.box_valid&&result.box_view_index==2&&result.box_view[13].species==716);
  assert(result.boxes.valid&&result.boxes.source_box==2&&result.boxes.source_slot==13&&result.boxes.target_box==998&&result.boxes.party_cursor==1);
  // A new session can place BoxPane 720 MiB beyond the UI allocator's start.
  const uint64_t late=ui+(720ull<<20)+0x180;bm.ranges[late-0x180]=std::vector<uint8_t>(4096,0);
  std::copy_n(ub.data()+0x180,0x100,bm.ranges[late-0x180].data()+0x180);
  std::memcpy(bm.ranges[late-0x180].data()+0x190,&late,8);put(0x180,0);za::LiveReader relocated;
  for(int n=0;n<190;++n){++bm.tick;result=relocated.Poll(bh,true,2);if(result.boxes.valid)break;}
  assert(result.boxes.valid&&result.boxes.source_box==2);

 }
 m.ranges[0x50000][5+8]^=1;assert(!reader.Poll(host).party_valid);
 {auto& signature=m.ranges[host.main_base+0x3da4348];signature.resize(24);uint64_t values[]={host.main_base+0x2add89c,host.main_base+0x269fe70,host.main_base+0x269fe7c};std::memcpy(signature.data(),values,24);}
 {auto& signature=m.ranges[host.main_base+0x3de1a20];signature.resize(24);uint64_t values[]={host.main_base+0x2c05980,host.main_base+0x269fe70,host.main_base+0x269fe7c};std::memcpy(signature.data(),values,24);}
 {auto& signature=m.ranges[host.main_base+0x3bd3b30];signature.resize(24);uint64_t values[]={host.main_base+0xfe6214,host.main_base+0xfe6290,host.main_base+0xfe56f4};std::memcpy(signature.data(),values,24);}
 // Validate the program itself when the host reports an old build ID.
 host.build_id[0]=0;assert(reader.Poll(host).selected_index==3);
 m.Put(host.main_base+0x3BD3B30,0x12345678);assert(reader.Poll(host).selected_index==-1);

 host.build_id[0]=build[0];m.ranges.erase(0x40000);assert(!reader.Poll(host).party_valid);
 std::puts("live reader: valid data, fresh HP, corruption, unsupported build, unmapped memory OK");
}
