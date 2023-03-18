#include <iostream>
#include <Windows.h>
#include <format>

static uintptr_t mono = 0;
const auto callbacks_offset = 0x49C7E0;

using compile_method_t = void* (__fastcall*)(void*, void*);
static compile_method_t original_compile_method = nullptr;

struct MonoMethodDesc
{
    char* name_space;
    char* klass;
    char* name;
    char* args;
    unsigned int num_args;
    int include_namespace;
    int klass_glob;
    int name_glob;
};
using mono_method_desc_from_method_t = MonoMethodDesc* (__fastcall*)(void*);
static mono_method_desc_from_method_t mono_method_desc_from_method = nullptr;
using mono_jit_info_table_find_t = void* (__fastcall*)(void* domain, void* addr);
static mono_jit_info_table_find_t mono_jit_info_table_find = nullptr;

using set_localization_key_t = void*(__fastcall*)(void*);
set_localization_key_t original_set_localization_key = nullptr;
void* __fastcall set_localization_key_hk(void* self)
{
    // just replace the string in the managed class pls
    
    auto ret = original_set_localization_key(self);

    auto repl = L"hello!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!";

    auto new_str = malloc(0x10 + sizeof(int32_t) + wcslen(repl) * 2 + 2);
    memcpy(new_str, ret, 0x10);
    *(int32_t*)(uintptr_t(new_str) + 0x10) = wcslen(repl);
    memcpy((void*)(uintptr_t(new_str) + 0x14), repl, wcslen(repl) * 2 + 2);

    return new_str;
}

void replace_function(void* jitted, void* hook)
{
    auto root_domain = (*(void**)(uintptr_t(GetModuleHandle(L"mono-2.0-bdwgc.dll")) + 0x499c78));

    auto info = mono_jit_info_table_find(root_domain, jitted);
    if (info == nullptr)
        return;

    // 00000010 code_start      dq ?
    *(void**)(uintptr_t(info) + 0x10) = hook;
}

std::string get_full_name(MonoMethodDesc* desc)
{
    if (desc->name_space == nullptr || strlen(desc->name_space) == 0)
        return std::format("{}:{}", desc->klass, desc->name);

    return std::format("{}.{}:{}", desc->name_space, desc->klass, desc->name);
}

void* __fastcall compile_method_hk(void* method, void* error)
{
    if (mono_method_desc_from_method == nullptr)
        mono_method_desc_from_method = (mono_method_desc_from_method_t)GetProcAddress((HMODULE)mono, "mono_method_desc_from_method");
    if (mono_jit_info_table_find == nullptr)
        mono_jit_info_table_find = (mono_jit_info_table_find_t)GetProcAddress((HMODULE)mono, "mono_jit_info_table_find");

    auto ret = original_compile_method(method, error);

    auto name = get_full_name(mono_method_desc_from_method(method));
    if (name == "EFT.InventoryLogic.ItemTemplate:get_ShortNameLocalizationKey" &&
            ret != &set_localization_key_hk) // for some reason compile_method can be called multiple times on the same function
    {
        original_set_localization_key = (set_localization_key_t)ret;

        // replace function pointer in jit table
        // info is added to table somewhere in original compile_method
        replace_function(ret, &set_localization_key_hk);

        // replace callback with original, we are done
        *(compile_method_t*)(mono + callbacks_offset + 0x58) = original_compile_method;
        
        return &set_localization_key_hk;
    }
    
    return ret;
}

void start()
{
    while (mono == 0)
        mono = uintptr_t(GetModuleHandleA("mono-2.0-bdwgc.dll"));

    /*
    .data:000000018049C7E0 ; MonoRuntimeCallbacks callbacks
    .data:000000018049C7E0 callbacks       MonoRuntimeCallbacks <?>

    00000058 compile_method  dq ?
    */
    while (original_compile_method == nullptr)
        original_compile_method = *(compile_method_t*)(mono + callbacks_offset + 0x58);
    *(compile_method_t*)(mono + callbacks_offset + 0x58) = &compile_method_hk;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(nullptr, 0, LPTHREAD_START_ROUTINE(start), nullptr, 0, nullptr);
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

