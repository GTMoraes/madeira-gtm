// GTMRagdollFix 1.0 - separa da memoria comum o bloco de desvios do Ragdoll Bullet Physics.
//
// O RagDoll_physics.bin (inicializacao em +0x2F30, chamada pelo RagDoll_physics.asi) faz
// buf = new char[1000]; memset(buf, 0x90, 1000); VirtualProtect(buf, 1000, PAGE_EXECUTE_READWRITE)
// e guarda ali os desvios dos ganchos. O bloco fica no meio do heap, colado em dados que o Ragdoll
// altera o tempo todo. No iPhone (paginas de 16 KB) a pagina inteira vira codigo vigiado e cada
// gravacao nesses dados e emulada uma a uma: dezenas de milhares por segundo em batidas e atropelos.
//
// Este plugin carrega o RagDoll_physics.bin antes do RagDoll_physics.asi (fica na pasta do jogo e o
// nome vem antes em ordem alfabetica) e troca so essa chamada do new por um VirtualAlloc proprio,
// com paginas so dele. O Ragdoll nunca libera esse bloco, entao nao ha delete a ajustar.
// Se o codigo nao for o esperado, nao mexe em nada.
#include <windows.h>
#include <stdio.h>
#include <string.h>

static char g_dir[MAX_PATH];

static void Log(const char* fmt, ...) {
    char path[MAX_PATH]; snprintf(path, sizeof path, "%sGTMRagdollFix.log", g_dir);
    FILE* f = fopen(path, "a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fputc('\n', f); fclose(f);
}

static void* g_block = nullptr;
extern "C" void* __cdecl TrampAlloc(size_t n) {
    void* p = VirtualAlloc(nullptr, n < 0x10000 ? 0x10000 : n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!g_block) g_block = p;
    return p;
}

static const BYTE kExpect[] = {      // RagDoll_physics.bin + 0x5DC0 ... 0x5E19
    0x56,0x8b,0xf1,0x68,0xe8,0x03,0x00,0x00,0xc6,0x46,0x10,0x01,0xc7,0x46,0x0c,0x00,0x00,0x00,0x00,
    0xc7,0x46,0x04,0x00,0x00,0x00,0x00,0xc7,0x46,0x08,0x00,0x00,0x00,0x00,0xc7,0x46,0x18,0xe8,0x03,
    0x00,0x00,0xc7,0x46,0x1c,0x01,0x00,0x00,0x00,0xe8,0x02,0x33,0x07,0x00,0x68,0xe8,0x03,0x00,0x00,
    0x68,0x90,0x00,0x00,0x00,0x50,0x89,0x46,0x14,0xe8,0x29,0x64,0x07,0x00,0x83,0xc4,0x10,0x8d,0x46,
    0x20,0x50,0x6a,0x40,0x68,0xe8,0x03,0x00,0x00,0xff,0x76,0x14,0xff,0x15 };
static const DWORD kFunc = 0x5DC0, kCall = 0x5DEF;      // call operator new(0x3E8)

static void Apply() {
    char asi[MAX_PATH], bin[MAX_PATH];
    snprintf(asi, sizeof asi, "%sRagDoll_physics.asi", g_dir);
    snprintf(bin, sizeof bin, "%sRagDoll_physics.bin", g_dir);
    if (GetFileAttributesA(asi) == INVALID_FILE_ATTRIBUTES || GetFileAttributesA(bin) == INVALID_FILE_ATTRIBUTES) {
        Log("Ragdoll nao instalado; nada a fazer");
        return;
    }
    if (GetModuleHandleA("RagDoll_physics.bin")) { Log("o Ragdoll ja tinha carregado antes deste plugin; nada feito"); return; }
    HMODULE m = LoadLibraryA(bin);
    if (!m) { Log("nao consegui carregar %s (erro %lu)", bin, GetLastError()); return; }
    BYTE* base = (BYTE*)m;
    if (memcmp(base + kFunc, kExpect, sizeof kExpect) != 0) {
        Log("RagDoll_physics.bin diferente do esperado (base %p); nada alterado", base);
        return;
    }
    BYTE* at = base + kCall;
    DWORD old;
    if (!VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old)) { Log("VirtualProtect falhou (%lu)", GetLastError()); return; }
    *(int*)(at + 1) = (int)((DWORD)&TrampAlloc - ((DWORD)at + 5));
    VirtualProtect(at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 5);
    Log("GTMRagdollFix 1.0: bloco de desvios do Ragdoll vai para paginas proprias (RagDoll_physics.bin em %p)", base);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        GetModuleFileNameA(h, g_dir, MAX_PATH);
        char* s = strrchr(g_dir, '\\'); if (s) s[1] = 0; else g_dir[0] = 0;
        Apply();
    }
    return TRUE;
}
