// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "core/mods/dsmod_module_abi.h"
#include "za_save.h"
namespace za {
struct BoxInput {
    bool valid=false;
    int cursor=0,party_cursor=0,source_box=0,source_slot=0,target_box=0,target_slot=0;
    bool EmptyDrag()const{return source_box==0&&source_slot==0&&target_box==0&&target_slot==0;}
};
struct LiveSnapshot {
    bool party_valid=false, coordinates_valid=false, position_live=false;
    std::array<Entity,6> party{};
    int party_count=0;
    int selected_index=-1,map_mode=-1,map_destination=0;
    BoxInput boxes;
    bool box_valid=false;
    int box_view_index=0;
    std::array<Entity,30> box_view{};
    std::array<bool,6> hp_valid{};
    std::unordered_map<u64,u64> quest_work;
    std::string area,diagnostic;
    float x=0,y=0,z=0,rotation=0;
};
class LiveReader {
public:
    LiveSnapshot Poll(const EdenDsmodHostApi& host,bool inspect_boxes=false,int requested_box=-1);
    // Last checksum-verified combat HP, keyed by PID + EC, survives transient torn reads.
    bool LastHP(const Entity& entity, std::uint16_t& hp) const;
private:
    std::unordered_map<std::uint64_t,std::uint16_t> confirmed_hp_;
    std::unordered_map<std::uint64_t,std::uint32_t> confirmed_exp_;
    std::array<std::uint64_t,6> hp_records_{};
    std::array<std::uint32_t,6> hp_identity_{};
    std::uint64_t position_object_=0, selector_slot_=0, selector_object_=0, discovery_retry_=0;
    std::uint64_t map_object_=0,ui_scan_begin_=0,ui_scan_cursor_=0,ui_scan_retry_=0;
    std::uint64_t box_object_=0,box_drag_=0,box_scan_begin_=0,box_scan_cursor_=0,box_scan_retry_=0;
    std::uint64_t box_entry_=0,next_box_poll_=0;
    int cached_box_index_=-1;
    bool cached_box_valid_=false;
    std::array<Entity,30> cached_box_{};
    std::uint64_t pool_begin_=0, pool_cursor_=0, pool_retry_=0;
    std::uint64_t party_entry_=0, coordinates_entry_=0, quest_entry_=0, mable_entry_=0, table_=0, next_index_=0;
};
}
