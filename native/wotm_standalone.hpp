// wotm.exe aberto direto (sem argumento: dois cliques, o zip do release). O run_live.ps1 passa o ELF, o cwd, a ISO e redireciona o
// stderr; aqui o exe acha tudo sozinho (docs/passos/RUNTIME.md, "wotm.exe sem argumento"):
// - pasta do jogo: a do exe (DLLs, mods/ do zip);
// - pasta de dados: a do jogo, se der para gravar nela; senao %APPDATA%\WotM (zip em Program Files). Nela ficam o SCUS_971.97, o
//   wotm.ini (iso=<caminho da ISO>), o wotm.log, e, por seguirem a pasta do ELF, mc0/, save.json, settings.ini e screenshots/;
// - mods: <dados>/mods, senao <jogo>/mods (WOTM_MODS);
// - log: stderr em <dados>/wotm.log e stdout em wotm.out.log (os anteriores viram *.prev.log), so quando o stderr nao e arquivo
//   nem pipe.
// Sem ELF ou sem ISO: uma caixa de mensagem e o exe sai (a primeira execucao que pede a ISO e o item 14 do TODO.md).
// Sem windows.h (conflita com o raylib): so as declaracoes de que precisa.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <io.h>
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(void *, wchar_t *, unsigned long);
extern "C" __declspec(dllimport) void *__stdcall GetStdHandle(unsigned long);
extern "C" __declspec(dllimport) unsigned long __stdcall GetFileType(void *);
extern "C" __declspec(dllimport) int __stdcall MessageBoxW(void *, const wchar_t *, const wchar_t *, unsigned int);
#endif

namespace wotm::standalone {

namespace fs = std::filesystem;

inline void setEnv(const char *name, const std::string &value)
{
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

inline std::string utf8(const fs::path &p)
{
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

inline void fail(const wchar_t *msg)
{
    std::fprintf(stderr, "[wotm] %ls\n", msg);
    std::fflush(stderr);
#ifdef _WIN32
    MessageBoxW(nullptr, msg, L"War of the Monsters Recompiled", 0x10u /*MB_ICONERROR*/);
#endif
}

inline fs::path exeDir()
{
#ifdef _WIN32
    wchar_t buf[4096];
    const unsigned long n = GetModuleFileNameW(nullptr, buf, 4096);
    if (n > 0 && n < 4096)
        return fs::path(buf).parent_path();
#endif
    std::error_code ec;
    return fs::current_path(ec);
}

inline bool writable(const fs::path &dir)
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

// stderr e stdout para <dados>/wotm.log, se ninguem os redirecionou (exe de janela aberto direto: sem console, sem handles)
inline void redirectLog(const fs::path &data)
{
#ifdef _WIN32
    const unsigned long type = GetFileType(GetStdHandle(static_cast<unsigned long>(-12) /*STD_ERROR_HANDLE*/));
    if (type == 1 /*FILE_TYPE_DISK*/ || type == 3 /*FILE_TYPE_PIPE*/)
        return;
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

// iso=<caminho> do wotm.ini (UTF-8, uma linha CHAVE=valor; # comenta)
inline fs::path iniIso(const fs::path &ini)
{
    std::ifstream f(ini, std::ios::binary);
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF)
            line.erase(0, 3);   // BOM do Bloco de Notas
        if (line.rfind("iso=", 0) == 0 && line.size() > 4)
            return fs::u8path(line.substr(4));
    }
    return {};
}

// Devolve o ELF a carregar, ou vazio (mensagem ja mostrada: o exe sai).
inline fs::path prepare()
{
    std::error_code ec;
    const fs::path game = exeDir();
    fs::path data = game;
    if (!writable(game))
    {
        const char *appdata = std::getenv("APPDATA");
        if (!appdata || !*appdata)
        {
            fail(L"The game folder is read-only and %APPDATA% is not set: move the game to a folder you can write to.");
            return {};
        }
        data = fs::path(appdata) / "WotM";
        fs::create_directories(data, ec);
        if (!writable(data))
        {
            fail(L"Neither the game folder nor %APPDATA%\\WotM can be written to: move the game to a folder you can write to.");
            return {};
        }
    }
    redirectLog(data);
    fs::current_path(data, ec);

    const fs::path elf = data / "SCUS_971.97";
    fs::path iso;
    if (const char *e = std::getenv("PS2X_CD_IMAGE"); e && *e)
        iso = e;
    else
        iso = iniIso(data / "wotm.ini");

    std::fprintf(stderr, "[wotm] standalone: game=%s data=%s iso=%s\n", utf8(game).c_str(), utf8(data).c_str(),
                 iso.empty() ? "(none)" : utf8(iso).c_str());
    if (!fs::exists(elf, ec))
    {
        fail((L"SCUS_971.97 not found in " + data.wstring() +
              L"\n\nThis port needs your own copy of War of the Monsters (NTSC-U, SCUS-97197).").c_str());
        return {};
    }
    if (iso.empty() || !fs::exists(iso, ec))
    {
        fail((L"Game disc image not found" + (iso.empty() ? std::wstring() : L": " + iso.wstring()) + L"\n\nSet iso=<path to your .iso> in " +
              (data / "wotm.ini").wstring()).c_str());
        return {};
    }
    setEnv("PS2X_CD_IMAGE", iso.string());

    if (!std::getenv("WOTM_MODS") && !fs::is_directory(data / "mods", ec) && fs::is_directory(game / "mods", ec))
        setEnv("WOTM_MODS", (game / "mods").string());
    std::fflush(stderr);
    return elf;
}

} // namespace wotm::standalone
