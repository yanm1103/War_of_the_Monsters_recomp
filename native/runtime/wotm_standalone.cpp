// wotm.exe aberto direto (sem argumento: dois cliques, o zip do release). O run_live.ps1 passa o ELF, o cwd, a ISO e redireciona o
// stderr; aqui o exe acha tudo sozinho (docs/passos/RUNTIME.md, "wotm.exe sem argumento"):
// - pasta do jogo: a do exe (DLLs, mods/ do zip);
// - pasta de dados: a do jogo, se der para gravar nela; senao %LOCALAPPDATA%\WotM (zip em Program Files). Nela ficam o SCUS_971.97,
//   o wotm.ini (iso=<caminho da ISO>), os logs, e, por seguirem a pasta do ELF, mc0/, save.json, settings.ini e screenshots/;
// - primeira execucao (sem o ELF, ou a ISO sumiu do caminho salvo): pede a ISO, tira o SCUS_971.97 dela, confere o SHA1 do NTSC-U e
//   grava o ELF e o wotm.ini;
// - mods: <dados>/mods, senao <jogo>/mods (WOTM_MODS);
// - log: stderr em <dados>/wotm.log e stdout em wotm.out.log (os anteriores viram *.prev.log), so quando o stderr nao e arquivo
//   nem pipe.
// Tambem a pilha da queda com nomes (wotmCrashTrace, chamado pelo crashFilter do wotm_live.inc), lidos do wotm.pdb ao lado do exe.
// Arquivo proprio (fora do unity build): o windows.h conflita com o raylib dos outros.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <dbghelp.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "dbghelp.lib")
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>
#include "wotm_version.hpp"

namespace {

namespace fs = std::filesystem;

constexpr const char *kElfName = "SCUS_971.97";
constexpr const char *kElfSha1 = "1ed7f8457436a9170d8f85386e267369c7fcdd4f";   // SCUS_971.97 do disco NTSC-U (SCUS-97197)
constexpr const wchar_t *kTitle = L"War of the Monsters Recompiled";

std::string utf8(const fs::path &p)
{
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

void setEnv(const char *name, const std::string &value)
{
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

// Caminho para quem abre com fopen de char (o runtime, PS2X_CD_IMAGE): na pagina de codigo do Windows; o que ela nao representa
// (ex.: japones num Windows em portugues) vai pelo nome curto 8.3
std::string narrow(const fs::path &p)
{
#ifdef _WIN32
    try
    {
        std::string s = p.string();
        if (fs::path(s) == p)
            return s;
    }
    catch (...)
    {
    }
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetShortPathNameW(p.c_str(), buf, DWORD(std::size(buf)));
    if (n > 0 && n < std::size(buf))
        return fs::path(buf).string();
    return {};
#else
    return p.string();
#endif
}

int box(const std::wstring &msg, unsigned flags)
{
    std::fprintf(stderr, "[wotm] %ls\n", msg.c_str());
    std::fflush(stderr);
#ifdef _WIN32
    return MessageBoxW(nullptr, msg.c_str(), kTitle, flags);
#else
    (void)flags;
    return 0;
#endif
}

fs::path exeDir()
{
#ifdef _WIN32
    wchar_t buf[4096];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
    if (n > 0 && n < 4096)
        return fs::path(buf).parent_path();
#endif
    std::error_code ec;
    return fs::current_path(ec);
}

bool writable(const fs::path &dir)
{
    const fs::path probe = dir / ".wotm_write_test";
    {
        std::ofstream f(probe, std::ios::binary);
        if (!f)
            return false;
        f << 'x';
        if (!f)
            return false;
    }
    std::error_code ec;
    fs::remove(probe, ec);
    return true;
}

void redirectLog(const fs::path &data)
{
#ifdef _WIN32
    const DWORD type = GetFileType(GetStdHandle(STD_ERROR_HANDLE));
    if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE)
        return;   // run_live/lote ja redirecionam
    // dois arquivos, como o live.err.log/live.out.log do run_live: no mesmo arquivo as threads misturam os dois no meio da linha
    auto open = [&](const char *name, const char *prev, FILE *stream) {
        const fs::path log = data / name;
        std::error_code ec;
        if (fs::exists(log, ec))
            fs::rename(log, data / prev, ec);
        if (_wfreopen(log.c_str(), L"w", stream))
            std::setvbuf(stream, nullptr, _IONBF, 0);
    };
    open("wotm.log", "wotm.prev.log", stderr);          // as [tags] do nativo e o [CRASH]: o que vai num relato de bug
    open("wotm.out.log", "wotm.out.prev.log", stdout);  // raylib e [hook]
#else
    (void)data;
#endif
}

// --- wotm.ini: uma linha CHAVE=valor, UTF-8; # comenta ---
std::vector<std::string> iniLines(const fs::path &ini)
{
    std::vector<std::string> out;
    std::ifstream f(ini, std::ios::binary);
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (out.empty() && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF)
            line.erase(0, 3);   // BOM do Bloco de Notas
        out.push_back(line);
    }
    return out;
}

fs::path iniIso(const fs::path &ini)
{
    for (const std::string &line : iniLines(ini))
        if (line.rfind("iso=", 0) == 0 && line.size() > 4)
            return fs::u8path(line.substr(4));
    return {};
}

bool iniSetIso(const fs::path &ini, const fs::path &iso)
{
    std::vector<std::string> lines = iniLines(ini);
    bool set = false;
    for (std::string &line : lines)
        if (line.rfind("iso=", 0) == 0)
        {
            line = "iso=" + utf8(iso);
            set = true;
        }
    if (!set)
        lines.push_back("iso=" + utf8(iso));
    std::ofstream f(ini, std::ios::binary | std::ios::trunc);
    for (const std::string &line : lines)
        f << line << "\r\n";
    return bool(f);
}

// --- SHA1 (para conferir o ELF; sem dependencia) ---
std::string sha1(const std::vector<uint8_t> &data)
{
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    std::vector<uint8_t> m(data);
    const uint64_t bits = uint64_t(data.size()) * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56)
        m.push_back(0);
    for (int i = 7; i >= 0; --i)
        m.push_back(uint8_t(bits >> (i * 8)));
    for (size_t off = 0; off < m.size(); off += 64)
    {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(m[off + 4 * i]) << 24 | uint32_t(m[off + 4 * i + 1]) << 16 | uint32_t(m[off + 4 * i + 2]) << 8 | m[off + 4 * i + 3];
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i)
        {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    char out[41];
    for (int i = 0; i < 5; ++i)
        std::snprintf(out + 8 * i, 9, "%08x", h[i]);
    return out;
}

// Um arquivo da raiz de uma ISO9660 (sem diferenciar maiusculas; a versao ";1" e ignorada). false: nao e ISO9660 ou nao achou.
bool isoReadRoot(const fs::path &iso, const char *name, std::vector<uint8_t> &out)
{
#ifdef _WIN32
    FILE *f = _wfopen(iso.c_str(), L"rb");
#else
    FILE *f = std::fopen(iso.c_str(), "rb");
#endif
    if (!f)
        return false;
    auto readAt = [&](uint64_t off, uint8_t *dst, size_t n) { return _fseeki64(f, int64_t(off), SEEK_SET) == 0 && std::fread(dst, 1, n, f) == n; };
    auto le32 = [](const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; };
    bool ok = false;
    uint8_t pvd[2048];
    if (readAt(16 * 2048ull, pvd, sizeof pvd) && pvd[0] == 1 && std::memcmp(pvd + 1, "CD001", 5) == 0)
    {
        const uint32_t ext = le32(pvd + 156 + 2), size = le32(pvd + 156 + 10);
        std::vector<uint8_t> dir(size <= (1u << 20) ? size : 0);
        if (!dir.empty() && readAt(uint64_t(ext) * 2048, dir.data(), dir.size()))
            for (size_t o = 0; o < dir.size();)
            {
                const uint8_t len = dir[o];
                if (len == 0) { o = (o / 2048 + 1) * 2048; continue; }   // registros nao cruzam setores
                if (o + len > dir.size() || len < 34)
                    break;
                std::string id(reinterpret_cast<const char *>(&dir[o + 33]), dir[o + 32]);
                if (const auto sc = id.find(';'); sc != std::string::npos)
                    id.resize(sc);
                if (_stricmp(id.c_str(), name) == 0 && !(dir[o + 25] & 2))
                {
                    const uint32_t fext = le32(&dir[o + 2]), fsize = le32(&dir[o + 10]);
                    if (fsize <= (64u << 20))
                    {
                        out.resize(fsize);
                        ok = readAt(uint64_t(fext) * 2048, out.data(), out.size());
                    }
                    break;
                }
                o += len;
            }
    }
    std::fclose(f);
    return ok;
}

fs::path pickIso(const fs::path &startDir)
{
#ifdef _WIN32
    wchar_t file[4096] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = L"PS2 disc image (*.iso)\0*.iso\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = DWORD(std::size(file));
    const std::wstring init = startDir.wstring();
    ofn.lpstrInitialDir = startDir.empty() ? nullptr : init.c_str();
    ofn.lpstrTitle = L"Select your War of the Monsters disc image (NTSC-U, SCUS-97197)";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn))
        return fs::path(file);
#else
    (void)startDir;
#endif
    return {};
}

// Primeira execucao, ou a ISO sumiu: pede a ISO ate uma valer (grava o ELF e o wotm.ini) ou o jogador cancelar.
bool setup(const fs::path &data, const fs::path &elf, fs::path &iso, bool elfOk)
{
    std::error_code ec;
    std::wstring why = !elfOk ? L"First run: this port needs your own copy of War of the Monsters for PlayStation 2 (NTSC-U, SCUS-97197) "
                                L"as a disc image (.iso).\n\nThe game files are read from the image; nothing is downloaded."
                              : L"The disc image is no longer at:\n" + iso.wstring() + L"\n\nSelect it again.";
    while (true)
    {
        if (box(why + L"\n\nPress OK to select the .iso file.", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
            return false;
        const fs::path pick = pickIso(iso.empty() ? fs::path() : iso.parent_path());
        if (pick.empty())
            return false;
        std::vector<uint8_t> bytes;
        if (!isoReadRoot(pick, kElfName, bytes))
        {
            why = L"This file is not a War of the Monsters (NTSC-U) disc image:\n" + pick.wstring() +
                  L"\n\nNo SCUS_971.97 was found in it. Only the North American release (SCUS-97197) is supported.";
            continue;
        }
        const std::string got = sha1(bytes);
        if (got != kElfSha1)
        {
            why = L"The disc image has a SCUS_971.97 that does not match the NTSC-U release (SCUS-97197):\n" + pick.wstring() +
                  L"\n\nIt may be a different version or a modified image. Only the original North American release is supported.";
            std::fprintf(stderr, "[wotm] SHA1 do %s na ISO: %s (esperado %s)\n", kElfName, got.c_str(), kElfSha1);
            continue;
        }
        {
            const fs::path tmp = data / "SCUS_971.97.tmp";
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
            f.close();
            if (!f || (fs::rename(tmp, elf, ec), ec))
            {
                box(L"Could not write " + elf.wstring(), MB_OK | MB_ICONERROR);
                return false;
            }
        }
        if (!iniSetIso(data / "wotm.ini", pick))
        {
            box(L"Could not write " + (data / "wotm.ini").wstring(), MB_OK | MB_ICONERROR);
            return false;
        }
        iso = pick;
        std::fprintf(stderr, "[wotm] ISO conferida (SHA1 do %s ok): %s; ELF e wotm.ini gravados\n", kElfName, utf8(pick).c_str());
        return true;
    }
}

} // namespace

// Pilha da queda, com o nome de cada funcao pelo PDB (o do build: wotm.pdb ao lado do exe; so simbolos publicos, entao uma funcao
// estatica sai com o nome da publica anterior e um deslocamento grande). Chamado uma vez pelo crashFilter (wotm_live.inc), na thread
// que caiu, com o CONTEXT da excecao. Sem PDB, so modulo+deslocamento, que o .map do build resolve.
void wotmCrashTrace(void *exceptionContext)
{
#ifdef _WIN32
    if (!exceptionContext)
        return;
    CONTEXT ctx = *static_cast<CONTEXT *>(exceptionContext);
    HANDLE proc = GetCurrentProcess(), thread = GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    const std::wstring dir = exeDir().wstring();
    const bool syms = SymInitializeW(proc, dir.c_str(), TRUE) != FALSE;
    if (!syms)
        std::fprintf(stderr, "[CRASH] sem simbolos (dbghelp %lu)\n", GetLastError());
    STACKFRAME64 f{};
    f.AddrPC.Offset = ctx.Rip;
    f.AddrPC.Mode = AddrModeFlat;
    f.AddrFrame.Offset = ctx.Rbp;
    f.AddrFrame.Mode = AddrModeFlat;
    f.AddrStack.Offset = ctx.Rsp;
    f.AddrStack.Mode = AddrModeFlat;
    alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 512];
    for (int i = 0; i < 32; ++i)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &f, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        const DWORD64 pc = f.AddrPC.Offset;
        if (!pc)
            break;
        HMODULE mod = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(pc), &mod);
        char modPath[MAX_PATH] = "?";
        if (mod)
            GetModuleFileNameA(mod, modPath, MAX_PATH);
        const char *modName = std::strrchr(modPath, '\\') ? std::strrchr(modPath, '\\') + 1 : modPath;
        const unsigned long long rel = mod ? pc - reinterpret_cast<DWORD64>(mod) : pc;
        auto *sym = reinterpret_cast<SYMBOL_INFO *>(symBuf);
        std::memset(symBuf, 0, sizeof symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        if (syms && SymFromAddr(proc, pc, &disp, sym))
            std::fprintf(stderr, "[CRASH]   #%-2d %s+0x%llx  %s+0x%llx\n", i, modName, rel, sym->Name, static_cast<unsigned long long>(disp));
        else
            std::fprintf(stderr, "[CRASH]   #%-2d %s+0x%llx\n", i, modName, rel);
    }
    std::fflush(stderr);
    if (syms)
        SymCleanup(proc);
#else
    (void)exceptionContext;
#endif
}

// Devolve o ELF a carregar, ou vazio (o jogador cancelou ou deu erro, ja avisado: o exe sai). Chamado pelo main.cpp sem argumento.
std::filesystem::path wotmStandalonePrepare()
{
    std::error_code ec;
    const fs::path game = exeDir();
    fs::path data = game;
    if (!writable(game))
    {
        const char *local = std::getenv("LOCALAPPDATA");
        if (!local || !*local)
        {
            box(L"The game folder is read-only and %LOCALAPPDATA% is not set: move the game to a folder you can write to.", MB_OK | MB_ICONERROR);
            return {};
        }
        data = fs::path(local) / "WotM";
        fs::create_directories(data, ec);
        if (!writable(data))
        {
            box(L"Neither the game folder nor %LOCALAPPDATA%\\WotM can be written to: move the game to a folder you can write to.",
                MB_OK | MB_ICONERROR);
            return {};
        }
    }
    redirectLog(data);
    std::fprintf(stderr, "[wotm] War of the Monsters Recompiled %s\n", WOTM_VERSION);   // primeira linha do log: casa o relato com o PDB
    fs::current_path(data, ec);

    const fs::path elf = data / kElfName;
    fs::path iso;
    const char *envIso = std::getenv("PS2X_CD_IMAGE");
    if (envIso && *envIso)
        iso = envIso;
    else
        iso = iniIso(data / "wotm.ini");
    std::fprintf(stderr, "[wotm] standalone: game=%s data=%s iso=%s\n", utf8(game).c_str(), utf8(data).c_str(),
                 iso.empty() ? "(none)" : utf8(iso).c_str());

    const bool elfOk = fs::exists(elf, ec);
    const bool isoOk = !iso.empty() && fs::exists(iso, ec);
    if ((!elfOk || !isoOk) && !(envIso && *envIso))
    {
        if (!setup(data, elf, iso, elfOk && !iso.empty()))
        {
            std::fprintf(stderr, "[wotm] primeira execucao cancelada\n");
            return {};
        }
    }
    else if (!elfOk || !isoOk)
    {
        box(L"PS2X_CD_IMAGE or SCUS_971.97 not found.", MB_OK | MB_ICONERROR);
        return {};
    }
    const std::string isoNarrow = narrow(iso);
    if (isoNarrow.empty())
    {
        box(L"The disc image path cannot be opened: " + iso.wstring() + L"\n\nMove the .iso to a folder with a simpler name.", MB_OK | MB_ICONERROR);
        return {};
    }
    setEnv("PS2X_CD_IMAGE", isoNarrow);

    if (!std::getenv("WOTM_MODS") && !fs::is_directory(data / "mods", ec) && fs::is_directory(game / "mods", ec))
        setEnv("WOTM_MODS", narrow(game / "mods"));
    std::fflush(stderr);
    return elf;
}
