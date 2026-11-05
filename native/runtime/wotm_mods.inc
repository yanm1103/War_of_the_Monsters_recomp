// wotm_mods.inc: pasta mods/ (MODS.md; registro em native/wotm_mods.hpp). Incluido por wotm_overrides.cpp.
// Dois ganchos atendem todo arquivo que o jogo le do disco:
// - fileCdSearchFile (src/common/file.cpp): fases, monstros, SHELL, bancos de som, musicas e filmes acham o arquivo por ele
//   (setor e tamanho). Com o arquivo em mods/ a original roda (para o log comparar o tamanho) e o sceCdlFILE sai com o LSN falso
//   e o tamanho do mod.
// - sceCdRead (stub do runtime): fileReadf/fileReada/fileReads e o snd_StreamSafeCdRead leem por ele; o LSN falso vem do mod.
// O som nativo (wotm_snd.inc, readLsnRaw) le bancos e musicas pelo LSN e consulta o registro antes do disco.
// Arquivo de mod maior que o do disco: os monstros (MON/*) e as texturas da fase carregam em sequencia acima de 32 MB (RAM alta), o
// NGP da fase e o menu (SHELL/*) em sequencia de 0xA00000 ate 0x1F10000; um arquivo maior so empurra o que vem depois, e o log confere
// o fim de cada arquivo de mod ja descompactado contra o teto (checkLoad). RST/*, os .AIC e PTS/* tem tamanho fixo (fixedCap): o jogo
// recebe so o que cabe. Os .PTR dos monstros: ptrRelocate. Bancos de som, musicas e filmes nao tem teto (som nativo, leitura em partes).
// WOTM_MODS=<pasta> troca a pasta (padrao: mods/ ao lado do ELF, senao a do repo); =0 desliga.
#include "wotm_mods.hpp"
#include "wotm_army_files.hpp"   // MON/ARMY.*: os veiculos de VEGAS para o exercito em qualquer fase
#include <set>

namespace wotm_mods_hooks
{
using wotm::mods::Registry;

PS2Runtime::RecompiledFunction g_origSearch = nullptr, g_origCdRead = nullptr;

inline uint32_t rd32(const uint8_t *rdram, uint32_t a) { uint32_t v; std::memcpy(&v, rdram + (a & PS2_RAM_MASK), 4); return v; }
inline void wr32(uint8_t *rdram, uint32_t a, uint32_t v) { std::memcpy(rdram + (a & PS2_RAM_MASK), &v, 4); }

constexpr uint32_t kLowCeiling = 0x1F10000u;   // fim da regiao do NGP da fase e do menu (checkLoad)
constexpr uint32_t kPtsMagic = 0x12282000u, kPtsCap = 0x4000u;

// Arquivos de formato de tamanho fixo, lidos num buffer do tamanho exato: os do resetcom (RST/*.CHR, .MRS, .GBL, .MDR e os .AIC das
// pastas das fases) em resetcom::m_data[0xC000], e os pontos (PTS/*) num buffer de 0x4000 do topo da MemoryStack, copiados para
// PointToolKit::points[256] (64 bytes cada, depois de 8 de cabecalho). Maior que isso estoura o objeto: o jogo recebe so o que cabe.
inline uint32_t fixedCap(const std::string &rel)
{
    if (rel.rfind("PTS/", 0) == 0) return kPtsCap;
    if (rel.rfind("RST/", 0) == 0) return 0xC000u;
    if (rel.size() > 4 && rel.substr(rel.size() - 4) == ".AIC") return 0xC000u;
    return 0;
}

// int fileCdSearchFile(sceCdlFILE *f, char *path); sceCdlFILE = {u32 lsn, u32 size, char name[16], u8 date[8]}
void searchFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    const uint32_t fAddr = getRegU32(ctx, 4), pAddr = getRegU32(ctx, 5);
    const char *p = reinterpret_cast<const char *>(rdram + (pAddr & PS2_RAM_MASK));
    const std::string rel = Registry::keyFromGame(std::string(p, strnlen(p, 256)));
    const std::string target = Registry::get().aliasOf(rel);
    const wotm::mods::File *m = Registry::get().find(target);
    if (target != rel)
    {
        // Traje de mod sem esse arquivo na pasta da skin: busca o do traje base ("LIZARD4" -> "LIZARD1"), trocando o nome no
        // proprio texto do jogo durante a chamada (o nome do base nunca e mais longo: o numero do traje base tem um digito)
        char *s = reinterpret_cast<char *>(rdram + (pAddr & PS2_RAM_MASK));
        const size_t len = strnlen(s, 256);
        const std::string saved(s, len + 1);
        const std::string from = rel.substr(rel.find_last_of('/') + 1), to = target.substr(target.find_last_of('/') + 1);
        std::string up(saved);
        for (char &c : up) c = char(std::toupper(static_cast<unsigned char>(c)));
        if (const size_t at = up.find(from); at != std::string::npos && to.size() <= from.size())
        {
            std::string repl = saved.substr(0, at) + to + saved.substr(at + from.size());
            std::memcpy(s, repl.c_str(), repl.size() + 1);
        }
        g_origSearch(rdram, ctx, rt);
        std::memcpy(s, saved.data(), saved.size());
        static std::set<std::string> logged;
        if (logged.insert(rel).second)
            std::fprintf(stderr, "[mods] %s = %s (skin: arquivo do traje base)%s\n", rel.c_str(), target.c_str(), getRegU32(ctx, 2) ? "" : " NAO ACHADO");
    }
    else
        g_origSearch(rdram, ctx, rt);
    if (!m)
        return;
    const bool onDisc = getRegU32(ctx, 2) != 0;
    const uint32_t discSize = onDisc ? rd32(rdram, fAddr + 4) : 0;
    const uint32_t cap = fixedCap(m->rel);
    const uint64_t served = cap && m->size > cap ? cap : m->size;   // formato de tamanho fixo: o jogo le so o que cabe no buffer
    wr32(rdram, fAddr + 0, m->lsn);
    wr32(rdram, fAddr + 4, uint32_t(served));
    if (!onDisc)   // arquivo novo: o nome do sceCdlFILE vem do mod (a original nao achou nada para copiar)
    {
        std::string base = m->rel.substr(m->rel.find_last_of('/') + 1);
        base.resize(std::min<size_t>(base.size(), 15));
        std::memset(rdram + ((fAddr + 8) & PS2_RAM_MASK), 0, 16 + 8);
        std::memcpy(rdram + ((fAddr + 8) & PS2_RAM_MASK), base.data(), base.size());
    }
    setReturnS32(ctx, 1);
    if (!m->logged)
    {
        m->logged = true;
        if (!onDisc)
            std::fprintf(stderr, "[mods] %s <- %s (%llu bytes; arquivo novo, nao esta no disco)\n", m->rel.c_str(), m->mod.c_str(),
                         (unsigned long long)m->size);
        else
        {
            const std::string &r = m->rel;
            const char *tail = m->size <= discSize ? ""
                             : served < m->size ? ""   // sai na linha de baixo
                             : r.rfind("MON/", 0) == 0 || r.rfind("LVL/", 0) == 0 || r.rfind("SHELL/", 0) == 0
                                 ? " (maior que o do disco: a carga confere a folga)"
                             : r.rfind("SND/", 0) == 0 || r.rfind("VAG/", 0) == 0 || r.rfind("MOV/", 0) == 0
                                 ? " (maior que o do disco; som nativo ou lido em partes, sem teto)"
                                 : " AVISO: maior que o do disco, pode sobrescrever o que o jogo carrega depois";
            std::fprintf(stderr, "[mods] %s <- %s (%llu bytes; disco %u)%s\n", r.c_str(), m->mod.c_str(), (unsigned long long)m->size,
                         discSize, tail);
        }
        if (served < m->size)
            std::fprintf(stderr, "[mods] %s: AVISO: formato de tamanho fixo, o jogo le so os primeiros %u bytes (o mod tem %llu)\n",
                         m->rel.c_str(), cap, (unsigned long long)m->size);
    }
}

// Carga de arquivo de mod pelo zipInflateAll (fase e menu, compactados) e pelo fileReadf: confere o fim do arquivo ja descompactado
// contra o teto da regiao. Embaixo de 32 MB o NGP da fase e o menu (NGP, RTX, TEX e .PTR do SHELL) moram em sequencia a partir de
// 0xA00000 ate 0x1F10000, onde comecam dados vivos (o buffer do zip fica em 0x1F7F840). A copia do SHELL.RTX/LOAD.RTX que o
// BootInitUserint descompacta em 0x1B7FFF0 nao e lida por ninguem depois. Acima de 32 MB vale o fim da RAM menos 1 MB (Mecha,
// kQuadDefs). Os .PTR dos monstros saem no ptrRelocate; RST, AIC e PTS tem tamanho fixo (fixedCap).
void checkLoad(uint8_t *rdram, const char *what, const std::string &gameName, uint32_t dst, int32_t size)
{
    const wotm::mods::File *m = Registry::get().find(Registry::get().aliasOf(Registry::keyFromGame(gameName)));
    if (!m || size <= 0 || fixedCap(m->rel) || (m->rel.rfind("MON/", 0) == 0 && m->rel.size() > 4 && m->rel.substr(m->rel.size() - 4) == ".PTR"))
        return;
    const uint32_t d = dst & 0x1FFFFFFFu, end = d + uint32_t(size);
    uint32_t ceiling;
    if (d >= 0xA00000u && d < kLowCeiling)
        ceiling = kLowCeiling;
    else if (d >= PS2_GUEST_RAM_SIZE)
        ceiling = PS2_RAM_SIZE - 0x100000u;
    else
        return;
    static std::set<std::string> logged;   // o menu recarrega o SHELL a cada volta: uma linha por arquivo e endereco
    if (!logged.insert(m->rel + "@" + std::to_string(d) + "+" + std::to_string(size)).second)
        return;
    const double left = (int64_t(ceiling) - int64_t(end)) / 1048576.0;
    std::fprintf(stderr, "[mods] %s (%s): %s %d bytes em 0x%07X-0x%07X, %.1f MB ate o teto 0x%07X%s\n", m->rel.c_str(), m->mod.c_str(), what,
                 size, d, end, left, ceiling, end > ceiling ? " -- AVISO: alem do teto, espere corrupcao" : "");
}

PS2Runtime::RecompiledFunction g_origZip = nullptr, g_origReadf = nullptr;

// int zipInflateAll(char *name, void *dest)
void zipInflate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    if (ctx->pc != wotm::fn::zipInflateAll__FPcPv)
    {
        g_origZip(rdram, ctx, rt);
        return;
    }
    const char *p = reinterpret_cast<const char *>(rdram + (getRegU32(ctx, 4) & PS2_RAM_MASK));
    const std::string name(p, strnlen(p, 256));
    const uint32_t dst = getRegU32(ctx, 5);
    g_origZip(rdram, ctx, rt);
    checkLoad(rdram, "descompactado:", name, dst, int32_t(getRegU32(ctx, 2)));
}

// int fileReadf(char *name, void *dest)
void readf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    if (ctx->pc != wotm::fn::fileReadf__FPcPv)
    {
        g_origReadf(rdram, ctx, rt);
        return;
    }
    const char *p = reinterpret_cast<const char *>(rdram + (getRegU32(ctx, 4) & PS2_RAM_MASK));
    const std::string name(p, strnlen(p, 256));
    const uint32_t dst = getRegU32(ctx, 5);
    g_origReadf(rdram, ctx, rt);
    checkLoad(rdram, "lido:", name, dst, int32_t(getRegU32(ctx, 2)));
}

// int sceCdRead(u32 lsn, u32 sectors, void *buf, sceCdRMode *mode)
void cdRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    const uint32_t lsn = getRegU32(ctx, 4);
    if (!Registry::isModLsn(lsn))
    {
        g_origCdRead(rdram, ctx, rt);
        return;
    }
    const uint32_t sectors = getRegU32(ctx, 5), offset = getRegU32(ctx, 6) & PS2_RAM_MASK;
    const size_t bytes = size_t(std::min<uint64_t>(uint64_t(sectors) * 2048, uint64_t(PS2_RAM_SIZE) - offset));
    if (!Registry::get().read(lsn, sectors, rdram + offset, bytes))
    {
        static int warned = 0;
        if (warned++ < 16)
            std::fprintf(stderr, "[mods] sceCdRead: LSN %#x (%u setores) nao e de nenhum arquivo de mod; devolvendo zeros\n", lsn, sectors);
        std::memset(rdram + offset, 0, bytes);
    }
    else if (const wotm::mods::File *f = Registry::get().findLsn(lsn); f && lsn == f->lsn && bytes >= 8 && f->rel.rfind("PTS/", 0) == 0)
    {   // pontos alem dos 255 que cabem nos 0x4000 lidos (e no points[256] do PointToolKit): o jogo copiaria lixo para fora do objeto
        const uint32_t maxN = (uint32_t(std::min<uint64_t>(f->size, kPtsCap)) - 8) / 64;
        if (rd32(rdram, offset) == kPtsMagic && rd32(rdram, offset + 4) > maxN)
        {
            std::fprintf(stderr, "[mods] %s: AVISO: %u pontos, o jogo tem lugar para %u; usando os primeiros\n", f->rel.c_str(),
                         rd32(rdram, offset + 4), maxN);
            wr32(rdram, offset + 4, maxN);
        }
    }
    setReturnS32(ctx, 1);   // o stub tambem le na hora: sceCdSync devolve "pronto"
}

// .PTR de mod (skin ou custom com modelo proprio): o dbsRelocateViaPtrListFile le o "mon/<nome>.ptr" inteiro na MemoryStack do jogo
// (pool de 512 KB). O retail so reserva 0xD40 bytes antes da leitura (o printf "Prepare for crash" compara com 0x30D40), entao o teto
// real e o que a pilha tem livre naquela hora (hi1 menos o inicio do buffer), em setores inteiros (o fileReadf le setores de 2 KB). O
// .PTR que nao cabe e lido na RAM livre logo acima do fim da carga (os arquivos da fase e dos monstros), e a pilha volta como estava.
PS2Runtime::RecompiledFunction g_origPtrReloc = nullptr;

void ptrRelocate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    constexpr uint32_t kFn = wotm::fn::dbsRelocateViaPtrListFile__Fi10_vramAddrs, kMs = wotm::sym::x_11MemoryStack_global,
                       kFs = wotm::sym::fileStatus;
    const uint32_t idx = getRegU32(ctx, 4), ngpN = rdram[kFs + 0x74];
    if (ctx->pc != kFn || idx == 0 || idx >= ngpN || ngpN > 13)
    {
        g_origPtrReloc(rdram, ctx, rt);
        return;
    }
    char name[9] = {};
    for (int c = 0; c < 8; ++c)
        name[c] = char(std::toupper(rdram[kFs + 0xB1 + 9 * idx + c]));
    const std::string rel = std::string("MON/") + name + ".PTR";
    const wotm::mods::File *m = Registry::get().find(Registry::get().aliasOf(rel));
    if (!m)
    {   // .PTR do disco: o retail cabe
        g_origPtrReloc(rdram, ctx, rt);
        return;
    }
    const uint32_t low = rd32(rdram, kMs + 4), hi1 = rd32(rdram, kMs + 0x10);
    const uint32_t buf = ((((low + 3) & ~3u) + 4) + 15) & ~15u;
    const uint64_t need = (m->size + 2047) & ~uint64_t(2047);
    const uint64_t room = hi1 > buf ? hi1 - buf : 0;
    if (need <= room)
    {
        std::fprintf(stderr, "[mods] %s (%s): %llu bytes, cabe na pilha do jogo (%llu KB livres)\n", rel.c_str(), m->mod.c_str(),
                     (unsigned long long)m->size, (unsigned long long)(room / 1024));
        g_origPtrReloc(rdram, ctx, rt);
        return;
    }
    // fim da carga: o maior endereco de carga das tres listas do fileStatus (.TEX, .NGP, .RTX), como no wotmCheckLoadEnd
    const uint32_t texN = rdram[kFs + 0x38], resN = rdram[kFs + 0xB0];
    uint32_t end = rd32(rdram, kFs + 0x3C + 4 * ngpN);
    if (texN <= 13) end = std::max(end, rd32(rdram, kFs + 4 * texN));
    if (resN <= 13) end = std::max(end, rd32(rdram, kFs + 0x78 + 4 * resN));
    const uint32_t scratch = (std::max<uint32_t>(end & 0x1FFFFFFFu, PS2_GUEST_RAM_SIZE) + 0xFFFF) & ~0xFFFFu;
    if (uint64_t(scratch) + need + 0x20 > uint64_t(PS2_RAM_SIZE) - 0x100000u)
    {
        std::fprintf(stderr, "[mods] %s (%s): %llu bytes, AVISO: nao cabe na pilha do jogo (%llu KB livres) nem acima da carga (0x%07X); "
                     "a leitura vai sobrescrever o que vem depois\n", rel.c_str(), m->mod.c_str(), (unsigned long long)m->size,
                     (unsigned long long)(room / 1024), scratch);
        g_origPtrReloc(rdram, ctx, rt);
        return;
    }
    std::fprintf(stderr, "[mods] %s (%s): %llu bytes, maior que a pilha do jogo (%llu KB livres): lido em 0x%07X, acima da carga\n",
                 rel.c_str(), m->mod.c_str(), (unsigned long long)m->size, (unsigned long long)(room / 1024), scratch);
    wr32(rdram, kMs + 4, scratch);   // o pushMark da funcao grava a marca ali; o popMark devolve low = scratch e a marca antiga
    g_origPtrReloc(rdram, ctx, rt);
    wr32(rdram, kMs + 4, low);
}
} // namespace wotm_mods_hooks

void wotmModsInstall(PS2Runtime &runtime, const std::string &elfPath)
{
    using namespace wotm_mods_hooks;
    namespace fs = std::filesystem;
    // Veiculos militares de VEGAS como MON/ARMY.* (exercito em qualquer fase, wotm_army.inc): gerados do disco do usuario numa cache ao
    // lado do ELF e servidos pelo registro, com ou sem a pasta de mods (o WOTM_MODS=0 so desliga a pasta)
    {
        const fs::path cache = fs::path(elfPath).parent_path() / "cache" / "army";
        const char *iso = std::getenv("WOTM_ISO");
        if (!iso) iso = std::getenv("PS2X_CD_IMAGE");
        std::string log;
        if (wotm::army_files::ensure(cache, fs::path(elfPath).parent_path(), iso ? iso : "", log))
            for (const char *f : wotm::army_files::kFiles)
                Registry::get().addGenerated(std::string("MON/") + f, cache / f, "army (gerado)");
        std::fprintf(stderr, "[exercito] arquivos MON/ARMY.*: %s\n", log.c_str());
    }
    fs::path dir;
    if (const char *e = std::getenv("WOTM_MODS"))
    {
        if (std::strcmp(e, "0") == 0)
            std::fprintf(stderr, "[mods] pasta de mods desligada (WOTM_MODS=0)\n");
        else
            dir = e;
    }
    else
    {
        // mods/ com esse nome exato: no Windows o caminho "mods" ao lado do ELF da pasta do disco cai no MOD/ do jogo (os IRX)
        auto modsIn = [](const fs::path &parent) {
            std::error_code ec;
            for (fs::directory_iterator it(parent, ec), end; !ec && it != end; it.increment(ec))
                if (it->path().filename() == "mods" && it->is_directory(ec))
                    return true;
            return false;
        };
        dir = fs::path(elfPath).parent_path() / "mods";
        // sem ela, a do repo (WOTM_REPO, que o run_live.bat e o launcher passam): os mods que vem no repo (Goliath Prime,
        // Cerebulon) valem sem copiar nada
        const char *repo = std::getenv("WOTM_REPO");
        if (!modsIn(fs::path(elfPath).parent_path()) && repo && *repo && modsIn(fs::path(repo)))
            dir = fs::path(repo) / "mods";
    }
    wotmBoot("mods: lendo %s", dir.empty() ? "(sem pasta)" : dir.string().c_str());
    const size_t n = Registry::get().scan(dir);
    if (n == 0)
        return;   // sem arquivos de mod: nenhum gancho, o jogo le o disco como sempre
    g_origSearch = runtime.lookupFunction(wotm::fn::fileCdSearchFile__FP10sceCdlFILEPc);
    g_origCdRead = runtime.lookupFunction(wotm::fn::sceCdRead);
    if (!g_origSearch || !g_origCdRead)
    {
        std::fprintf(stderr, "[mods] fileCdSearchFile/sceCdRead nao achados: mods desligados\n");
        return;
    }
    runtime.registerFunction(wotm::fn::fileCdSearchFile__FP10sceCdlFILEPc, hostLeaf<searchFile>);
    runtime.registerFunction(wotm::fn::sceCdRead, hostLeaf<cdRead>);
    if ((g_origPtrReloc = runtime.lookupFunction(wotm::fn::dbsRelocateViaPtrListFile__Fi10_vramAddrs)))
        runtime.registerFunction(wotm::fn::dbsRelocateViaPtrListFile__Fi10_vramAddrs, &ptrRelocate);
    if ((g_origZip = runtime.lookupFunction(wotm::fn::zipInflateAll__FPcPv)))
        runtime.registerFunction(wotm::fn::zipInflateAll__FPcPv, &zipInflate);
    if ((g_origReadf = runtime.lookupFunction(wotm::fn::fileReadf__FPcPv)))
        runtime.registerFunction(wotm::fn::fileReadf__FPcPv, &readf);
    std::string names;
    for (const std::string &m : Registry::get().modNames())
        names += (names.empty() ? "" : ", ") + m;
    if (names.empty())
        std::fprintf(stderr, "[mods] %zu arquivo(s) gerados pelo port, sem pasta de mods\n", n);
    else
        std::fprintf(stderr, "[mods] %zu arquivo(s) de %s em %s\n", n, names.c_str(), dir.string().c_str());
}
