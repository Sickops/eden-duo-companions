#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
#pragma comment(lib,"dbghelp.lib")
int wmain(int argc,wchar_t** argv){
 if(argc<2)return 1;
 std::ifstream file(argv[1],std::ios::binary);std::vector<char> bytes(std::istreambuf_iterator<char>(file),{});if(bytes.empty())return 2;
 auto stream=[&](MINIDUMP_STREAM_TYPE kind){void* data{};ULONG size{};MINIDUMP_DIRECTORY* dir{};return MiniDumpReadDumpStream(bytes.data(),kind,&dir,&data,&size)?data:nullptr;};
 auto* modules=static_cast<MINIDUMP_MODULE_LIST*>(stream(ModuleListStream));auto* exception=static_cast<MINIDUMP_EXCEPTION_STREAM*>(stream(ExceptionStream));auto* threads=static_cast<MINIDUMP_THREAD_LIST*>(stream(ThreadListStream));if(!modules||!exception||!threads)return 3;
 HANDLE self=GetCurrentProcess();SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_LOAD_LINES);SymInitialize(self,nullptr,FALSE);
 for(ULONG i=0;i<modules->NumberOfModules;++i){auto& m=modules->Modules[i];auto* name=reinterpret_cast<MINIDUMP_STRING*>(bytes.data()+m.ModuleNameRva);SymLoadModuleExW(self,nullptr,name->Buffer,nullptr,m.BaseOfImage,m.SizeOfImage,nullptr,0);}
 auto symbol=[&](DWORD64 address){for(ULONG i=0;i<modules->NumberOfModules;++i){auto& m=modules->Modules[i];if(address<m.BaseOfImage||address>=m.BaseOfImage+m.SizeOfImage)continue;char storage[sizeof(SYMBOL_INFO)+2048]{};auto* info=reinterpret_cast<SYMBOL_INFO*>(storage);info->SizeOfStruct=sizeof(SYMBOL_INFO);info->MaxNameLen=2048;DWORD64 delta{};auto* name=reinterpret_cast<MINIDUMP_STRING*>(bytes.data()+m.ModuleNameRva);auto* base=wcsrchr(name->Buffer,L'\\');std::printf("%llx %ls+%llx",address,base?base+1:name->Buffer,address-m.BaseOfImage);if(SymFromAddr(self,address,&delta,info))std::printf(" %s+%llx",info->Name,delta);std::puts("");return;}};
 auto* ctx=reinterpret_cast<CONTEXT*>(bytes.data()+exception->ThreadContext.Rva);
 std::printf("exception %lx thread %lu RIP=%llx RSP=%llx RCX=%llx RDX=%llx R8=%llx\n",exception->ExceptionRecord.ExceptionCode,exception->ThreadId,ctx->Rip,ctx->Rsp,ctx->Rcx,ctx->Rdx,ctx->R8);symbol(ctx->Rip);
 for(ULONG i=0;i<threads->NumberOfThreads;++i){auto& t=threads->Threads[i];if(t.ThreadId!=exception->ThreadId)continue;auto start=ctx->Rsp>=t.Stack.StartOfMemoryRange?ctx->Rsp-t.Stack.StartOfMemoryRange:0;for(auto offset=start;offset+8<=t.Stack.Memory.DataSize&&offset<start+2048;offset+=8){DWORD64 address{};memcpy(&address,bytes.data()+t.Stack.Memory.Rva+offset,8);symbol(address);}}
 SymCleanup(self);
}
