// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only developer probe for this Windows Eden build. PDB field offsets are resolved at
// runtime; no target code is executed and no process memory is written.
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <stdexcept>
#include <algorithm>
#include <deque>
#include <unordered_set>
#include "../../native/modules/za_live.cpp"
#include "core/mods/dsmod_module_abi.h"
#pragma comment(lib,"dbghelp.lib")

HANDLE process;
DWORD64 module;
uint64_t guest_arena;
template<class T> T Read(uint64_t address) {
    T result{}; SIZE_T got{};
    if(!ReadProcessMemory(process,reinterpret_cast<void*>(address),&result,sizeof(result),&got) || got!=sizeof(result))
        throw std::runtime_error("ReadProcessMemory failed");
    return result;
}
ULONG Field(const char* type,const wchar_t* member) {
    char buffer[sizeof(SYMBOL_INFO)+2048]{}; auto* symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=2048;
    if(!SymGetTypeFromName(process,module,type,symbol)) {std::printf("missing type %s (%lu)\n",type,GetLastError());throw std::runtime_error("type missing");}
    DWORD count{};SymGetTypeInfo(process,module,symbol->TypeIndex,TI_GET_CHILDRENCOUNT,&count);
    std::vector<uint8_t> storage(sizeof(TI_FINDCHILDREN_PARAMS)+count*sizeof(ULONG));
    auto* children=reinterpret_cast<TI_FINDCHILDREN_PARAMS*>(storage.data());children->Count=count;children->Start=0;
    if(!SymGetTypeInfo(process,module,symbol->TypeIndex,TI_FINDCHILDREN,children))throw std::runtime_error("children missing");
    for(DWORD i=0;i<count;++i) {
        WCHAR* name{};ULONG offset{};
        if(!SymGetTypeInfo(process,module,children->ChildId[i],TI_GET_SYMNAME,&name))continue;
        const bool match=std::wcscmp(name,member)==0;LocalFree(name);
        if(match && SymGetTypeInfo(process,module,children->ChildId[i],TI_GET_OFFSET,&offset))return offset;
    }
    std::printf("missing member %s::%ls\n",type,member);throw std::runtime_error("field missing");
}
DWORD64 Symbol(const char* name) {
    char buffer[sizeof(SYMBOL_INFO)+2048]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=2048;
    return SymFromName(process,name,symbol)?symbol->Address:0;
}
int main(int argc,char** argv) {
    try {
        DWORD pid=0;PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(Process32FirstW(snapshot,&entry))do{if(_wcsicmp(entry.szExeFile,L"eden.exe")==0){pid=entry.th32ProcessID;break;}}while(Process32NextW(snapshot,&entry));
        CloseHandle(snapshot);if(!pid){std::puts("eden.exe not running");return 2;}
        process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);
        if(!process){std::printf("OpenProcess: %lu\n",GetLastError());return 3;}
        if(argc==2&&std::strcmp(argv[1],"env")==0){
            PROCESS_BASIC_INFORMATION info{};using Query=LONG(NTAPI*)(HANDLE,PROCESSINFOCLASS,PVOID,ULONG,PULONG);
            auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationProcess"));
            if(!query||query(process,ProcessBasicInformation,&info,sizeof(info),nullptr)<0)return 15;
            auto parameters=Read<uint64_t>(reinterpret_cast<uint64_t>(info.PebBaseAddress)+0x20);auto environment=Read<uint64_t>(parameters+0x80);
            std::vector<wchar_t> block(32768);SIZE_T got{};while(got<block.size()*2){SIZE_T part{};const auto n=std::min<SIZE_T>(4096-((environment+got)&4095),block.size()*2-got);if(!ReadProcessMemory(process,reinterpret_cast<void*>(environment+got),reinterpret_cast<char*>(block.data())+got,n,&part)||!part)break;got+=part;}if(!got)return 16;
            size_t matches=0;for(size_t at=0;at<got/2&&block[at];){size_t end=at;while(end<got/2&&block[end])++end;std::wstring entry(block.data()+at,end-at);if(entry.starts_with(L"EDEN_")){std::wprintf(L"%ls\n",entry.c_str());++matches;}at=end+1;}
            std::printf("EDEN environment entries: %zu\n",matches);CloseHandle(process);return 0;
        }
        MODULEENTRY32W image{};image.dwSize=sizeof(image);
        snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
        if(!Module32FirstW(snapshot,&image))return 4;CloseHandle(snapshot);
        char path[32768];WideCharToMultiByte(CP_UTF8,0,image.szExePath,-1,path,sizeof(path),nullptr,nullptr);
        SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS);
        if(!SymInitialize(process,nullptr,FALSE))return 5;
        module=SymLoadModuleEx(process,nullptr,path,nullptr,reinterpret_cast<DWORD64>(image.modBaseAddr),image.modBaseSize,nullptr,0);
        if(!module){std::printf("SymLoadModule: %lu\n",GetLastError());return 6;}
        if(argc==2 && std::strcmp(argv[1],"symbols")==0) {
            for(const char* name:{"QtCommon::system","Core::System::ModRuntime","Core::Mods::ModRuntime::OwnerMemory",
                    "Core::Memory::Memory::GetPointerSilent","Kernel::KProcess::GetMemory","Core::System::ApplicationMemory"}) {
                const auto address=Symbol(name);
                std::printf("%s actual=%llx image=%llx\n",name,address,address?address-module+0x140000000ull:0);
                if(!address)continue;
                std::array<uint8_t,128> bytes{};SIZE_T got{};
                ReadProcessMemory(process,reinterpret_cast<void*>(address),bytes.data(),bytes.size(),&got);
                for(size_t i=0;i<got;++i)std::printf("%02x",bytes[i]);std::puts("");
                if(std::strcmp(name,"QtCommon::system")==0)std::printf("system=%llx impl=%llx\n",Read<uint64_t>(address),Read<uint64_t>(Read<uint64_t>(address)));
            }
            return 0;
        }
        const auto qt_system=Symbol("QtCommon::system");
        auto system=qt_system ? Read<uint64_t>(qt_system) : Symbol("Core::System::GetInstance::system");
        if(!system)system=Symbol("?system@?1??GetInstance@System@Core@@SAAEAV12@XZ@4V23@A");
        if(!system) {
            SymEnumSymbols(process,module,"*GetInstance*",+[](PSYMBOL_INFO info,ULONG,void*)->BOOL {
                if(std::strstr(info->Name,"System"))std::printf("symbol %llx %s\n",info->Address,info->Name);return TRUE;
            },nullptr);
            return 7;
        }
        const auto impl=Read<uint64_t>(system); // Core::System contains only its unique_ptr<Impl>.
        if(argc==2&&std::strcmp(argv[1],"hid")==0){
            for(const char* name:{"Core::System::GetInputSubsystem","Core::System::HIDCore","Core::HID::EmulatedController::GetNpadButtons"}){
                auto address=Symbol(name);std::printf("%s %llx\n",name,address);if(address){auto bytes=Read<std::array<uint8_t,96>>(address);for(auto b:bytes)std::printf("%02x",b);std::puts("");}
            }
            for(auto member:{L"hid_core",L"input_subsystem"})try{std::printf("Impl %ls %lx\n",member,Field("Core::System::Impl",member));}catch(...){}
            return 0;
        }
        const auto rt_function=Read<std::array<uint8_t,32>>(Symbol("Core::System::ModRuntime"));
        uint32_t rt_offset=0;
        for(size_t i=0;i+7<=rt_function.size();++i)
            if(rt_function[i]==0x48&&rt_function[i+1]==0x8d&&rt_function[i+2]==0x82)
                std::memcpy(&rt_offset,rt_function.data()+i+3,4);
        if(!rt_offset)throw std::runtime_error("unrecognized ModRuntime accessor");
        const auto runtime=impl+rt_offset;
        EdenDsmodHostApi host{};
        const auto memory_function=Read<std::array<uint8_t,16>>(Symbol("Core::Mods::ModRuntime::OwnerMemory"));
        if(memory_function[0]!=0x48||memory_function[1]!=0x8b||memory_function[2]!=0x81||memory_function[7]!=0x48||memory_function[8]!=0x05)
            throw std::runtime_error("unrecognized OwnerMemory accessor");
        uint32_t owner_offset{},memory_offset{};
        std::memcpy(&owner_offset,memory_function.data()+3,4);
        std::memcpy(&memory_offset,memory_function.data()+9,4);
        for(size_t i=0;i<0x7000;i+=8) {
            const auto title=Read<uint64_t>(runtime+i);
            if(title!=0x0100F43008C44000ull || i<40)continue;
            const auto candidate=Read<EdenDsmodHostApi>(runtime+i-40);
            if(candidate.userdata==reinterpret_cast<void*>(runtime) && candidate.abi_hash==EDEN_DSMOD_MODULE_ABI_HASH) {host=candidate;break;}
        }
        if(!host.main_base)throw std::runtime_error("module host not found");
        if(argc>=2&&std::strcmp(argv[1],"hostinfo")==0){
            std::printf("main=%llx size=%llx heap=%llx..%llx\n",host.main_base,host.main_size,
                Read<uint64_t>(runtime+Field("Core::Mods::ModRuntime",L"heap_low")),Read<uint64_t>(runtime+Field("Core::Mods::ModRuntime",L"heap_high")));return 0;}
        const auto owner=Read<uint64_t>(runtime+owner_offset);
        const auto memory=owner+memory_offset;
        const auto pointer_function=Read<std::array<uint8_t,16>>(Symbol("Core::Memory::Memory::GetPointerSilent"));
        if(pointer_function[0]!=0x48||pointer_function[1]!=0x8b||pointer_function[2]!=0x49||pointer_function[4]!=0xe9)
            throw std::runtime_error("unrecognized memory accessor");
        const auto memory_impl=Read<uint64_t>(memory+pointer_function[3]);
        const auto table=Read<uint64_t>(memory_impl+0x10); // confirmed GetPointerImpl disassembly
        const auto arena=Read<uint64_t>(table+0x30); // Common::PageTable, immediately after entries
        std::printf("pid=%lu main=%llx size=%llx arena=%llx runtime=%llx\n",pid,host.main_base,host.main_size,arena,runtime);
        if(!arena)return 9;
        if(argc>=4&&(std::strcmp(argv[1],"graph")==0||std::strcmp(argv[1],"graphread")==0)){
            std::vector<uint64_t> queue;std::unordered_set<uint64_t> visited;const bool follow=std::strcmp(argv[1],"graph")==0;
            if(follow){std::string seeds=argv[2];size_t off=0;while(off<seeds.size()){auto end=seeds.find(',',off);queue.push_back(std::strtoull(seeds.substr(off,end-off).c_str(),nullptr,16));if(end==std::string::npos)break;off=end+1;}
                const auto a=Read<uint64_t>(arena+host.main_base+0x610A710);if(a)queue.push_back(a);
            }else{FILE* input=std::fopen(argv[2],"rb");if(!input)return 40;uint64_t at;std::array<uint8_t,512> skip;while(std::fread(&at,8,1,input)==1){queue.push_back(at);std::fread(skip.data(),1,skip.size(),input);}std::fclose(input);}
            FILE* output=std::fopen(argv[3],"wb");if(!output)return 41;size_t total=0;
            for(size_t i=0;i<queue.size()&&i<12000;++i){const auto at=queue[i];if(!visited.insert(at).second)continue;std::array<uint8_t,512> b{};SIZE_T got{};
                if(!ReadProcessMemory(process,reinterpret_cast<void*>(arena+at),b.data(),b.size(),&got)||got!=b.size())continue;
                std::fwrite(&at,8,1,output);std::fwrite(b.data(),1,b.size(),output);++total;
                if(follow)for(size_t off=0;off+8<=b.size();off+=8){auto ptr=za::At<uint64_t>(b.data()+off);if(!(ptr&7)&&ptr>=0x114c500000ull&&ptr<0x1200000000ull&&!visited.contains(ptr)&&queue.size()<24000)queue.push_back(ptr);}
            }std::fclose(output);std::printf("graph objects=%zu queued=%zu\n",total,queue.size());return 0;
        }
        if(argc>=2&&std::strcmp(argv[1],"hudroots")==0){
            auto ptr=[&](uint64_t at){uint64_t v=0;SIZE_T got=0;ReadProcessMemory(process,reinterpret_cast<void*>(arena+at),&v,8,&got);return got==8?v:0;};
            std::vector<uint8_t> table(1<<20);SIZE_T got=0;ReadProcessMemory(process,reinterpret_cast<void*>(arena+host.main_base+0x4800000),table.data(),table.size(),&got);
            for(size_t i=0;i+8<=got;i+=8){auto hud=za::At<uint64_t>(table.data()+i);if(hud<0x1000000000ull||hud>=0x1800000000ull)continue;auto view=ptr(hud+0xB8),sel=ptr(view+0x368);if(ptr(sel)==host.main_base+0x3BD3B30){auto info=ptr(sel+8);std::printf("slot=%llx hud=%llx vt=%llx view=%llx selector=%llx index=%u count=%u\n",0x4800000+i,hud,ptr(hud)-host.main_base,view,sel,uint32_t(info),uint32_t(info>>32));}}
            return 0;
        }
        if(argc>=3&&std::strcmp(argv[1],"readselectors")==0){
            FILE* input=std::fopen(argv[2],"rb");if(!input)return 26;uint64_t at;
            while(std::fread(&at,8,1,input)==1){auto b=Read<std::array<uint8_t,16>>(arena+at);std::printf("%llx index=%u count=%u\n",at,za::At<uint32_t>(b.data()+8),za::At<uint32_t>(b.data()+12));}std::fclose(input);return 0;
        }
        if(argc>=4&&std::strcmp(argv[1],"readpositions")==0){
            FILE* input=std::fopen(argv[2],"rb"),*output=std::fopen(argv[3],"w");if(!input||!output)return 26;uint64_t at;size_t count=0;
            while(std::fread(&at,8,1,input)==1){std::array<float,3> xyz{};SIZE_T got{};if(ReadProcessMemory(process,reinterpret_cast<void*>(arena+at),xyz.data(),12,&got)&&got==12){std::fprintf(output,"%llx,%.8f,%.8f,%.8f\n",at,xyz[0],xyz[1],xyz[2]);++count;}}
            std::fclose(input);std::fclose(output);std::printf("positions=%zu\n",count);return 0;
        }
        if(argc>=3&&std::strcmp(argv[1],"scanpos")==0){
            auto root=Read<uint64_t>(arena+host.main_base+0x610A670),manager=Read<uint64_t>(arena+root+0x30),begin=Read<uint64_t>(arena+manager+8),end=Read<uint64_t>(arena+manager+16);std::array<float,3> want{};bool located=false;
            for(auto at=begin;at<end;at+=32){auto entry=Read<std::array<uint8_t,32>>(arena+at);if(za::At<uint32_t>(entry.data())!=za::KCoordinates)continue;auto address=za::At<uint64_t>(entry.data()+8);auto bytes=Read<std::array<uint8_t,117>>(arena+address);za::Cipher cipher(za::KCoordinates);for(auto& b:bytes)b^=cipher.Next();std::memcpy(want.data(),bytes.data()+5+0x20,12);located=true;break;}
            if(!located)return 24;std::printf("position %.5f %.5f %.5f\n",want[0],want[1],want[2]);FILE* output=std::fopen(argv[2],"wb");if(!output)return 25;
            uint64_t cursor=arena+0x1000000000ull,stop=arena+0x1800000000ull;std::vector<uint8_t> buffer((1<<20)+16);size_t matches=0;
            while(cursor<stop){MEMORY_BASIC_INFORMATION info{};if(!VirtualQueryEx(process,reinterpret_cast<void*>(cursor),&info,sizeof(info)))break;auto end=std::min(stop,reinterpret_cast<uint64_t>(info.BaseAddress)+info.RegionSize);if(end<=cursor)break;
                if(info.State==MEM_COMMIT&&!(info.Protect&(PAGE_NOACCESS|PAGE_GUARD)))for(auto at=cursor;at<end;at+=1<<20){size_t count=std::min<uint64_t>(buffer.size(),end-at);SIZE_T got{};if(!ReadProcessMemory(process,reinterpret_cast<void*>(at),buffer.data(),count,&got))continue;
                    for(size_t i=0;i+12<=got&&i<(1<<20);i+=4){auto x=za::At<float>(buffer.data()+i);if(std::abs(x-want[0])>0.02f)continue;auto y=za::At<float>(buffer.data()+i+4),z=za::At<float>(buffer.data()+i+8);if(std::abs(y-want[1])<0.05f&&std::abs(z-want[2])<0.02f){auto address=at+i-arena;std::fwrite(&address,8,1,output);if(matches<80)std::printf("position candidate %llx\n",address);++matches;}}
                }cursor=end;
            }std::fclose(output);std::printf("matches=%zu\n",matches);return 0;
        }
        if(argc>=4&&(std::strcmp(argv[1],"scanindex")==0||std::strcmp(argv[1],"scantype")==0||std::strcmp(argv[1],"scanushort")==0)){
            const bool typed=std::strcmp(argv[1],"scantype")==0,short_scan=std::strcmp(argv[1],"scanushort")==0;const auto want=typed?host.main_base+std::strtoull(argv[2],nullptr,16):uint64_t(std::strtoul(argv[2],nullptr,0));FILE* output=std::fopen(argv[3],"wb");if(!output)return 22;
            uint64_t cursor=arena+0x1000000000ull,stop=arena+0x1800000000ull;std::vector<uint8_t> buffer(1<<20);size_t matches=0;
            while(cursor<stop){MEMORY_BASIC_INFORMATION info{};if(!VirtualQueryEx(process,reinterpret_cast<void*>(cursor),&info,sizeof(info)))break;auto end=std::min(stop,reinterpret_cast<uint64_t>(info.BaseAddress)+info.RegionSize);if(end<=cursor)break;
                if(info.State==MEM_COMMIT&&!(info.Protect&(PAGE_NOACCESS|PAGE_GUARD)))for(auto at=cursor;at<end;at+=buffer.size()){size_t count=std::min<uint64_t>(buffer.size(),end-at);SIZE_T got{};if(!ReadProcessMemory(process,reinterpret_cast<void*>(at),buffer.data(),count,&got))continue;
                    for(size_t i=0;i+8<=got;i+=typed?8:short_scan?2:4)if((typed?za::At<uint64_t>(buffer.data()+i):short_scan?za::At<uint16_t>(buffer.data()+i):za::At<uint32_t>(buffer.data()+i))==want){auto address=at+i-arena;std::fwrite(&address,8,1,output);++matches;}
                }cursor=end;
            }std::fclose(output);std::printf("scan %llx matches=%zu\n",want,matches);return 0;
        }
        if(argc>=4&&(std::strcmp(argv[1],"refineindex")==0||std::strcmp(argv[1],"refineushort")==0)){
            const int width=std::strcmp(argv[1],"refineushort")==0?2:4;
            const auto want=uint32_t(std::strtoul(argv[2],nullptr,0));FILE* input=std::fopen(argv[3],"rb");if(!input)return 23;uint64_t address;size_t matches=0;
            while(std::fread(&address,8,1,input)==1){uint32_t value{};SIZE_T got{};if(ReadProcessMemory(process,reinterpret_cast<void*>(arena+address),&value,width,&got)&&got==width&&value==want){std::printf("index candidate %llx\n",address);++matches;}}
            std::fclose(input);std::printf("matches=%zu\n",matches);return 0;
        }
        if(argc>=3&&std::strcmp(argv[1],"paths")==0){
            std::vector<uint64_t> targets;std::string args=argv[2];size_t pos=0;while(pos<args.size()){auto end=args.find(',',pos);targets.push_back(std::strtoull(args.substr(pos,end-pos).c_str(),nullptr,16));if(end==std::string::npos)break;pos=end+1;}
            struct Node {uint64_t at;std::string path;int depth;};std::deque<Node> queue;std::unordered_set<uint64_t> seen;
            auto offer=[&](uint64_t at,std::string path,int depth){if(at>=0x1000000000ull&&at<0x1800000000ull&&!(at&7)&&seen.insert(at).second)queue.push_back({at,std::move(path),depth});};
            if(argc>=4)offer(std::strtoull(argv[3],nullptr,16),"root",0);else for(uint64_t off=0x6109000;off<0x610B000;off+=8){auto at=Read<uint64_t>(arena+host.main_base+off);char path[40];std::snprintf(path,sizeof(path),"%llx",off);offer(at,path,1);}
            size_t tested=0,found=0;std::unordered_set<uint64_t> found_targets;std::array<uint8_t,0x800> bytes{};
            while(!queue.empty()&&tested<100000&&found<targets.size()){auto node=queue.front();queue.pop_front();++tested;SIZE_T got{};if(!ReadProcessMemory(process,reinterpret_cast<void*>(arena+node.at),bytes.data(),bytes.size(),&got)||got<8)continue;
                for(size_t i=0;i+8<=got;i+=8){uint64_t value{};std::memcpy(&value,bytes.data()+i,8);char offset[32];std::snprintf(offset,sizeof(offset),",%zx",i);auto path=node.path+offset;
                    if(std::find(targets.begin(),targets.end(),value)!=targets.end()&&found_targets.insert(value).second){std::printf("path %s -> %llx\n",path.c_str(),value);++found;}
                    if(node.depth<5)offer(value,std::move(path),node.depth+1);
                }
            }std::printf("nodes=%zu queued=%zu matches=%zu\n",tested,queue.size(),found);SymCleanup(process);CloseHandle(process);return 0;
        }
        if(argc>=4&&std::strcmp(argv[1],"block")==0){
            auto key=uint32_t(std::strtoul(argv[2],nullptr,16));auto root=Read<uint64_t>(arena+host.main_base+0x610A670);auto manager=Read<uint64_t>(arena+root+0x30);auto begin=Read<uint64_t>(arena+manager+8),end=Read<uint64_t>(arena+manager+16);
            if(end<begin||end-begin>2048*32)return 17;for(auto at=begin;at<end;at+=32){auto entry=Read<std::array<uint8_t,32>>(arena+at);if(za::At<uint32_t>(entry.data())!=key)continue;
                auto address=za::At<uint64_t>(entry.data()+8),size=za::At<uint64_t>(entry.data()+16);if(size>1<<20)return 18;std::vector<uint8_t> data(size);SIZE_T got{};if(!ReadProcessMemory(process,reinterpret_cast<void*>(arena+address),data.data(),size,&got)||got!=size)return 19;
                za::Cipher cipher(key);for(auto& b:data)b^=cipher.Next();FILE* out=std::fopen(argv[3],"wb");if(!out)return 20;std::fwrite(data.data(),1,data.size(),out);std::fclose(out);std::printf("block %x %zu bytes\n",key,data.size());SymCleanup(process);CloseHandle(process);return 0;
            }return 21;
        }
        if(argc>=3&&std::strcmp(argv[1],"refs")==0){
            std::vector<uint64_t> targets;std::string args=argv[2];size_t start=0;
            while(start<args.size()){auto end=args.find(',',start);targets.push_back(std::strtoull(args.substr(start,end-start).c_str(),nullptr,16));if(end==std::string::npos)break;start=end+1;}
            for(uint64_t offset=0;offset<host.main_size;offset+=1<<20){std::vector<uint8_t> data(std::min<uint64_t>(1<<20,host.main_size-offset));SIZE_T got{};if(!ReadProcessMemory(process,reinterpret_cast<void*>(arena+host.main_base+offset),data.data(),data.size(),&got))continue;
                for(size_t i=0;i+8<=got;i+=8){auto value=za::At<uint64_t>(data.data()+i);for(auto target:targets)if(value<=target&&target-value<=0x100){std::printf("main ref +%llx -> %llx (object+%llx)\n",offset+i,value,target-value);}}
            }
            uint64_t cursor=arena+0x1000000000ull,stop=arena+0x1800000000ull;std::vector<uint8_t> buffer(1<<20);size_t matches=0;
            while(cursor<stop){MEMORY_BASIC_INFORMATION info{};if(!VirtualQueryEx(process,reinterpret_cast<void*>(cursor),&info,sizeof(info)))break;auto end=std::min(stop,reinterpret_cast<uint64_t>(info.BaseAddress)+info.RegionSize);if(end<=cursor)break;
                if(info.State==MEM_COMMIT&&!(info.Protect&(PAGE_NOACCESS|PAGE_GUARD))){for(auto at=cursor;at<end;at+=buffer.size()){size_t count=std::min<uint64_t>(buffer.size(),end-at);SIZE_T got{};if(!ReadProcessMemory(process,reinterpret_cast<void*>(at),buffer.data(),count,&got))continue;
                    for(size_t i=0;i+8<=got;i+=8){uint64_t value{};std::memcpy(&value,buffer.data()+i,8);for(auto target:targets)if(value<=target&&target-value<=0x200){std::printf("ref %llx -> %llx (record+%llx)\n",at+i-arena,value,target-value);++matches;}}
                }}cursor=end;
            }std::printf("references=%zu\n",matches);SymCleanup(process);CloseHandle(process);return 0;
        }
        if(argc>=4 && std::strcmp(argv[1],"chain")==0){
            uint64_t address=host.main_base;std::string offsets=argv[2];size_t start=0;
            while(start<offsets.size()){auto end=offsets.find(',',start);auto part=offsets.substr(start,end-start);address=Read<uint64_t>(arena+address+std::strtoull(part.c_str(),nullptr,16));std::printf(" -> %llx",address);if(end==std::string::npos)break;start=end+1;}
            const auto size=std::strtoull(argv[3],nullptr,0);if(!size||size>65536)return 10;
            std::vector<uint8_t> bytes(size);SIZE_T got{};if(!ReadProcessMemory(process,reinterpret_cast<void*>(arena+address),bytes.data(),size,&got)||got!=size)return 11;
            if(argc>=5){FILE* out=std::fopen(argv[4],"wb");if(!out)return 12;std::fwrite(bytes.data(),1,bytes.size(),out);std::fclose(out);}else for(size_t i=0;i<size;i+=16){std::printf("\n%012llx:",address+i);for(size_t j=i;j<size&&j<i+16;++j)std::printf(" %02x",bytes[j]);}
            std::puts("");SymCleanup(process);CloseHandle(process);return 0;
        }
        if((argc>=3 && std::strcmp(argv[1],"live")==0) || (argc==2&&(std::strcmp(argv[1],"findparty")==0||std::strcmp(argv[1],"watchhp")==0))){
            guest_arena=arena;auto library=argc>=3?LoadLibraryA(argv[2]):nullptr;if(argc>=3&&!library)throw std::runtime_error("module load failed");
            using Get=const EdenDsmodModuleApi*(*)(uint32_t,uint64_t);
            auto get=library?reinterpret_cast<Get>(GetProcAddress(library,"eden_dsmod_get_module")):nullptr;
            auto api=get?get(EDEN_DSMOD_MODULE_ABI_VERSION,EDEN_DSMOD_MODULE_ABI_HASH):nullptr;
            if(argc>=3&&!api)throw std::runtime_error("module ABI mismatch");
            EdenDsmodHostApi local{};local.abi_version=EDEN_DSMOD_MODULE_ABI_VERSION;local.struct_size=sizeof(local);local.abi_hash=EDEN_DSMOD_MODULE_ABI_HASH;
            local.main_base=host.main_base;local.main_size=host.main_size;local.title_id=host.title_id;std::memcpy(local.build_id,host.build_id,sizeof(local.build_id));
            local.is_mapped=[](void*,uint64_t at,size_t size)->EdenDsmodBool{MEMORY_BASIC_INFORMATION info{};auto pointer=reinterpret_cast<void*>(guest_arena+at);return VirtualQueryEx(process,pointer,&info,sizeof(info))&&info.State==MEM_COMMIT&&!(info.Protect&(PAGE_NOACCESS|PAGE_GUARD))&&reinterpret_cast<uint64_t>(pointer)+size<=reinterpret_cast<uint64_t>(info.BaseAddress)+info.RegionSize;};
            local.read_memory=[](void*,uint64_t at,void* out,size_t size)->EdenDsmodBool{SIZE_T got{};return ReadProcessMemory(process,reinterpret_cast<void*>(guest_arena+at),out,size,&got)&&got==size;};
            local.publish_i64=[](void*,const char* key,int64_t value){if(std::strncmp(key,"za.p",4)==0||std::strcmp(key,"za.live.ready")==0||std::strcmp(key,"za.game.selected")==0||std::strncmp(key,"za.map.",7)==0)std::printf("%s=%lld\n",key,value);};
            local.publish_f64=[](void*,const char* key,double value){if(std::strcmp(key,"za.x")==0||std::strcmp(key,"za.z")==0)std::printf("%s=%f\n",key,value);};
            if(std::strcmp(argv[1],"watchhp")==0){
                za::LiveReader reader;int invalid=0,missing=0,changed=0,last=-1;
                for(int i=0;i<600;++i){auto snap=reader.Poll(local);if(!snap.party_valid)++invalid;else{if(!snap.hp_valid[1])++missing;if(snap.party[1].hp!=last){last=snap.party[1].hp;++changed;std::printf("poll %d HP=%d valid=%d\n",i,last,int(snap.hp_valid[1]));}}Sleep(20);}
                std::printf("600 polls: invalid_party=%d unconfirmed_HP=%d changes=%d\n",invalid,missing,changed);return 0;
            }
            if(std::strcmp(argv[1],"findparty")==0){
                za::LiveReader reader;auto live=reader.Poll(local);if(!live.party_valid)throw std::runtime_error("party snapshot unavailable");
                size_t scanned=0,matches=0;uint64_t cursor=arena+0x1000000000ull,stop=arena+0x1800000000ull;std::vector<uint8_t> buffer((1<<20)+za::kPartySize);
                while(cursor<stop){MEMORY_BASIC_INFORMATION info{};if(!VirtualQueryEx(process,reinterpret_cast<void*>(cursor),&info,sizeof(info)))break;auto end=std::min(stop,reinterpret_cast<uint64_t>(info.BaseAddress)+info.RegionSize);if(end<=cursor)break;
                    if(info.State==MEM_COMMIT && !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD))){
                        for(auto at=cursor;at<end;at+=1<<20){size_t count=std::min<uint64_t>(buffer.size(),end-at);SIZE_T got{};if(!ReadProcessMemory(process,reinterpret_cast<void*>(at),buffer.data(),count,&got)||got<za::kPartySize)continue;scanned+=got;
                            for(size_t i=0;i+za::kPartySize<=got&&i<(1<<20);i+=4){uint32_t ec{};std::memcpy(&ec,buffer.data()+i,4);
                                for(int slot=0;slot<6;++slot)if(ec&&ec==live.party[slot].encryption_constant){za::Entity entity;
                                    bool decoded=za::DecryptEntity(buffer.data()+i,za::kPartySize,entity)&&entity.checksum_ok&&entity.pid==live.party[slot].pid;
                                    const char* kind="encrypted";if(!decoded){decoded=za::ReadPlainEntity(buffer.data()+i,za::kPartySize,entity)&&entity.checksum_ok&&entity.pid==live.party[slot].pid;kind="plain";}
                                    if(decoded&&entity.species==live.party[slot].species){std::printf("candidate %llx slot=%d species=%u hp=%u/%u lvl=%u %s\n",at+i-arena,slot,entity.species,entity.hp,entity.hp_max,entity.level,kind);++matches;}
                                }
                            }
                        }
                    }cursor=end;
                }
                std::printf("scanned=%zu MiB matches=%zu\n",scanned>>20,matches);SymCleanup(process);CloseHandle(process);return 0;
            }
            auto instance=api->create(&local,"");if(!instance)throw std::runtime_error("module creation failed");for(int warm=0;warm<40;++warm)api->tick(instance,&local);api->sample(instance,&local);api->destroy(instance);FreeLibrary(library);
            SymCleanup(process);CloseHandle(process);return 0;
        }
        if(argc>=3) {
            const auto guest=std::strtoull(argv[1],nullptr,16), size=std::strtoull(argv[2],nullptr,0);
            if(!size || size>64*1024*1024)return 10;
            std::vector<uint8_t> bytes(static_cast<size_t>(size));SIZE_T got{};
            if(!ReadProcessMemory(process,reinterpret_cast<void*>(arena+guest),bytes.data(),bytes.size(),&got) || got!=bytes.size()) {
                std::printf("guest read failed %lu (%zu bytes)\n",GetLastError(),got);return 11;
            }
            if(argc>=4){FILE* file=std::fopen(argv[3],"wb");if(!file)return 12;std::fwrite(bytes.data(),1,bytes.size(),file);std::fclose(file);}
            else for(size_t i=0;i<bytes.size();i+=16){std::printf("%012llx:",guest+i);for(size_t j=i;j<bytes.size()&&j<i+16;++j)std::printf(" %02x",bytes[j]);std::puts("");}
        }
        SymCleanup(process);CloseHandle(process);return 0;
    } catch(const std::exception& error){std::printf("probe error: %s\n",error.what());return 1;}
}
