// Monstros custom (MODS.md, "Monstros custom"): monstros que o jogo nao oferece na selecao, jogados numa luta comum. Os chefes
// que sao Monster: o Goliath Prime (tipo 13, "assboss", MON/ASSBOSS1.*), chefe da aventura em canyon2, e as duas ultimas fases do
// Cerebulon na fase final (14 "jelly", MON/JELLY0.*, e 15 "final", MON/FINAL0.*; a primeira, o Mecha, e peca da fase e nao
// Monster). Incluido pelo wotm_overrides.cpp, depois do wotm_skins.inc. Investigacao: git show
// 18bb1d2:docs/investigacoes/2026-10-23/goliath_codigo.md (e goliath_selecao.md).
//
// - TheGame::SetAIMonster: a cada IA que entra, o retail desliga todo slot dos tipos 0x1A0, 0x1C0 e 0x1E0 (disableMonster:
//   m_playerNum 0 e drawMe 0 no m_cs, na sombra e nos _cs de +0x71F8/+0x71FC; na aventura e o que esconde o chefe ate a cena).
//   Com um chefe como jogador ele ficava invisivel e parado. Aqui o que o disableMonster zera nos jogadores (m_monsters[0..3])
//   volta depois da chamada; o chefe da aventura e uma IA (m_monsters[4..]) e continua como no retail. O retail tambem conta todo
//   slot 0x1C0/0x1E0 em m_numAIs (os dois esperam escondidos na fase final): um jogador desses tipos sai da conta, senao os lacos
//   por m_monsters[4 + i] leriam uma IA a mais (nula).
// - Monster::takeHit: no 0x1A0 o dano so passa com o attackState do chefe em 4 ou 10 ou vindo do jogador 1 (attackerId ==
//   m_monsters[0]->m_id). Com o Goliath jogador, as IAs nao o machucavam. Durante a chamada, o m_id do jogador 1 vale o do
//   atacante (a unica leitura de m_monsters[0] no takeHit e esse teste); o resto do takeHit segue o caminho normal (bloqueio,
//   recuos). So quando o Goliath atingido e um jogador.

#include "wotm_disc.hpp"   // .CHR do monstro base da camera (camTable)

namespace wotm_custom
{
    constexpr uint32_t kSetAI = wotm::fn::SetAIMonster__7TheGameiiii, kTakeHit = wotm::fn::takeHit__7MonsterP8_fvectorfi;
    constexpr uint32_t kGameObj = wotm::sym::shellGame;
    constexpr uint32_t kMonsters = 0x120380;          // TheGame::m_monsters[8]
    constexpr uint32_t kTypeBits = 0x14, kPlayerNum = 0x18, kId = 0x20, kCs = 0xC, kShadow = 0x1A3C, kCsA = 0x71F8, kCsB = 0x71FC;
    constexpr uint32_t kDrawMe = 0xC;                 // _cs::drawMe (byte)
    constexpr uint32_t kNumAIs = 0x1203E0, kGameMode = 0x1203C8;   // TheGame::m_numAIs, m_gameMode
    constexpr uint32_t kSlots = 0xB80, kSlotStride = 0x11190, kNumSlots = 0x1203D4;   // TheGame::m_slots[16], m_numSlots
    constexpr uint32_t kGoliath = 13u << 5, kJelly = 14u << 5, kFinal = 15u << 5;
    constexpr uint32_t kSpecialState = 0x7980;        // Monster::m_specialState (o estado do golpe especial)
    // Chefe que e Monster (a selecao nao oferece; o SetAIMonster o esconde)
    bool bossType(uint32_t bits) { return bits == kGoliath || bits == kJelly || bits == kFinal; }
    PS2Runtime::RecompiledFunction g_origSetAI = nullptr, g_origTakeHit = nullptr;

    bool isPlayerSlot(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m)
    {
        for (uint32_t i = 0; i < 4; ++i)
            if (READ32(kGameObj + kMonsters + 4 * i) == m)
                return true;
        return false;
    }

    struct Saved
    {
        uint32_t m = 0, playerNum = 0;
        uint32_t cs[4] = {};
        uint8_t draw[4] = {};
    };

    void setAI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != kSetAI)
        {
            g_origSetAI(rdram, ctx, runtime);
            return;
        }
        // fora da aventura, todos os slots de chefe (um chefe CPU tambem fica visivel); na aventura, so os dos jogadores
        const bool adventure = READ32(kGameObj + kGameMode) == 1u;
        const uint32_t slots = std::min(16u, READ32(kGameObj + kNumSlots));
        Saved saved[16];
        int n = 0;
        for (uint32_t i = 0; i < slots; ++i)
        {
            const uint32_t m = kGameObj + kSlots + i * kSlotStride;
            if (!bossType(READ32(m + kTypeBits)) || (adventure && !isPlayerSlot(rdram, ctx, runtime, m)))
                continue;
            Saved &s = saved[n++];
            s.m = m;
            s.playerNum = READ32(m + kPlayerNum);
            const uint32_t at[4] = {kCs, kShadow, kCsA, kCsB};
            for (int k = 0; k < 4; ++k)
                if ((s.cs[k] = READ32(m + at[k])))
                    s.draw[k] = READ8(s.cs[k] + kDrawMe);
        }
        g_origSetAI(rdram, ctx, runtime);
        int32_t ais = static_cast<int32_t>(READ32(kGameObj + kNumAIs));
        bool players = false;
        for (int j = 0; j < n; ++j)
        {
            const Saved &s = saved[j];
            // IA desta luta (m_monsters[4..]): o aiInit lhe deu m_playerNum 2, que o disableMonster zerou. So o Jelly e o Cerebulon:
            // a IA do Goliath (Ai::updateAssBoss) precisa dos pontos da fase dele
            bool cpu = false;
            const uint32_t bits = READ32(s.m + kTypeBits);
            for (uint32_t i = 0; i < 12 && !cpu && (bits == kJelly || bits == kFinal); ++i)
                cpu = READ32(kGameObj + kMonsters + 4 * (4 + i)) == s.m;
            const bool player = isPlayerSlot(rdram, ctx, runtime, s.m);
            if (!player && !cpu)
                continue;   // carregado e ainda sem IA: o retail o esconde ate o SetAIMonster dele
            WRITE32(s.m + kPlayerNum, player ? s.playerNum : 2u);
            // o jogador volta como estava; a CPU fica com o corpo e a sombra ligados, como uma IA comum depois do SetAIMonster
            // (antes dele os quatro estao desligados), e os dois _cs extras desligados, como os do jogador
            for (int k = 0; k < 4; ++k)
                if (s.cs[k])
                    WRITE8(s.cs[k] + kDrawMe, player ? s.draw[k] : uint8_t(k < 2));
            if (!player)
                continue;
            // contado pelo retail (m_playerNum 2 antes do disableMonster, ou tipo 0x1C0/0x1E0); o setAIMonsterCounted do
            // WOTM_AIVSAI nao o desconta, porque ja o le zerado
            const uint32_t type = READ32(s.m + kTypeBits);
            if (s.playerNum == 2u || type == kJelly || type == kFinal)
                --ais;
            players = true;
        }
        if (players)
            WRITE32(kGameObj + kNumAIs, static_cast<uint32_t>(ais < 0 ? 0 : ais));
    }

    void takeHit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t self = GPR_U32(ctx, 4), attacker = GPR_U32(ctx, 6);
        const uint32_t p1 = ctx->pc == kTakeHit && READ32(self + kTypeBits) == kGoliath && isPlayerSlot(rdram, ctx, runtime, self)
                                ? READ32(kGameObj + kMonsters) : 0;
        const uint32_t oldId = p1 ? READ32(p1 + kId) : 0;
        if (p1)
            WRITE32(p1 + kId, attacker);
        g_origTakeHit(rdram, ctx, runtime);
        if (p1)
            WRITE32(p1 + kId, oldId);
    }

    // Som: o SoundManager::loadMonsterBank (e o de baixa resolucao) nao tem caso para os chefes. Os sons do Goliath sao do banco do
    // chefe (gSoundBanks[BANK_MECH], Assemb_s.bnk), que o retail so carrega na cena do chefe em canyon2 (canyon2BossIntro); os do
    // Jelly e da forma final, do Final_s.bnk (gSoundBanks[BANK_FINAL]), que o loadRTSoundBanks so carrega na fase final da
    // aventura. Fora da aventura, o pedido do banco do 0x1A0 vira o SoundManager::loadMechBossSoundBank (mesmo this), como um
    // salto, e o do 0x1C0/0x1E0 vira o m_banks.loadSoundBank(&gSoundBanks[BANK_FINAL], "Final_s.bnk", autoUnload) (que nao faz nada
    // com o banco ja carregado: os dois tipos na mesma luta dividem um).
    constexpr uint32_t kLoadBank = wotm::fn::loadMonsterBank__12SoundManagerii, kLoadLoBank = wotm::fn::loadLoResMonsterBank__12SoundManagerii,
                       kLoadMech = wotm::fn::loadMechBossSoundBank__12SoundManager,
                       kLoadSoundBank = wotm::fn::loadSoundBank__16SoundBankManagerPPvPci;
    constexpr uint32_t kSndBanks = 0x210, kBankFinal = 24;   // SoundManager::m_banks, BANK_FINAL
    PS2Runtime::RecompiledFunction g_origLoadBank = nullptr, g_origLoadLoBank = nullptr, g_loadMech = nullptr;

    // Traje de um Monster pelo arquivo de onde veio: o no raiz (m_cs->epNode) fica dentro do .NGP dele, e o fileStatus guarda o
    // nome ("JELLY4"); o numero do fim e o traje. -1 se nao achou. Dois customs do mesmo tipo (o Jelly do disco e o Mecha, com
    // arquivos proprios no traje 4) so se distinguem assim: o m_skinNum vira o traje base (wotm_skins.inc).
    uint32_t cRd32(const uint8_t *rdram, uint32_t a) { uint32_t v; std::memcpy(&v, rdram + (a & 0x01FFFFFFu), 4); return v; }
    uint32_t cRd8(const uint8_t *rdram, uint32_t a) { return rdram[a & 0x01FFFFFFu]; }
    int monsterCostume(uint8_t *rdram, uint32_t m)
    {
        constexpr uint32_t kFileStatus = wotm::sym::fileStatus;
        if (!m)
            return -1;
        const uint32_t cs = cRd32(rdram, m + kCs) & 0x1FFFFFFFu;
        const uint32_t ep = cs ? cRd32(rdram, cs) & 0x1FFFFFFFu : 0;
        const uint32_t n = cRd8(rdram, kFileStatus + 0x74);
        for (uint32_t i = 1; ep && i < n && i < 13; ++i)
        {
            const uint32_t lo = cRd32(rdram, kFileStatus + 0x3C + 4 * i) & 0x1FFFFFFFu, hi = cRd32(rdram, kFileStatus + 0x3C + 4 * (i + 1)) & 0x1FFFFFFFu;
            if (ep < lo || ep >= hi)
                continue;
            char name[10] = {};
            for (uint32_t k = 0; k < 9; ++k)
                name[k] = char(cRd8(rdram, kFileStatus + 0xB1 + 9 * i + k));
            size_t e = std::strlen(name), b = e;
            while (b > 0 && std::isdigit(static_cast<unsigned char>(name[b - 1])))
                --b;
            return b < e ? std::atoi(name + b) : -1;
        }
        return -1;
    }
    // O custom de um Monster da luta, pelo tipo e pelo traje do arquivo. Traje de mod (4+): o custom dono dos arquivos proprios
    // ou da skin; a skin de um monstro do jogo nao e custom. Traje do disco: o custom sem "files" nesse traje. Sem traje achado,
    // o primeiro custom do tipo, so nos chefes (num jogavel e o monstro do jogo).
    const wotm::mods::Custom *customOf(uint8_t *rdram, uint32_t m)
    {
        const uint32_t type = m ? cRd32(rdram, m + kTypeBits) >> 5 : 0;
        if (!type)
            return nullptr;
        const int costume = monsterCostume(rdram, m);
        const wotm::mods::Registry &r = wotm::mods::Registry::get();
        if (costume >= 4)
            if (const wotm::mods::Skin *k = r.skinByIndex(int(type), costume))
                return k->custom.empty() ? nullptr : r.custom(k->custom);
        const wotm::mods::Custom *cu = costume >= 0 ? r.customByType(int(type), costume) : nullptr;
        return cu || wotm::mods::monsterInfo(int(type)) ? cu : r.customByType(int(type));
    }

    // O nome do disco de cada tipo no MonsterNewNames, guardado antes da primeira troca: o setPov o devolve quando o monstro da
    // camera e um jogavel que nao e custom (a entrada e uma so por tipo).
    std::string g_diskName[17];
    bool g_diskNameSaved[17] = {};
    void writeShownName(uint8_t *rdram, uint32_t type, const std::string &shown)
    {
        if (type >= 17)
            return;
        static std::string last[17];
        if (last[type] != shown)
        {
            std::fprintf(stderr, "[custom] nome na tela do tipo %u: \"%s\"\n", type, shown.c_str());
            last[type] = shown;
        }
        for (const uint32_t names : {wotm::sym::MonsterNewNames_006E8958, wotm::sym::MonsterNewNames_006E8D90, wotm::sym::MonsterNewNames_006EAA78,
                                     wotm::sym::MonsterNewNames_006ED318, wotm::sym::MonsterNewNames_006EFEC8})
        {
            char *e = reinterpret_cast<char *>(rdram + ((names + type * 12) & 0x01FFFFFFu));
            if (!g_diskNameSaved[type])
            {
                g_diskName[type].assign(e, strnlen(e, 12));
                g_diskNameSaved[type] = true;
            }
            std::memset(e, 0, 12);
            std::memcpy(e, shown.data(), std::min<size_t>(shown.size(), 11));
        }
    }
    void restoreShownName(uint8_t *rdram, uint32_t type)
    {
        if (type < 17 && g_diskNameSaved[type])
            writeShownName(rdram, type, g_diskName[type]);
    }

    // Nome na tela ("%s CRUSHED", "%s WINS"): a entrada do tipo nas 5 copias do MonsterNewNames[17][12] ("assboss", "jelly",
    // "final"); fica o "name" do monster.json (ate 11 letras, minusculas como as do jogo) ou `fallback`. Sem `cu`, o primeiro
    // custom do tipo (o setPov o reescreve depois com o do monstro da camera).
    void setShownName(uint8_t *rdram, uint32_t type, const char *fallback, const wotm::mods::Custom *cu = nullptr)
    {
        std::string shown = fallback;
        if (!cu)
            cu = wotm::mods::Registry::get().customByType(int(type));
        if (cu)
        {
            std::string n = cu->name;
            if (n.size() > 11)   // "GOLIATH PRIME" -> "goliath": corta na ultima palavra que cabe
            {
                const size_t sp = n.rfind(' ', 11);
                n = n.substr(0, sp != std::string::npos && sp > 0 ? sp : 11);
            }
            shown.clear();
            for (char ch : n)
                shown += char(std::tolower(static_cast<unsigned char>(ch)));
        }
        writeShownName(rdram, type, shown);
    }

    void loadBankCommon(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t entry, PS2Runtime::RecompiledFunction orig)
    {
        const uint32_t bits = ctx->pc == entry ? GPR_U32(ctx, 5) : 0;
        if ((bits == kJelly || bits == kFinal) && READ32(kGameObj + kGameMode) != 1u)
        {
            const uint32_t self = GPR_U32(ctx, 4), autoUnload = GPR_U32(ctx, 6);
            std::fprintf(stderr, "[custom] banco de som do %s (Final_s.bnk)\n", bits == kJelly ? "Jelly" : "Cerebulon");
            setShownName(rdram, bits >> 5, bits == kJelly ? "jelly" : "cerebulon");
            wotm_modes4::call(rdram, ctx, runtime, kLoadSoundBank,
                              {self + kSndBanks, wotm::sym::gSoundBanks + 4 * kBankFinal, wotm_modes4::scratchText(rdram, "Final_s.bnk"), autoUnload});
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        if (bits == kGoliath && READ32(kGameObj + kGameMode) != 1u && g_loadMech)
        {
            std::fprintf(stderr, "[custom] banco de som do Goliath (Assemb_s.bnk, loadMechBossSoundBank)\n");
            setShownName(rdram, 13, "goliath");
            ctx->pc = kLoadMech;
            g_loadMech(rdram, ctx, runtime);
            return;
        }
        orig(rdram, ctx, runtime);
    }
    void loadBank(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { loadBankCommon(rdram, ctx, runtime, kLoadBank, g_origLoadBank); }
    void loadLoBank(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { loadBankCommon(rdram, ctx, runtime, kLoadLoBank, g_origLoadLoBank); }

    // .CHR e .AIC proprios (pasta "files", Registry::scanResetFiles). O charreset::setCharacter (MonsterTweaks::setActiveMonster,
    // no initAfterDbLoad) e o AiReset::setAi (AiTweaks::setActiveAi, no Ai::init) montam o nome do arquivo com o MonsterLongNames
    // do tipo no resetcom::m_fileName ("lizard"; "rst/lizard1.aic", ou "<fase>/lizard1.aic" na aventura), e o forceRead seguinte
    // le o arquivo e grava os valores no Monster/Ai guardado em +0x40C0. Num Monster com o traje de um custom (o dos arquivos
    // proprios ou uma skin dele), o nome vira o do custom: so pelo traje, porque um custom sem "files" nao se distingue do monstro
    // do jogo do mesmo tipo. Cada instancia le o seu: dois customs do mesmo tipo na luta ficam com os numeros de cada um.
    constexpr uint32_t kSetCharacter = wotm::fn::setCharacter__9charresetP7Monster, kSetAiReset = wotm::fn::setAi__7AiResetP2Ai;
    constexpr uint32_t kResetFileName = 0xC040, kResetFileNameSize = 0x40;   // resetcom::m_fileName[0x40]
    PS2Runtime::RecompiledFunction g_origSetCharacter = nullptr, g_origSetAiReset = nullptr;
    const wotm::mods::Custom *ownFilesCustom(uint8_t *rdram, uint32_t m)
    {
        const int costume = m ? monsterCostume(rdram, m) : -1;
        if (costume < 4)
            return nullptr;
        const wotm::mods::Registry &r = wotm::mods::Registry::get();
        const wotm::mods::Skin *k = r.skinByIndex(int(cRd32(rdram, m + kTypeBits) >> 5), costume);
        return k && !k->custom.empty() ? r.custom(k->custom) : nullptr;
    }
    std::string resetName(uint8_t *rdram, uint32_t self)
    {
        const char *s = reinterpret_cast<const char *>(rdram + ((self + kResetFileName) & 0x01FFFFFFu));
        return std::string(s, strnlen(s, kResetFileNameSize));
    }
    void setResetName(uint8_t *rdram, uint32_t self, const std::string &name, const wotm::mods::Custom &cu, const char *what)
    {
        const std::string old = resetName(rdram, self);
        char *dst = reinterpret_cast<char *>(rdram + ((self + kResetFileName) & 0x01FFFFFFu));
        std::memset(dst, 0, kResetFileNameSize);
        std::memcpy(dst, name.data(), std::min<size_t>(name.size(), kResetFileNameSize - 1));
        static std::set<std::string> logged;   // uma linha por custom e arquivo (o jogo le de novo a cada luta)
        if (logged.insert(cu.id + "/" + name).second)
            std::fprintf(stderr, "[custom] %s: %s proprio (%s no lugar de %s)\n", cu.id.c_str(), what, name.c_str(), old.c_str());
    }
    void setCharacter(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != kSetCharacter)
        {
            g_origSetCharacter(rdram, ctx, runtime);
            return;
        }
        const uint32_t self = GPR_U32(ctx, 4), m = GPR_U32(ctx, 5);
        g_origSetCharacter(rdram, ctx, runtime);
        if (const wotm::mods::Custom *cu = ownFilesCustom(rdram, m); cu && cu->ownChr)
            setResetName(rdram, self, cu->resetStem, *cu, ".CHR");
    }
    void setAiReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != kSetAiReset)
        {
            g_origSetAiReset(rdram, ctx, runtime);
            return;
        }
        const uint32_t self = GPR_U32(ctx, 4), ai = GPR_U32(ctx, 5);
        g_origSetAiReset(rdram, ctx, runtime);
        const wotm::mods::Custom *cu = ai ? ownFilesCustom(rdram, cRd32(rdram, ai)) : nullptr;   // Ai +0: o Monster
        if (!cu || !cu->ownAic)
            return;
        // "rst/lizard1.aic" -> "rst/C_<ID>1.aic" (tambem na aventura: o custom nao tem .AIC por fase)
        const std::string old = resetName(rdram, self);
        const size_t dot = old.size() >= 5 ? old.size() - 4 : std::string::npos;
        const char d = dot != std::string::npos ? old[dot - 1] : 0;
        if (d < '0' || d > '2')
        {
            std::fprintf(stderr, "[custom] %s: nome de .AIC inesperado (%s); fica o do tipo\n", cu->id.c_str(), old.c_str());
            return;
        }
        setResetName(rdram, self, "rst/" + cu->resetStem + d + ".aic", *cu, ".AIC");
    }

    // Camera: o RST/ASSBOSS.CHR traz parametros de camera que o retail nunca usou (no POV 0 a camera fica na frente e baixa:
    // m_00 = (-70, 336, 90) contra (0, -255, 150) do Togera), e a do RST/JELLY.CHR fica nas pernas do Jelly. A tabela de camera do
    // monstro (+0x6CF0: 10 POVs de 0xA0, mais 10 da tela dividida) vem do .CHR dele (MonsterTweaks::setActiveMonster) e o
    // Cameras::SetCameraPOV copia o POV pedido para a camera. Um custom com "camera" fica com a tabela do .CHR do monstro base,
    // com as tres distancias de cada POV (m_00, m_10, m_20) escaladas: na entrada do SetCameraPOV de uma camera que segue o custom,
    // a tabela calculada vai para o monstro antes da copia.
    // A tabela do base sai do .CHR lido no host (mods/ ou disco), da chave de camera como o charreset::readKey a le (tipo 0xFC6:
    // depois de 0x10 bytes, 2 telas de 9 POVs de 0xA0, que vao para os POVs 0..8 de cada tela; o resto da tabela fica o do
    // monstro). Nada de ler o .CHR pelo jogo dentro de um gancho: o setActiveMonster chamado duas vezes deixava as vezes a
    // tabela do proprio .CHR por cima da emprestada.
    constexpr uint32_t kSetPov = wotm::fn::SetCameraPOV__7CamerasiQ26Camera9CameraPOV;
    constexpr uint32_t kCamTable = 0x6CF0, kCamPovs = 20, kCamPov = 0xA0, kCamSplit = 0x640;
    constexpr uint32_t kCameras = wotm::sym::x_7Cameras_m_cameras, kCamStride = 0xEB0, kCamMonster = 0xCF4;
    constexpr uint32_t kChrCameraKey = 0xFC6, kChrKeyData = 0x10, kChrPovs = 9, kChrSplit = kChrPovs * kCamPov, kChrHeader = 12;
    // Sem monster.json (WOTM_MATCH=13): a do Togera em dobro. Com ele, a do "camera" do mod ({"from", "scale"}).
    constexpr const char *kCamBaseDefault = "lizard";
    constexpr float kCamScaleDefault = 2.0f;
    PS2Runtime::RecompiledFunction g_origSetPov = nullptr;
    std::map<std::string, std::vector<uint8_t>> g_camTables;   // "<base>*<escala>" -> tabela pronta (vazia = falhou)

    // Chave de camera do .CHR -> tabela (sobre `cam`, a do monstro); false sem a chave
    bool chrCamera(const std::vector<uint8_t> &file, std::vector<uint8_t> &cam)
    {
        auto rd = [&](size_t o) { uint32_t v = 0; if (o + 4 <= file.size()) std::memcpy(&v, &file[o], 4); return v; };
        for (size_t o = kChrHeader; o + 8 <= file.size();)
        {
            const uint32_t type = rd(o), size = rd(o + 4);
            if (type == 1 || size < 8)
                break;
            if (type == kChrCameraKey && o + kChrKeyData + 2 * kChrSplit <= file.size())
            {
                for (uint32_t s = 0; s < 2; ++s)
                    for (uint32_t p = 0; p < kChrPovs; ++p)
                    {
                        const uint8_t *src = &file[o + kChrKeyData + s * kChrSplit + p * kCamPov];
                        uint8_t *dst = &cam[s * kCamSplit + p * kCamPov];
                        for (uint32_t v = 0; v < 3; ++v)   // m_00, m_10, m_20 (sem o w)
                            std::memcpy(dst + v * 16, src + v * 16, 12);
                        std::memcpy(dst + 0x30, src + 0x30, 0x94 - 0x30);   // 3 PitchChange e os escalares
                        std::memcpy(dst + 0x94, src + 0x98, 4);             // 0x94 e 0x98 trocados no readKey
                        std::memcpy(dst + 0x98, src + 0x94, 4);
                        std::memcpy(dst + 0x9C, src + 0x9C, 4);
                    }
                return true;
            }
            o += size;
        }
        return false;
    }

    const std::vector<uint8_t> &camTable(uint8_t *rdram, uint32_t m, const std::string &base, float scale, const char *who)
    {
        const std::string key = base + "*" + std::to_string(scale);
        if (const auto it = g_camTables.find(key); it != g_camTables.end())
            return it->second;
        std::vector<uint8_t> &cam = g_camTables[key];
        std::string up = base;
        for (char &ch : up)
            ch = char(std::toupper(static_cast<unsigned char>(ch)));
        const std::string rel = "RST/" + up + ".CHR";
        std::vector<uint8_t> file;
        const std::string host = wotm::mods::Registry::get().hostPath(rel);
        if (!host.empty())
        {
            std::ifstream in(std::filesystem::u8path(host), std::ios::binary);
            file.assign(std::istreambuf_iterator<char>(in), {});
        }
        else
        {
            const PS2Runtime::IoPaths &io = PS2Runtime::getIoPaths();
            wotm::disc::readFile(io.cdRoot, io.cdImage.string(), rel, file);
        }
        const uint8_t *const mon = rdram + ((m + kCamTable) & 0x01FFFFFFu);
        std::vector<uint8_t> table(mon, mon + kCamPovs * kCamPov);
        if (!chrCamera(file, table))
        {
            std::fprintf(stderr, "[custom] %s: camera do %s nao carregou (%s); fica a do proprio .CHR\n", who, rel.c_str(),
                         file.empty() ? "arquivo nao achado" : "sem a chave de camera");
            return cam;
        }
        cam = std::move(table);
        for (uint32_t p = 0; p < kCamPovs; ++p)
            for (uint32_t v = 0; v < 3; ++v)
                for (uint32_t c = 0; c < 3; ++c)
                {
                    float f;
                    std::memcpy(&f, &cam[p * kCamPov + v * 16 + c * 4], 4);
                    f *= scale;
                    std::memcpy(&cam[p * kCamPov + v * 16 + c * 4], &f, 4);
                }
        float d10[3];
        std::memcpy(d10, cam.data(), 12);
        std::fprintf(stderr, "[custom] %s: camera do %s, distancias x%.2f (POV 0: %.0f, %.0f, %.0f)\n", who, rel.c_str(), scale, d10[0], d10[1], d10[2]);
        return cam;
    }

    void setPov(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t view = GPR_U32(ctx, 4);
        const uint32_t m = ctx->pc == kSetPov && view < 4 ? READ32(kCameras + view * kCamStride + kCamMonster) : 0;
        const uint32_t type = m ? READ32(m + kTypeBits) >> 5 : 0;
        const wotm::mods::Custom *cu = type ? customOf(rdram, m) : nullptr;
        if (cu && cu->files.empty() == false)
            setShownName(rdram, type, cu->typeName.c_str(), cu);
        else if (!cu && type && wotm::mods::monsterInfo(int(type)))
            restoreShownName(rdram, type);
        const char *base =cu && !cu->cameraFrom.empty() ? cu->cameraFrom.c_str() : type == 13 && !cu ? kCamBaseDefault : nullptr;
        if (base)
        {
            const float scale = cu && !cu->cameraFrom.empty() ? cu->cameraScale : kCamScaleDefault;
            const std::vector<uint8_t> &cam = camTable(rdram, m, base, scale, cu ? cu->id.c_str() : "assboss");
            if (!cam.empty())
                std::memcpy(rdram + ((m + kCamTable) & 0x01FFFFFFu), cam.data(), cam.size());
        }
        g_origSetPov(rdram, ctx, runtime);
    }
    // Dardo (StateJavelin): o Weapon::DetonateWeapon chama o Monster::attachPickupImpaler na vitima, que entra no StateImpaled pelo
    // enterNewState sem o transitionOK (que pede as anims 0x8E, 0x8F e 0x90). O StateImpaled::update fica na 0x8E (preso; o debater
    // alterna 0x10F/0x110 e volta a ela) e so sai com a 0x90 no fim; sem ela (ASSBOSS0/1, JELLY) a vitima travava para sempre. A 0x8F
    // ninguem usa (o debater e a 0x10F/0x110): Togera, Zorgulon e Cerebulon (LIZARD, ALIEN, FINAL) nao a tem e sao empalados no retail. Sem a 0x8E ou a 0x90, o gancho faz o que o retail
    // faz com pickup invalido: killPickup(it, 1) e volta. O golpe do objeto (dispatchHit) chega como um comum. Investigacao: 18bb1d2:docs/investigacoes/2026-10-23/goliath_impale.md.
    constexpr uint32_t kImpale = wotm::fn::attachPickupImpaler__7MonsterGQ2t10LinkedList1ZP6Pickup8IteratorP8_fvector,
                       kKillPickup = wotm::fn::killPickup__12LevelPickupsGQ2t10LinkedList1ZP6Pickup8Iteratori;
    constexpr uint32_t kAnims = 0x1CF0, kAnimStride = 0x10;   // Monster::m_anims[id].ci
    PS2Runtime::RecompiledFunction g_origImpale = nullptr, g_killPickup = nullptr;

    void impale(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = GPR_U32(ctx, 4);
        bool has = true;
        if (ctx->pc == kImpale && g_killPickup)
            for (const uint32_t id : {0x8Eu, 0x90u})   // preso e arrancar
                has = has && READ32(m + kAnims + id * kAnimStride) != 0;
        if (has)
        {
            g_origImpale(rdram, ctx, runtime);
            return;
        }
        std::fprintf(stderr, "[custom] dardo: monstro tipo %u sem as anims de empalado; vira golpe comum\n", READ32(m + kTypeBits) >> 5);
        ctx->r[4] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(GPR_U32(ctx, 5))));
        ctx->r[5] = _mm_set_epi64x(0, 1);
        ctx->pc = kKillPickup;
        g_killPickup(rdram, ctx, runtime);
    }
    // Giro (StateTopSpin, Monster+0x10EBC; investigacao 18bb1d2:docs/investigacoes/2026-10-23/goliath_topspin.md). Nos monstros comuns X+Quadrado (PadFlags +0x38) e o
    // StateCrowdControl (+0xF3F0), que no Goliath nunca comeca (pede a anim de efeito do tipo, que ele nao tem). So a IA do chefe
    // entrava no TopSpin, pelo setAssBossState(12). Com o Goliath jogador:
    //  - StateCrowdControl::transitionOK: X+Quadrado no chao (Idle ou Run), com ataques ligados, sem objeto grande e o
    //    transitionOK do TopSpin aceito;
    //  - Monster::enterNewState: o CrowdControl vira o TopSpin;
    //  - StateTopSpin::update: o golpe cheio (dano, empurrao, som) so acerta m_monsters[0], que e ele mesmo; durante a chamada vale a
    //    IA viva mais perto;
    //  - Ai::setAssBossState (chamado pelo update a cada batida): so faz algo para o chefe da aventura (o dono e o assBoss).
    constexpr uint32_t kCrowdOK = wotm::fn::transitionOK__17StateCrowdControl, kEnterState = wotm::fn::enterNewState__7MonsterP12MonsterState,
                       kSpinUpdate = wotm::fn::update__12StateTopSpin, kSpinOK = wotm::fn::transitionOK__12StateTopSpin,
                       kSetBossState = wotm::fn::setAssBossState__2Aii, kHoldingLarge = wotm::fn::isHoldingLarge__7Monster,
                       kPadFrame = wotm::fn::x__vc__8PadFlagsi;
    constexpr uint32_t kStateOwner = 0xC, kCurState = 0x34, kIdle = 0x7984, kRun = 0x79AC, kCrowd = 0xF3F0, kSpin = 0x10EBC, kTazer = 0x11130;
    constexpr uint32_t kAttacksOn = 0x4A, kPad = 0x5040, kPadSpin = 0x38, kDead = 0xE8, kNumMonsters = 0x1203D8, kTrans = 0x10;
    PS2Runtime::RecompiledFunction g_origCrowdOK = nullptr, g_origEnterState = nullptr, g_origSpinUpdate = nullptr, g_origSetBossState = nullptr;

    bool goliathPlayer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m)
    {
        return m && READ32(m + kTypeBits) == kGoliath && isPlayerSlot(rdram, ctx, runtime, m);
    }
    void ret(R5900Context *ctx, uint32_t v0)
    {
        ctx->r[2] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(v0)));
        ctx->pc = GPR_U32(ctx, 31);
    }

    void crowdOK(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = ctx->pc == kCrowdOK ? READ32(GPR_U32(ctx, 4) + kStateOwner) : 0;
        if (!goliathPlayer(rdram, ctx, runtime, m))
        {
            g_origCrowdOK(rdram, ctx, runtime);
            return;
        }
        using wotm_modes4::call;
        const uint32_t st = READ32(m + kCurState);
        bool ok = READ8(m + kAttacksOn) != 0 && (st == m + kIdle || st == m + kRun);
        ok = ok && READ16(call(rdram, ctx, runtime, kPadFrame, {m + kPad, 0u}) + kPadSpin) != 0;
        ok = ok && call(rdram, ctx, runtime, kHoldingLarge, {m}) == 0 && call(rdram, ctx, runtime, kSpinOK, {m + kSpin}) != 0;
        ret(ctx, ok ? 1u : 0u);
    }

    void enterState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = GPR_U32(ctx, 4);
        if (ctx->pc == kEnterState && GPR_U32(ctx, 5) == m + kCrowd && goliathPlayer(rdram, ctx, runtime, m))
        {
            ctx->r[5] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(m + kSpin)));
            std::fprintf(stderr, "[custom] Goliath: giro (X+Quadrado)\n");
        }
        if (ctx->pc == kEnterState && GPR_U32(ctx, 5) == m + kTazer && READ32(m + kTypeBits) == kJelly && isPlayerSlot(rdram, ctx, runtime, m))
            std::fprintf(stderr, "[custom] Jelly: choque (%s)\n", READ32(m + kPlayerNum) == 2u ? "IA" : "Triangulo+Circulo");
        // "specialRoar": ao entrar no estado do especial (+0x7980), o grito do MonsterSound::playSignatureSound, que o gancho do
        // banco proprio (wotm_snd.inc) toca do .BNK do custom
        const uint32_t next = GPR_U32(ctx, 5);
        const bool roar = ctx->pc == kEnterState && m && next && next == READ32(m + kSpecialState) && next != READ32(m + kCurState);
        g_origEnterState(rdram, ctx, runtime);
        if (const wotm::mods::Custom *cu = roar ? ownFilesCustom(rdram, m) : nullptr; cu && cu->specialRoar)
        {
            constexpr uint32_t kMonsterSound = 0x1A7C;   // Monster::m_sound
            wotm_modes4::call(rdram, ctx, runtime, wotm::fn::playSignatureSound__12MonsterSound, {m + kMonsterSound});
            std::fprintf(stderr, "[custom] %s: grito no especial\n", cu->id.c_str());
        }
    }

    void spinUpdate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = ctx->pc == kSpinUpdate ? READ32(GPR_U32(ctx, 4) + kStateOwner) : 0;
        uint32_t near = 0;
        if (goliathPlayer(rdram, ctx, runtime, m))
        {
            const uint32_t cs = READ32(m + kCs), n = std::min(8u, READ32(kGameObj + kNumMonsters));
            float best = 1e30f, p[3], q[3];
            std::memcpy(p, rdram + ((cs + kTrans) & 0x01FFFFFFu), 12);
            for (uint32_t i = 0; i < n && cs; ++i)
            {
                const uint32_t o = READ32(kGameObj + kMonsters + 4 * i), ocs = o ? READ32(o + kCs) : 0;
                if (!ocs || o == m || READ32(o + kPlayerNum) != 2u || READ8(o + kDead))
                    continue;
                std::memcpy(q, rdram + ((ocs + kTrans) & 0x01FFFFFFu), 12);
                const float d = (q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2]);
                if (d < best)
                    best = d, near = o;
            }
        }
        const uint32_t p1 = near ? READ32(kGameObj + kMonsters) : 0;
        if (near)
            WRITE32(kGameObj + kMonsters, near);
        g_origSpinUpdate(rdram, ctx, runtime);
        if (near)
            WRITE32(kGameObj + kMonsters, p1);
    }

    void setBossState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kSetBossState)
        {
            const uint32_t boss = READ32(wotm::sym::assBoss);
            if (!boss || READ32(GPR_U32(ctx, 4)) != boss)
            {
                ctx->pc = GPR_U32(ctx, 31);   // void: so o chefe da aventura (ou o WOTM_AIVSAI, que roda o initAssBoss)
                return;
            }
        }
        g_origSetBossState(rdram, ctx, runtime);
    }
    // Olhar (18bb1d2:docs/investigacoes/2026-10-23/goliath_lockon.md): o Monster::updateLookAt vira a cabeca para o inimigo mais perto
    // (getLookAtTarget) pelas anims 154/155. No ASSBOSS elas giram a torre inteira (28 das 30 pecas) em ate 90 graus, o que parece
    // um lock-on permanente. No Goliath jogador, sem alvo de olhar: a torre fica no centro; com o lock (os dois gatilhos) ela mira
    // pelo getTarget, como num monstro comum. O chefe da aventura e o Goliath sob IA seguem o retail.
    constexpr uint32_t kLookAt = wotm::fn::getLookAtTarget__7Monster;
    PS2Runtime::RecompiledFunction g_origLookAt = nullptr;

    void lookAt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = GPR_U32(ctx, 4);
        if (ctx->pc == kLookAt && goliathPlayer(rdram, ctx, runtime, m) && READ32(m + kPlayerNum) != 2u)
        {
            ret(ctx, 0);
            return;
        }
        g_origLookAt(rdram, ctx, runtime);
    }
    // Choque do Jelly (StateUltraTazer, Monster+0x11130): o transitionOK do retail so confere as anims e o isSpecialAvailable
    // (m_specialWeapon), sem botao; na fase final quem decide e o roteiro do FinalBoss::updateJelly. Com o Jelly jogador o choque
    // saia sozinho: aqui ele so comeca com Triangulo+Circulo apertados no controle do jogador (inputGetPadButtons do pad da
    // posicao em m_monsters[0..3]; e a combinacao do especial dos monstros comuns no tipo A, PadEntry +0x36, entao o Jelly
    // continua parado e o transitionOK
    // segue sendo consultado). O Jelly sob a IA (m_playerNum 2) e o da aventura seguem o retail.
    constexpr uint32_t kTazerOK = wotm::fn::transitionOK__15StateUltraTazer, kPadButtons = wotm::fn::inputGetPadButtons__Fi;
    constexpr uint32_t kBtnTriangle = 0x1000, kBtnCircle = 0x2000;   // libpad (inputGetPadButtons ja inverte os ativos em 0)
    constexpr uint32_t kTazerUpdate = wotm::fn::update__15StateUltraTazer;
    constexpr float kTazerReach = 1200.f, kTazerKeep = 1500.f;   // inimigo mais perto para comecar e para continuar o choque
    PS2Runtime::RecompiledFunction g_origTazerOK = nullptr, g_origTazerUpdate = nullptr;
    bool g_tazerHeld[4] = {};   // Triangulo+Circulo ja apertados na chamada anterior do transitionOK sem alvo (por jogador)

    // Monstro vivo e ativo (m_playerNum 1 ou 2) de outro slot mais perto de `m`, a no maximo `maxDist`; 0 sem nenhum
    uint32_t nearestEnemy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m, float maxDist, float *dist)
    {
        const uint32_t cs = READ32(m + kCs), slots = std::min(16u, READ32(kGameObj + kNumSlots));
        if (!cs)
            return 0;
        float p[3], q[3], best = maxDist * maxDist;
        uint32_t near = 0;
        std::memcpy(p, rdram + ((cs + kTrans) & 0x01FFFFFFu), 12);
        for (uint32_t i = 0; i < slots; ++i)
        {
            const uint32_t o = kGameObj + kSlots + i * kSlotStride, ocs = READ32(o + kCs), pn = READ32(o + kPlayerNum);
            if (o == m || !ocs || (pn != 1u && pn != 2u) || READ8(o + kDead))
                continue;
            std::memcpy(q, rdram + ((ocs + kTrans) & 0x01FFFFFFu), 12);
            const float d = (q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2]);
            if (d < best)
                best = d, near = o;
        }
        if (dist)
            *dist = std::sqrt(best);
        return near;
    }

    // Mecha ("kind": "mecha"; o MECHA.NGP do tools/mechamon.py, base Jelly; MODS.md, "Mecha"): o ritmo do FinalBoss::updateMecha.
    // Com o escudo ligado ele toma kShieldDamage do dano e pode disparar o laser, sempre disponivel (o especial e religado antes de
    // cada transitionOK). O disparo usa o StateUltraTazer do Jelly so como roteiro de anims (preparar, disparar e desligar do robo:
    // 160/161/162) e tempo (+0x2C, 180 quadros): na fase de disparo o update original fica de fora (sem os raios do choque) e o laser
    // e o do chefe (mechaLaser). Depois de um disparo o escudo baixa (instancia 0x1D8, anim 0) e ele fica kShieldDownFields quadros
    // sem escudo (dano cheio, sem laser); depois sobe (anim 1) e volta ao brilho em laco (anim 2).
    constexpr uint32_t kMonsterUpdate = wotm::fn::update__7Monster, kTakeDamage = wotm::fn::takeDamage__7MonsterfbP7Monster;
    constexpr uint32_t kGetHandle = wotm::fn::animationGetHandle__FP11_animHandleUiUiUi, kAnimStart = wotm::fn::animationStart__FG11_animHandleb,
                       kAnimTrans = wotm::fn::animationTransitionInto__FR11_animHandlefii, kAnimPercent = wotm::fn::animationSetToPercent__FG11_animHandlefi;
    constexpr uint32_t kBlendInit = wotm::fn::init__9AnimBlendR11_animHandle, kBlendRampIn = wotm::fn::rampIn__9AnimBlendf,
                       kBlendSpeed = wotm::fn::setSpeed__9AnimBlendf, kBlendPercent = wotm::fn::setPercent__9AnimBlendf;
    constexpr uint32_t kLos = wotm::fn::hdLosCSTest__FP8_fvectorT0P9_hdResulti, kHitCreate = wotm::fn::create__8HitEvent,
                       kDispatchHit = wotm::fn::dispatchHit__12InteractivesP8_fvectorfii, kGetInteractive = wotm::fn::getInteractive__12Interactivesi,
                       kCreditHit = wotm::fn::creditHit__11HurtHistoryif, kFx = wotm::fn::particleCreateFx__FP8_fvectorifPA3_ff;
    constexpr uint32_t kDupId = 0x1C, kSpecial = 0xF3, kTazerPhase = 0x40, kTazerFiring = 0xA1, kTazerTimer = 0x2C;
    constexpr uint32_t kLocMuzzle = 0x3E50, kLocChest = 0x3E30, kCamView = 0x6CD8, kHeading = 0x68, kHealth = 0x44C;
    constexpr uint32_t kCamCs = 0xCF0;   // Camera::m_cs (kCameras/kCamStride: setPov)
    constexpr uint32_t kShieldCi = kJelly + 0x18, kLaserCi = kJelly + 0x19;   // ids das instancias no MECHA.NGP
    constexpr uint32_t kTurretYaw = 290, kTurretPitch = 291;   // anims da torre na instancia principal (as 11/12 do chefe, m_blend1/2)
    // Memoria do Mecha no topo da RAM (abaixo dos kQuadDefs do split4): 2 Mechas x 0x1C0 e um rascunho de 0x80 para as chamadas.
    // Por Mecha: 6 _animHandle (escudo 0 baixa, 1 sobe, 2 brilho; laser; torre giro e inclinacao), 2 AnimBlend (0x68) e um
    // HurtHistory (0x84) para os predios
    constexpr int kMaxMechas = 2;
    constexpr uint32_t kMechaMem = PS2_RAM_SIZE - 0x1400, kMechaStride = 0x1C0, kScratch = kMechaMem + kMaxMechas * kMechaStride;
    constexpr uint32_t kSFrom = kScratch, kSTo = kScratch + 0x10, kSRes = kScratch + 0x20, kSDir = kScratch + 0x50, kSPt = kScratch + 0x60,
                       kSFx = kScratch + 0x70;
    constexpr uint32_t kShieldDownFields = 240, kShieldRaiseFields = 30;   // FinalBoss::updateMecha: estado 7; anim 1 do escudo
    constexpr float kShieldDamage = 0.25f, kLaserLen = 4000.f, kMechaReach = 2500.f;
    constexpr float kLaserDamage = 0.4f, kLaserPush = 6.f, kBuildingCredit = 0.3f;   // por quadro (o chefe: 0,5 / 5 / 0,16)
    constexpr float kPi = 3.14159265f;
    PS2Runtime::RecompiledFunction g_origMonsterUpdate = nullptr, g_origTakeDamage = nullptr;
    // Sons do Mecha: os do chefe (FinalBossSound, banco Final_s.bnk, que o Mecha ja carrega como o Jelly). O laco do laser (som 5)
    // segue a boca; o monstro atingido toca o laco do impacto sozinho com MonsterSound +0x8 (Monster +0x1A84) = 1 no quadro, como o
    // chefe faz, e o desligamos no fim da rajada (o desligamento por tempo do jogo depende do contador de fields, parado no port)
    constexpr uint32_t kSndReady = wotm::fn::playGetReadyToFireSound__14FinalBossSoundP8_fvector,
                       kSndShieldUp = wotm::fn::playShieldPowerUpSound__14FinalBossSoundP8_fvector,
                       kSndShieldDown = wotm::fn::playShieldPowerDownSound__14FinalBossSoundP8_fvector,
                       kSndLaser = wotm::fn::updateMechLaser__14FinalBossSoundP8_fvector, kLoopStop = wotm::fn::terminateLoop__11LoopManagerPUi,
                       kImpactStop = wotm::fn::stopMechaLaserImpactSound__12MonsterSound, kLoopManager = wotm::sym::gLoopManager;
    constexpr uint32_t kMonSound = 0x1A7C, kLaserHitFlag = 0x1A84;
    // WOTM_MECHATEST=1: teste sem controle; o Mecha do jogador dispara sempre que pode e a mira da camera da torre varre sozinha
    bool mechaTest()
    {
        static const bool on = [] { const char *e = std::getenv("WOTM_MECHATEST"); return e && *e && *e != '0'; }();
        return on;
    }
    struct Mecha
    {
        uint32_t m = 0, ep = 0;         // monstro e o no raiz dele (outro = outra carga: pega os handles de novo)
        bool down = false, fired = false, wasTazer = false;
        uint32_t tick = 0, until = 0, raise = 0;  // quadros (chamadas do Monster::update) dele; quando o escudo sobe / termina de subir
        float yaw = 0.5f, pitch = 0.5f;           // percentuais dos blends da torre (0,5 = para a frente)
        float aimHead = 0.f, aimPitch = 0.f;      // mira do jogador durante o laser (camera da torre)
        float camW = 0.f;                         // peso da camera da torre (0 = a do jogo, 1 = a da torre)
        bool aimInit = false, gunLogged = false;
        float gunR = 0.f;                         // raio da boca em volta do eixo do corpo (camera da torre)
        uint32_t lastTarget = 0;                  // ultimo monstro atingido (para desligar o laco do impacto)
        uint32_t hitMon = 0, hitObj = 0;          // quadros da rajada no monstro e em objetos (log)
        float dealt = 0.f;                        // vida tirada do monstro na rajada (log)
    };
    Mecha g_mechas[kMaxMechas];

    bool isMecha(uint8_t *rdram, uint32_t m)
    {
        if (!m || cRd32(rdram, m + kTypeBits) != kJelly)
            return false;
        const wotm::mods::Custom *cu = customOf(rdram, m);
        return cu && cu->kind == "mecha";
    }
    uint32_t mechaBase(int slot) { return kMechaMem + kMechaStride * uint32_t(slot); }
    uint32_t handle(int slot, int k) { return mechaBase(slot) + 0x10u * uint32_t(k); }
    uint32_t blend(int slot, int j) { return mechaBase(slot) + 0x60u + 0x68u * uint32_t(j); }
    uint32_t hurt(int slot) { return mechaBase(slot) + 0x130u; }
    // FinalBossSound do Mecha (o this; so o +4, m_laserHandle, e usado pelo updateMechLaser) nos 12 bytes depois do HurtHistory
    uint32_t sndObj(int slot) { return mechaBase(slot) + 0x1B4u; }
    // a RAM do runtime tem 64 MB (PS2_RAM_SIZE): a mascara de 32 MB do cRd32 jogaria a area do Mecha no meio dos dados da fase
    uint8_t *mem(uint8_t *rdram, uint32_t a) { return rdram + (a & (PS2_RAM_SIZE - 1u)); }
    uint32_t rdW(uint8_t *rdram, uint32_t a) { uint32_t v; std::memcpy(&v, mem(rdram, a), 4); return v; }
    float rdF(uint8_t *rdram, uint32_t a) { float v; std::memcpy(&v, mem(rdram, a), 4); return v; }
    void wrF(uint8_t *rdram, uint32_t a, float v) { std::memcpy(mem(rdram, a), &v, 4); }
    void rdV(uint8_t *rdram, uint32_t a, float *v) { std::memcpy(v, mem(rdram, a), 12); }
    void wrV(uint8_t *rdram, uint32_t a, const float *v)
    {
        const float w[4] = {v[0], v[1], v[2], 0.f};
        std::memcpy(mem(rdram, a), w, 16);
    }
    void wrW(uint8_t *rdram, uint32_t a, uint32_t v) { std::memcpy(mem(rdram, a), &v, 4); }
    float frand(float lo, float hi) { return lo + (hi - lo) * float(std::rand()) / float(RAND_MAX); }

    // Funcao do jogo com floats em $f12/$f13; devolve $f0 (e $v0 em *v0)
    float callF(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t addr, std::initializer_list<uint32_t> args, float f12 = 0.f,
                float f13 = 0.f, uint32_t *v0 = nullptr)
    {
        const PS2Runtime::RecompiledFunction f = runtime->lookupFunction(addr);
        if (!f)
            return 0.f;
        const R5900Context saved = *ctx;
        int r = 4;
        for (uint32_t a : args)
            ctx->r[r++] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(a)));
        ctx->f[12] = f12;
        ctx->f[13] = f13;
        ctx->r[31] = _mm_set_epi64x(0, 0);
        ctx->pc = addr;
        f(rdram, ctx, runtime);
        wotmFinishGuestCall(rdram, ctx, runtime);
        const float f0 = ctx->f[0];
        if (v0)
            *v0 = GPR_U32(ctx, 2);
        *ctx = saved;
        return f0;
    }
    // Vaga do Mecha `m` (pegando os handles e montando os blends da torre na carga atual); -1 se `m` nao e um Mecha
    int mechaSlot(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m)
    {
        if (!isMecha(rdram, m))
            return -1;
        const uint32_t cs = cRd32(rdram, m + kCs) & 0x1FFFFFFFu, ep = cs ? cRd32(rdram, cs) & 0x1FFFFFFFu : 0;
        int slot = -1;
        for (int i = 0; i < kMaxMechas; ++i)
            if (g_mechas[i].m == m && g_mechas[i].ep == ep)
                return i;
        for (int i = 0; i < kMaxMechas && slot < 0; ++i)
            if (g_mechas[i].m == m || !isMecha(rdram, g_mechas[i].m))
                slot = i;
        if (slot < 0)
            return -1;
        Mecha &s = g_mechas[slot];
        s = Mecha{};
        s.m = m;
        s.ep = ep;
        std::memset(mem(rdram, mechaBase(slot)), 0, kMechaStride);
        const uint32_t dup = cRd32(rdram, m + kDupId);
        for (int k = 0; k < 3; ++k)
            wotm_modes4::call(rdram, ctx, runtime, kGetHandle, {handle(slot, k), kShieldCi, dup, uint32_t(k)});
        wotm_modes4::call(rdram, ctx, runtime, kGetHandle, {handle(slot, 3), kLaserCi, dup, 0u});
        wotm_modes4::call(rdram, ctx, runtime, kGetHandle, {handle(slot, 4), kJelly, dup, kTurretYaw});
        wotm_modes4::call(rdram, ctx, runtime, kGetHandle, {handle(slot, 5), kJelly, dup, kTurretPitch});
        const bool ok = rdW(rdram, handle(slot, 1)) && rdW(rdram, handle(slot, 3)) && rdW(rdram, handle(slot, 4));
        std::fprintf(stderr, "[custom] Mecha (dup %u): escudo, laser e torre %s\n", dup, ok ? "achados" : "FALTANDO (MECHA.NGP antigo?)");
        for (int k = 0; k < 6 && !ok; ++k)
        {
            int16_t idx;
            std::memcpy(&idx, mem(rdram, handle(slot, k) + 0xC), 2);
            std::fprintf(stderr, "[custom] Mecha: handle %d ci %08X anim %d\n", k, rdW(rdram, handle(slot, k)), idx);
        }
        wotm_modes4::call(rdram, ctx, runtime, kAnimStart, {handle(slot, 1), 1u});   // o escudo sobe na entrada, como no initAfter
        s.raise = s.tick + kShieldRaiseFields;
        callF(rdram, ctx, runtime, kAnimPercent, {handle(slot, 3), 1u}, 0.f);
        // torre: os dois blends do FinalBoss::initAfter (modificador, prioridade 999, parados no meio)
        for (int j = 0; j < 2; ++j)
        {
            const uint32_t b = blend(slot, j);
            mem(rdram, b)[0xD] = 4;
            const int16_t prio = 999;
            std::memcpy(mem(rdram, b + 0xE), &prio, 2);
            const float w = 1.f;
            std::memcpy(mem(rdram, b + 0x64), &w, 4);
            wotm_modes4::call(rdram, ctx, runtime, kBlendInit, {b, handle(slot, 4 + j)});
            callF(rdram, ctx, runtime, kBlendRampIn, {b}, 200.f);
            callF(rdram, ctx, runtime, kBlendSpeed, {b}, 0.f);
            callF(rdram, ctx, runtime, kBlendPercent, {b}, 0.5f);
        }
        return slot;
    }

    // Um som de posicao do FinalBossSound (fn) no ponto p
    void mechaSound(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int slot, uint32_t fn, const float *p)
    {
        wrV(rdram, kSFx, p);
        wotm_modes4::call(rdram, ctx, runtime, fn, {sndObj(slot), kSFx});
    }
    void mechaSoundAtBody(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int slot, uint32_t fn)
    {
        float p[3];
        rdV(rdram, (cRd32(rdram, g_mechas[slot].m + kCs) & 0x1FFFFFFFu) + kTrans, p);
        mechaSound(rdram, ctx, runtime, slot, fn, p);
    }
    // Fim da rajada: o laco do laser e o do impacto no ultimo atingido
    void stopLaserSound(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int slot)
    {
        Mecha &s = g_mechas[slot];
        if (rdW(rdram, sndObj(slot) + 4))
            wotm_modes4::call(rdram, ctx, runtime, kLoopStop, {kLoopManager, sndObj(slot) + 4});
        if (s.lastTarget)
            wotm_modes4::call(rdram, ctx, runtime, kImpactStop, {s.lastTarget + kMonSound});
        s.lastTarget = 0;
    }

    void monsterUpdate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = ctx->pc == kMonsterUpdate ? GPR_U32(ctx, 4) : 0;
        g_origMonsterUpdate(rdram, ctx, runtime);
        const int slot = m ? mechaSlot(rdram, ctx, runtime, m) : -1;
        if (slot < 0)
            return;
        Mecha &s = g_mechas[slot];
        const uint32_t now = ++s.tick;
        const bool inTazer = cRd32(rdram, m + kCurState) == m + kTazer;
        if (!inTazer && (s.yaw != 0.5f || s.pitch != 0.5f))   // fora do disparo a torre volta para a frente
        {
            s.yaw += (0.5f - s.yaw) * 0.15f;
            s.pitch += (0.5f - s.pitch) * 0.15f;
            if (std::fabs(s.yaw - 0.5f) < 0.002f && std::fabs(s.pitch - 0.5f) < 0.002f)
                s.yaw = s.pitch = 0.5f;
            callF(rdram, ctx, runtime, kBlendPercent, {blend(slot, 0)}, s.yaw);
            callF(rdram, ctx, runtime, kBlendPercent, {blend(slot, 1)}, s.pitch);
        }
        if (!s.wasTazer && inTazer)
            mechaSoundAtBody(rdram, ctx, runtime, slot, kSndReady);
        if (s.wasTazer && !inTazer)
            stopLaserSound(rdram, ctx, runtime, slot);
        if (s.wasTazer && !inTazer && s.fired)
        {
            mechaSoundAtBody(rdram, ctx, runtime, slot, kSndShieldDown);
            callF(rdram, ctx, runtime, kAnimTrans, {handle(slot, 0), 1u, 1u}, 50.f);
            callF(rdram, ctx, runtime, kAnimPercent, {handle(slot, 3), 1u}, 0.f);
            s.down = true;
            s.fired = false;
            s.raise = 0;
            s.until = now + kShieldDownFields;
            std::fprintf(stderr, "[custom] Mecha: escudo baixou (%u quadros com dano cheio); rajada: %u quadros no monstro (-%.1f de vida), %u em objetos\n",
                         kShieldDownFields, s.hitMon, s.dealt, s.hitObj);
            s.hitMon = s.hitObj = 0;
            s.dealt = 0.f;
        }
        if (s.down && int32_t(now - s.until) >= 0)
        {
            wotm_modes4::call(rdram, ctx, runtime, kAnimStart, {handle(slot, 1), 1u});
            mechaSoundAtBody(rdram, ctx, runtime, slot, kSndShieldUp);
            s.down = false;
            s.raise = now + kShieldRaiseFields;
            std::fprintf(stderr, "[custom] Mecha: escudo religou\n");
        }
        if (s.raise && int32_t(now - s.raise) >= 0)
        {
            callF(rdram, ctx, runtime, kAnimTrans, {handle(slot, 2), 1u, 1u}, 50.f);
            s.raise = 0;
        }
        s.wasTazer = inTazer;
    }

    // Monster::takeDamage(float dmg, bool, Monster *src): com o escudo ligado o Mecha toma kShieldDamage do dano
    void takeDamage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = ctx->pc == kTakeDamage ? GPR_U32(ctx, 4) : 0;
        const int slot = m && ctx->f[12] > 0.f ? mechaSlot(rdram, ctx, runtime, m) : -1;
        if (slot >= 0 && !g_mechas[slot].down)
            ctx->f[12] *= kShieldDamage;
        g_origTakeDamage(rdram, ctx, runtime);
    }

    // Para onde o Mecha mira: jogador humano, o centro da tela (o raio da camera dele, pulando o proprio Mecha, ate o primeiro
    // obstaculo); IA (ou sem camera), o peito (0x809) do inimigo mais perto; sem ninguem, em frente
    void mechaAim(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m, const float *mouth, float *aim)
    {
        const uint32_t view = cRd32(rdram, m + kCamView), id = cRd32(rdram, m + kId);
        if (cRd32(rdram, m + kPlayerNum) != 2u && isPlayerSlot(rdram, ctx, runtime, m) && view < 4u)
        {
            const uint32_t cs = cRd32(rdram, kCameras + kCamStride * view + kCamCs) & 0x1FFFFFFFu;
            if (cs >= 0x1000u && cs < PS2_RAM_SIZE)
            {
                float pos[3], pitch, head;
                rdV(rdram, cs + kTrans, pos);
                std::memcpy(&pitch, mem(rdram, cs + 0x60), 4);
                std::memcpy(&head, mem(rdram, cs + 0x68), 4);
                const float dir[3] = {std::sin(head) * std::cos(pitch), std::cos(head) * std::cos(pitch), std::sin(pitch)};
                // comeca na altura do Mecha (a distancia da camera ate a boca, ao longo da vista) para nao pegar o que esta atras
                const float skip = std::max(0.f, (mouth[0] - pos[0]) * dir[0] + (mouth[1] - pos[1]) * dir[1] + (mouth[2] - pos[2]) * dir[2]);
                float from[3], to[3];
                for (int i = 0; i < 3; ++i)
                    from[i] = pos[i] + dir[i] * skip, to[i] = pos[i] + dir[i] * (skip + 2.f * kLaserLen);
                wrV(rdram, kSFrom, from);
                wrV(rdram, kSTo, to);
                std::memset(mem(rdram, kSRes), 0, 0x30);
                callF(rdram, ctx, runtime, kLos, {kSFrom, kSTo, kSRes, id});
                if (mem(rdram, kSRes)[0])
                    rdV(rdram, kSRes + 0x10, aim);
                else
                    std::memcpy(aim, to, 12);
                return;
            }
        }
        const uint32_t t = nearestEnemy(rdram, ctx, runtime, m, 1e9f, nullptr);
        if (t)
        {
            rdV(rdram, t + kLocChest, aim);
            return;
        }
        float head;
        std::memcpy(&head, mem(rdram, (cRd32(rdram, m + kCs) & 0x1FFFFFFFu) + kHeading), 4);
        aim[0] = mouth[0] + std::sin(head) * kLaserLen;
        aim[1] = mouth[1] + std::cos(head) * kLaserLen;
        aim[2] = mouth[2] - 400.f;
    }

    // Um quadro do laser (fase de disparo): a torre gira para a mira (blends 290/291, como o m_blend1/m_blend2 do chefe), um raio
    // da boca (0x80B) ate 4000 na direcao da mira; o monstro atingido toma dano (HitEvent tipo 0 pelo dispatchHit, como o fogo:
    // credita o Mecha e respeita a defesa) e e empurrado; predio e objeto acumulam no HurtHistory e levam 5,01 a cada 5 (como o
    // chefe); faiscas no ponto (fx 0x58/0x21/0x1F, 1 em 8 cada); a instancia do laser estica ate o ponto
    void mechaLaser(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int slot, uint32_t m, bool first)
    {
        Mecha &s = g_mechas[slot];
        s.fired = true;
        const uint32_t id = cRd32(rdram, m + kId), mcs = cRd32(rdram, m + kCs) & 0x1FFFFFFFu;
        if (first)
            std::memset(mem(rdram, hurt(slot)), 0, 0x84);
        float mouth[3], aim[3], body;
        rdV(rdram, m + kLocMuzzle, mouth);
        mechaAim(rdram, ctx, runtime, m, mouth, aim);
        std::memcpy(&body, mem(rdram, mcs + kHeading), 4);
        float dir[3] = {aim[0] - mouth[0], aim[1] - mouth[1], aim[2] - mouth[2]};
        const float horiz = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1]), d = std::sqrt(horiz * horiz + dir[2] * dir[2]);
        if (d < 1.f)
            return;
        for (float &c : dir)
            c /= d;
        // torre: giro relativo ao corpo (0,5 = em frente, 1 volta = 1) e inclinacao (0,5 - pitch/pi, a conta do chefe)
        float rel = std::atan2(dir[0], dir[1]) - body;
        while (rel > kPi) rel -= 2.f * kPi;
        while (rel < -kPi) rel += 2.f * kPi;
        const float yaw = rel / (2.f * kPi) + 0.5f, pitch = 0.5f - std::atan2(dir[2], horiz / d) / kPi;
        s.yaw += (yaw - s.yaw) * 0.6f;
        s.pitch += (pitch - s.pitch) * 0.6f;
        callF(rdram, ctx, runtime, kBlendPercent, {blend(slot, 0)}, s.yaw);
        callF(rdram, ctx, runtime, kBlendPercent, {blend(slot, 1)}, s.pitch);

        float to[3];
        for (int i = 0; i < 3; ++i)
            to[i] = mouth[i] + dir[i] * kLaserLen;
        wrV(rdram, kSFrom, mouth);
        wrV(rdram, kSTo, to);
        std::memset(mem(rdram, kSRes), 0, 0x30);
        callF(rdram, ctx, runtime, kLos, {kSFrom, kSTo, kSRes, id});
        float len = 1.f;
        if (mem(rdram, kSRes)[0])
        {
            float pt[3];
            rdV(rdram, kSRes + 0x10, pt);
            len = std::sqrt((pt[0] - mouth[0]) * (pt[0] - mouth[0]) + (pt[1] - mouth[1]) * (pt[1] - mouth[1]) + (pt[2] - mouth[2]) * (pt[2] - mouth[2])) / kLaserLen;
            wrV(rdram, kSPt, pt);
            const uint32_t ids = rdW(rdram, kSRes + 4), id2 = ids & 0x7FFu, id1 = (ids >> 11) & 0x3FFFu;
            // monstro atingido: o do slot com m_id == id2 (o chefe compara o id2 com o m_id do jogador)
            uint32_t inter = 0;
            const uint32_t slots = std::min(16u, READ32(kGameObj + kNumSlots));
            for (uint32_t i = 0; id2 && i < slots && !inter; ++i)
            {
                const uint32_t o = kGameObj + kSlots + i * kSlotStride;
                if (o != m && READ32(o + kCs) && (READ32(o + kId) & 0x7FFu) == id2)
                    inter = o;
            }
            if (inter)
            {
                // como o chefe: takeDamage direto (sem HitEvent nem recuo), creditado ao Mecha, e o empurrao
                if (!READ8(inter + kDead))
                {
                    float hp0, hp1;
                    std::memcpy(&hp0, mem(rdram, inter + kHealth), 4);
                    callF(rdram, ctx, runtime, kTakeDamage, {inter, 0u, m}, kLaserDamage);
                    std::memcpy(&hp1, mem(rdram, inter + kHealth), 4);
                    s.dealt += hp0 - hp1;
                }
                ++s.hitMon;
                wrW(rdram, inter + kLaserHitFlag, 1);   // o laco do impacto (MonsterSound::updateGeneralMonsterSounds)
                if (s.lastTarget && s.lastTarget != inter)
                    wotm_modes4::call(rdram, ctx, runtime, kImpactStop, {s.lastTarget + kMonSound});
                s.lastTarget = inter;
                const uint32_t tcs = cRd32(rdram, inter + kCs) & 0x1FFFFFFFu;
                if (tcs && horiz > 0.01f)   // o empurrao do chefe: no chao, na direcao do laser
                {
                    float tp[3];
                    rdV(rdram, tcs + kTrans, tp);
                    tp[0] += dir[0] / (horiz / d) * kLaserPush;
                    tp[1] += dir[1] / (horiz / d) * kLaserPush;
                    std::memcpy(mem(rdram, tcs + kTrans), tp, 8);
                }
            }
            else if (id2)
            {
                ++s.hitObj;
                const float total = callF(rdram, ctx, runtime, kCreditHit, {hurt(slot), id2}, kBuildingCredit);
                if (total > 5.f)
                {
                    callF(rdram, ctx, runtime, kCreditHit, {hurt(slot), id2}, -5.f);
                    uint32_t ev = 0;
                    callF(rdram, ctx, runtime, kHitCreate, {}, 0.f, 0.f, &ev);
                    if (ev)
                        wrW(rdram, ev, kSPt);
                    float mp[3];
                    rdV(rdram, mcs + kTrans, mp);
                    const float v[3] = {pt[0] - mp[0], pt[1] - mp[1], pt[2] - mp[2]};
                    wrV(rdram, kSDir, v);
                    callF(rdram, ctx, runtime, kDispatchHit, {kSDir, id, id2}, 5.01f);
                }
            }
            static const uint32_t kSparks[3] = {0x58, 0x21, 0x1F};
            for (uint32_t fx : kSparks)
                if (std::rand() % 8 == 0)
                {
                    const float p[3] = {pt[0] + frand(-35.f, 35.f), pt[1] + frand(-35.f, 35.f), pt[2] + frand(-35.f, 35.f)};
                    wrV(rdram, kSFx, p);
                    callF(rdram, ctx, runtime, kFx, {kSFx, fx, 0u}, frand(1.f, 5.f), 0.f);
                }
        }
        callF(rdram, ctx, runtime, kAnimPercent, {handle(slot, 3), 1u}, std::clamp(len, 0.01f, 1.f));
        mechaSound(rdram, ctx, runtime, slot, kSndLaser, mouth);   // o laco do laser (comeca no 1o quadro, depois so volume e pan)
        if (first)
            std::fprintf(stderr, "[custom] Mecha laser: boca (%.0f %.0f %.0f) mira (%.0f %.0f %.0f) hit %u ids %08X len %.2f yaw %.2f pitch %.2f\n",
                         mouth[0], mouth[1], mouth[2], aim[0], aim[1], aim[2], mem(rdram, kSRes)[0], rdW(rdram, kSRes + 4), len, s.yaw, s.pitch);
    }

    // Quem tem o choque como golpe de jogador: o Jelly e o custom com "special": "jelly" ou "final" (o Cerebulon segue o retail)
    bool tazerOwner(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m)
    {
        const uint32_t type = READ32(m + kTypeBits);
        return type == kJelly || (type != kFinal && READ32(m + kSpecialState) == m + kTazer);
    }
    void tazerOK(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = ctx->pc == kTazerOK ? READ32(GPR_U32(ctx, 4) + kStateOwner) : 0;
        int player = -1;
        for (uint32_t i = 0; m && i < 4 && player < 0; ++i)
            if (READ32(kGameObj + kMonsters + 4 * i) == m)
                player = int(i);
        const int mecha = m ? mechaSlot(rdram, ctx, runtime, m) : -1;
        if (mecha >= 0)
        {
            if (g_mechas[mecha].down)   // sem escudo nao atira (religa primeiro)
            {
                ret(ctx, 0);
                return;
            }
            WRITE8(m + kSpecial, 1);   // o laser e o unico ataque dele: sempre disponivel
            if (player >= 0 && READ32(m + kPlayerNum) != 2u)   // jogador: Triangulo+Circulo, sem precisar de alvo (mira pela camera)
            {
                const uint32_t b = wotm_modes4::call(rdram, ctx, runtime, kPadButtons, {uint32_t(player)});
                if ((b & (kBtnTriangle | kBtnCircle)) != (kBtnTriangle | kBtnCircle) && !mechaTest())
                {
                    ret(ctx, 0);
                    return;
                }
                g_origTazerOK(rdram, ctx, runtime);
                return;
            }
        }
        const float reach = mecha >= 0 ? kMechaReach : kTazerReach;
        if (player >= 0 && tazerOwner(rdram, ctx, runtime, m) && READ32(m + kPlayerNum) != 2u)
        {
            const uint32_t b = wotm_modes4::call(rdram, ctx, runtime, kPadButtons, {uint32_t(player)});
            float d = 0.f;
            const uint32_t target = (b & (kBtnTriangle | kBtnCircle)) == (kBtnTriangle | kBtnCircle) ? nearestEnemy(rdram, ctx, runtime, m, reach, &d) : 0;
            if (!target)
            {
                const bool pressed = (b & (kBtnTriangle | kBtnCircle)) == (kBtnTriangle | kBtnCircle);
                if (pressed && !g_tazerHeld[player])   // um aviso por aperto (o transitionOK roda a cada quadro)
                {
                    float far = 0.f;
                    nearestEnemy(rdram, ctx, runtime, m, 1e6f, &far);
                    std::fprintf(stderr, "[custom] %s sem inimigo a %.0f (o mais perto a %.0f)\n", mecha >= 0 ? "Mecha: laser" : "Jelly: choque", reach, far);
                }
                g_tazerHeld[player] = pressed;
                ret(ctx, 0);
                return;
            }
            g_origTazerOK(rdram, ctx, runtime);
            if (GPR_U32(ctx, 2))
                std::fprintf(stderr, "[custom] Jelly: alvo do choque tipo %u a %.0f\n", READ32(target + kTypeBits) >> 5, d);
            return;
        }
        g_origTazerOK(rdram, ctx, runtime);
    }

    // Alvo do choque: o StateUltraTazer::update mira game->m_monsters[0] (na fase final, o jogador), sem limite de distancia:
    // raios dos locators do Jelly aos dele, empurrao e golpe (dispatchHit). Com o Jelly jogador o alvo era ele mesmo. Durante a
    // chamada, m_monsters[0] vale o inimigo vivo mais perto (qualquer slot ativo, jogador ou IA); sem ninguem a kTazerKeep, vale
    // o proprio Jelly com m_dead ligado, e o update sai para o Idle como faz com o alvo morto.
    void tazerUpdate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t state = GPR_U32(ctx, 4);
        const uint32_t m = ctx->pc == kTazerUpdate ? READ32(state + kStateOwner) : 0;
        const int mecha = m ? mechaSlot(rdram, ctx, runtime, m) : -1;
        // Mecha na fase de disparo: o laser e nosso; o original (raios do choque) so entra no ultimo quadro, para passar a fase de
        // desligar (o +0x2C cai 1,0 por quadro e com <= 0 vai para 0xA2)
        if (mecha >= 0 && READ32(state + kTazerPhase) == kTazerFiring)
        {
            float left;
            std::memcpy(&left, mem(rdram, state + kTazerTimer), 4);
            mechaLaser(rdram, ctx, runtime, mecha, m, !g_mechas[mecha].wasTazer || !g_mechas[mecha].fired);
            if (left > 1.5f)
            {
                left -= 1.f;
                std::memcpy(mem(rdram, state + kTazerTimer), &left, 4);
                return;
            }
            callF(rdram, ctx, runtime, kAnimPercent, {handle(mecha, 3), 1u}, 0.f);
            stopLaserSound(rdram, ctx, runtime, mecha);   // a rajada acabou (vai para a fase de desligar)
        }
        // O Mecha mira o inimigo mais perto sempre (o retail mira m_monsters[0], que pode ser ele mesmo)
        if (!m || !tazerOwner(rdram, ctx, runtime, m) || ((!isPlayerSlot(rdram, ctx, runtime, m) || READ32(m + kPlayerNum) == 2u) && mecha < 0))
        {
            g_origTazerUpdate(rdram, ctx, runtime);
            return;
        }
        const float keep = mecha >= 0 ? 1e9f : kTazerKeep;
        const uint32_t target = nearestEnemy(rdram, ctx, runtime, m, keep, nullptr), p1 = READ32(kGameObj + kMonsters);
        const uint8_t dead = READ8(m + kDead);
        WRITE32(kGameObj + kMonsters, target ? target : m);
        if (!target)
            WRITE8(m + kDead, 1);
        g_origTazerUpdate(rdram, ctx, runtime);
        WRITE32(kGameObj + kMonsters, p1);
        if (!target)
            WRITE8(m + kDead, dead);
    }
    // Camera da torre: com o Mecha de um jogador no laser (StateUltraTazer, da preparacao ao fim), a vista dele vai para a frente da
    // ponta do canhao (eixo do corpo + raio da boca + kLaserCamAhead, na direcao da mira) e um pouco acima da boca, olhando para a mira:
    // o corpo fica atras da camera e o feixe sai logo abaixo dela; o centro da tela e para onde o feixe vai (mechaAim). Os dois
    // analogicos e o mouse giram a mira (sensibilidade e eixo invertido da aba CAMERA) e chegam zerados ao jogo e a orbita. Entra e sai
    // em ~12 quadros.
    constexpr float kLaserCamAhead = 0.f, kLaserCamUp = 220.f, kLaserCamRate = 1.f / 12.f;
    constexpr float kAimYawRate = 0.03f, kAimPitchRate = 0.022f, kAimPitchMin = -1.1f, kAimPitchMax = 0.35f;

    int mechaOf(uint8_t *rdram, uint32_t m)
    {
        for (int i = 0; i < kMaxMechas; ++i)
            if (m && g_mechas[i].m == m && isMecha(rdram, m))
                return i;
        return -1;
    }
    int playerIndex(uint8_t *rdram, uint32_t m)
    {
        for (uint32_t i = 0; m && i < 4; ++i)
            if (rdW(rdram, kGameObj + kMonsters + 4 * i) == m)
                return int(i);
        return -1;
    }
    bool laserAiming(uint8_t *rdram, uint32_t m)
    {
        return rdW(rdram, m + kCurState) == m + kTazer && rdW(rdram, m + kPlayerNum) != 2u && playerIndex(rdram, m) >= 0;
    }

    // Entrada do PadFlags::interpretInputs (gp = GamePad): no laser, analogicos (0xC..0xF esquerdo, 0x10..0x13 direito) e mouse
    // viram mira
    void laserInput(uint8_t *rdram, uint32_t owner, uint32_t gp)
    {
        const uint32_t m = owner & 0x1FFFFFFFu;
        const int i = mechaOf(rdram, m);
        if (i < 0 || !laserAiming(rdram, m) || gp < 0x1000u || gp + 0x14u > PS2_RAM_SIZE)
            return;
        Mecha &s = g_mechas[i];
        uint8_t *b = rdram + gp;
        auto axis = [](float x, float y, float &ox, float &oy) {
            const float mag = std::sqrt(x * x + y * y), k = mag > 0.2f ? std::min(1.f, (mag - 0.2f) / 0.8f) / mag : 0.f;
            ox = x * k;
            oy = y * k;
        };
        float lx, ly, rx, ry;
        axis((float(b[0xD]) - float(b[0xC])) / 255.f, (float(b[0xE]) - float(b[0xF])) / 255.f, lx, ly);
        axis((float(b[0x11]) - float(b[0x10])) / 255.f, (float(b[0x12]) - float(b[0x13])) / 255.f, rx, ry);
        std::memset(b + 0xC, 0, 8);
        float sx = std::clamp(lx + rx, -1.f, 1.f), sy = std::clamp(ly + ry, -1.f, 1.f);
        if (mechaTest())
        {
            static uint32_t t = 0;
            ++t;
            sx = 0.7f * std::sin(float(t) * 0.02f);
            sy = 0.4f * std::sin(float(t) * 0.013f);
        }
        const float sens = float(std::clamp(wotm::settings::get(wotm::settings::CamSens), 1, 10)) * 0.2f;
        const float inv = wotm::settings::on(wotm::settings::CamInvertY) ? -1.f : 1.f;
        s.aimHead += sx * kAimYawRate * sens;
        s.aimPitch += sy * kAimPitchRate * sens * inv;
        if (playerIndex(rdram, m) == wotm::mouse::g_kbPlayer.load(std::memory_order_relaxed))
        {
            float mx = 0.f, my = 0.f;
            if (rdW(rdram, wotm::sym::isPaused) == 0u)
                wotm::mouse::want();
            wotm::mouse::take(mx, my);
            s.aimHead += mx * 0.0025f * sens;
            s.aimPitch -= my * 0.0025f * sens * inv;   // a orbita: para cima (my < 0) olha para cima
        }
        while (s.aimHead > kPi) s.aimHead -= 2.f * kPi;
        while (s.aimHead < -kPi) s.aimHead += 2.f * kPi;
        s.aimPitch = std::clamp(s.aimPitch, kAimPitchMin, kAimPitchMax);
    }

    // Depois do Cameras::Update (e da orbita): a pose da camera da torre, misturada com a do jogo pelo camW
    void laserCam(uint8_t *rdram)
    {
        float reticle = 0.f;
        struct Publish   // a retícula do quadro, em qualquer saida
        {
            float &r;
            ~Publish() { g_wotmMechaReticle.store(r, std::memory_order_relaxed); }
        } publish{reticle};
        for (int i = 0; i < kMaxMechas; ++i)
        {
            Mecha &s = g_mechas[i];
            const uint32_t m = s.m;
            if (!m || !isMecha(rdram, m))
                continue;
            const uint32_t view = rdW(rdram, m + kCamView), mcs = rdW(rdram, m + kCs) & 0x1FFFFFFFu;
            const uint32_t c = kCameras + kCamStride * view, cs = view < 4u ? rdW(rdram, c + kCamCs) & 0x1FFFFFFFu : 0u;
            const bool on = laserAiming(rdram, m) && cs >= 0x1000u && mcs >= 0x1000u && (rdW(rdram, c + kCamMonster) & 0x1FFFFFFFu) == m;
            if (on && !s.aimInit)   // comeca olhando para onde a camera do jogo olhava, um pouco para baixo
            {
                const float fx = rdF(rdram, cs + 0x30), fy = rdF(rdram, cs + 0x34), fz = rdF(rdram, cs + 0x38);
                s.aimHead = std::atan2(fx, fy);
                s.aimPitch = std::clamp(std::atan2(fz, std::sqrt(fx * fx + fy * fy)) - 0.1f, kAimPitchMin, kAimPitchMax);
                s.aimInit = true;
                std::fprintf(stderr, "[custom] Mecha: camera da torre (mira %.0f graus, %.0f para baixo)\n", s.aimHead * 57.3f, -s.aimPitch * 57.3f);
            }
            if (!on)
                s.aimInit = false;
            s.camW = std::clamp(s.camW + (on ? kLaserCamRate : -kLaserCamRate), 0.f, 1.f);
            if (s.camW <= 0.f || cs < 0x1000u || mcs < 0x1000u)
                continue;
            // pivo: o eixo do corpo na altura da boca (a boca gira com a torre e balancaria a camera)
            float body[3], mouth[3];
            rdV(rdram, mcs + kTrans, body);
            rdV(rdram, m + kLocMuzzle, mouth);
            WotmFreeCamPose laser;
            laser.heading = s.aimHead;
            laser.pitch = s.aimPitch;
            float ax[3][3];
            wotmFreeCamAxes(laser, ax[0], ax[1], ax[2]);
            // raio da boca em volta do eixo (suavizado: a boca gira com a torre)
            const float gunR = std::sqrt((mouth[0] - body[0]) * (mouth[0] - body[0]) + (mouth[1] - body[1]) * (mouth[1] - body[1]));
            s.gunR = s.gunR > 0.f ? s.gunR + (gunR - s.gunR) * 0.1f : gunR;
            if (s.aimInit && !s.gunLogged)
            {
                s.gunLogged = true;
                std::fprintf(stderr, "[custom] Mecha: boca a %.0f do eixo, %.0f acima do chao do corpo\n", gunR, mouth[2] - body[2]);
            }
            const float ch = std::cos(s.aimHead), sh = std::sin(s.aimHead), ahead = s.gunR + kLaserCamAhead;
            laser.pos[0] = body[0] + sh * ahead;
            laser.pos[1] = body[1] + ch * ahead;
            laser.pos[2] = mouth[2] + kLaserCamUp;
            // mistura com a pose do jogo (entrada e saida)
            const float w = s.camW * s.camW * (3.f - 2.f * s.camW);
            reticle = std::max(reticle, std::clamp((w - 0.6f) / 0.4f, 0.f, 1.f));
            WotmFreeCamPose p;
            const float gfx = rdF(rdram, cs + 0x30), gfy = rdF(rdram, cs + 0x34), gfz = rdF(rdram, cs + 0x38);
            const float gHead = std::atan2(gfx, gfy), gPitch = std::atan2(gfz, std::sqrt(gfx * gfx + gfy * gfy));
            float dh = laser.heading - gHead;
            while (dh > kPi) dh -= 2.f * kPi;
            while (dh < -kPi) dh += 2.f * kPi;
            p.heading = gHead + dh * w;
            p.pitch = gPitch + (laser.pitch - gPitch) * w;
            for (int k = 0; k < 3; ++k)
                p.pos[k] = rdF(rdram, cs + 0x10 + 4 * k) + (laser.pos[k] - rdF(rdram, cs + 0x10 + 4 * k)) * w;
            wotmFreeCamAxes(p, ax[0], ax[1], ax[2]);
            for (int k = 0; k < 3; ++k)
            {
                wrF(rdram, cs + 0x10 + 4 * k, p.pos[k]);
                for (int r = 0; r < 3; ++r)
                    wrF(rdram, cs + 0x20 + 0x10 * r + 4 * k, ax[r][k]);
            }
            for (int r = 0; r < 3; ++r)
                wrF(rdram, cs + 0x20 + 0x10 * r + 0xC, 0.f);
            wrF(rdram, cs + 0x60, p.pitch);
            wrF(rdram, cs + 0x64, 0.f);
            wrF(rdram, cs + 0x68, p.heading);
        }
    }

    // O choque gasta o especial (o StateUltraTazer::update zera o m_specialWeapon), e o PowerUps::TakeHit nao deixa os tipos
    // 0x1C0/0x1E0 pegarem o item de especial (na fase final o roteiro e quem o liga). Com o Jelly jogador, durante a chamada o tipo
    // vale o do Togera (0x60): pega o item como um monstro comum. A funcao so le o tipo nesses testes (vida, stamina, especial).
    constexpr uint32_t kPowerUpHit = wotm::fn::TakeHit__8PowerUpsP7MonsterP3_cs, kTypeStandIn = 3u << 5;
    PS2Runtime::RecompiledFunction g_origPowerUpHit = nullptr;

    void powerUpHit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = ctx->pc == kPowerUpHit ? GPR_U32(ctx, 4) : 0;
        const bool jelly = m && READ32(m + kTypeBits) == kJelly && isPlayerSlot(rdram, ctx, runtime, m) && READ32(m + kPlayerNum) != 2u;
        if (jelly)
            WRITE32(m + kTypeBits, kTypeStandIn);
        g_origPowerUpHit(rdram, ctx, runtime);
        if (jelly)
            WRITE32(m + kTypeBits, kJelly);
    }

    // WOTM_ANIMLOG=1: uma linha por animacao que um monstro da luta comeca (animationStart, animationTransitionInto): field, slot,
    // tipo e id. Para conferir que uma animacao acrescentada por mod (tools/ngpanim.py) toca. Pelo handle (a0 aponta para ele nas
    // duas): ci, animIdx +0xC; a instancia principal do monstro tem id1 = tipo (indice << 5) e id2 = dupId.
    constexpr uint32_t kAnimStartFn = wotm::fn::animationStart__FG11_animHandleb, kAnimTransFn = wotm::fn::animationTransitionInto__FR11_animHandlefii;
    PS2Runtime::RecompiledFunction g_origAnimStart = nullptr, g_origAnimTrans = nullptr;

    bool animRamOk(uint32_t a, uint32_t n) { return a >= 0x1000u && a + n <= PS2_RAM_SIZE; }
    // os monstros carregam acima de 32 MB (WOTM_HIGHRAM): o cRd32 (mascara de 32 MB) nao serve para os dados do .NGP
    uint32_t aRd32(const uint8_t *rdram, uint32_t a) { uint32_t v = 0; a &= 0x1FFFFFFFu; if (a + 4 <= PS2_RAM_SIZE) std::memcpy(&v, rdram + a, 4); return v; }

    void logAnim(uint8_t *rdram, uint32_t h, const char *how)
    {
        const uint32_t ci = aRd32(rdram, h) & 0x1FFFFFFFu;
        const int16_t k = static_cast<int16_t>(aRd32(rdram, h + 0xC) & 0xFFFFu);
        if (!animRamOk(ci, 0x30u) || k < 0)
            return;
        const uint32_t head = aRd32(rdram, ci), id1 = head >> 18, id2 = (head >> 7) & 0x7FFu;
        if ((id1 & 0x1Fu) || !id1 || id1 > (15u << 5))
            return;
        const uint32_t ch = aRd32(rdram, ci + 0x20) & 0x1FFFFFFFu;
        if (!animRamOk(ch, 0x10u) || uint32_t(k) >= (aRd32(rdram, ch) & 0xFFFFu))
            return;
        const uint32_t an = aRd32(rdram, ch + 0xC + 4u * uint32_t(k)) & 0x1FFFFFFFu;
        if (!animRamOk(an, 4u))
            return;
        for (uint32_t i = 0; i < 8; ++i)
        {
            const uint32_t m = cRd32(rdram, kGameObj + kMonsters + 4 * i) & 0x1FFFFFFFu;
            if (!animRamOk(m, 0x20u) || cRd32(rdram, m + kTypeBits) != id1 || cRd32(rdram, m + kDupId) != id2)
                continue;
            const wotm::mods::Custom *cu = ownFilesCustom(rdram, m);
            std::fprintf(stderr, "[anim] %u m%u %s tipo %u anim %u (%s)\n", cRd32(rdram, wotm::sym::g_frame), i, cu ? cu->id.c_str() : "-",
                         id1 >> 5, aRd32(rdram, an) >> 18, how);
            return;
        }
    }
    void animStart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kAnimStartFn)
            logAnim(rdram, GPR_U32(ctx, 4) & 0x1FFFFFFFu, "start");
        g_origAnimStart(rdram, ctx, runtime);
    }
    void animTrans(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kAnimTransFn)
            logAnim(rdram, GPR_U32(ctx, 4) & 0x1FFFFFFFu, "transition");
        g_origAnimTrans(rdram, ctx, runtime);
    }

    // "special" (monster.json): o golpe especial de outro tipo. Todo Monster traz os estados de especial embutidos, em offsets
    // fixos; o Monster::initDynamics (initAfterDbLoad, antes do .CHR) escolhe um pelo tipo, grava o ponteiro em +0x7980 e chama o
    // init virtual dele. Depois dele, o ponteiro vira o do tipo pedido e o init desse estado roda; quem dispara o especial (botoes,
    // IA, modo treino) le o +0x7980. O especial toca as animacoes 160..162 do proprio monstro. Os testes de tipo dentro dos estados
    // seguem o tipo-base (o fogo so incendeia num Raptros). Vale para o custom com o traje dos arquivos proprios (ou uma skin dele) e
    // para os chefes custom fora da aventura, os unicos que se distinguem do monstro do jogo do mesmo tipo.
    constexpr uint32_t kInitDynamics = wotm::fn::initDynamics__7Monster;
    constexpr uint32_t kVptr = 0x10, kVInit = 0x8;   // MonsterState: vtable em +0x10, init(Monster *) na entrada 1
    // estado do especial de cada tipo (Monster +offset, como no initDynamics); 0 = sem especial
    constexpr uint32_t kSpecialOf[16] = {0,       0xF6F4 /* SonicRoar */,   0xFE5C /* RobotSpecial */, 0xF974 /* FireBreath */,
                                         0x10600 /* GrappleHook */,        0xF9D8 /* BugAttack */,    0,
                                         0x1059C /* ZapAttack */,          0x10930 /* LavaBlast */,   0xF974 /* FireBreath */,
                                         0x102A8 /* RockSpecial */,        0x10790 /* AirStrike */,   0,
                                         0x10F60 /* CannonHands */,        0x11130 /* UltraTazer */,  0x11130 /* UltraTazer */};
    PS2Runtime::RecompiledFunction g_origInitDynamics = nullptr;
    void initDynamics(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != kInitDynamics)
        {
            g_origInitDynamics(rdram, ctx, runtime);
            return;
        }
        const uint32_t m = GPR_U32(ctx, 4), ra = GPR_U32(ctx, 31);
        g_origInitDynamics(rdram, ctx, runtime);
        const wotm::mods::Custom *cu = ownFilesCustom(rdram, m);
        if (!cu && bossType(READ32(m + kTypeBits)) && READ32(kGameObj + kGameMode) != 1u)
            cu = customOf(rdram, m);
        const uint32_t off = cu && cu->special > 0 && cu->special < 16 ? kSpecialOf[cu->special] : 0;
        if (!off)
            return;
        const uint32_t st = m + off, vt = READ32(st + kVptr);
        WRITE32(m + kSpecialState, st);
        const int16_t delta = static_cast<int16_t>(READ16(vt + kVInit));
        wotm_modes4::call(rdram, ctx, runtime, READ32(vt + kVInit + 4), {st + static_cast<uint32_t>(static_cast<int32_t>(delta)), m});
        ctx->pc = ra;
        static std::set<std::string> logged;
        if (logged.insert(cu->id).second)
        {
            const char *who = "?";
            if (const wotm::mods::MonsterInfo *mi = wotm::mods::monsterInfo(cu->special))
                who = mi->name;
            for (const wotm::mods::MonsterInfo &b : wotm::mods::kCustomBases)
                if (b.type == cu->special)
                    who = b.name;
            std::fprintf(stderr, "[custom] %s: especial do %s\n", cu->id.c_str(), who);
        }
    }
    // Cor do fogo ("fireColor" da skin ou do custom; MODS.md): os efeitos do Togera criados por um monstro com cor propria saem
    // dela. Os ganchos de quem os cria guardam o monstro (g_fxOwner): FireBreath::Activate e TestCollis (o bafo e o acerto dele),
    // Weapon::DetonateWeapon (a explosao da bola de fogo; o dono e o m_ownerId) e StateCrowdControl::transitionInto. O gancho dos
    // dois particleCreateFx grava, no indice de fx[] que ele devolve, o preset + 1 e a cor, ou 0 (g_wotmFxTint; o desenho,
    // wotm_fx.hpp, pinta enquanto o preset de fx[i] for o mesmo). So os presets do Togera: poeira, fumaca e clarao branco ficam.
    constexpr uint32_t kFxPos = wotm::fn::particleCreateFx__FP8_fvectorifPA3_ff, kFxMat = wotm::fn::particleCreateFx__FP8_fvectorPA3_fifT0T1bf;
    constexpr uint32_t kBreathActivate = wotm::fn::Activate__10FireBreathfffffff, kBreathCollis = wotm::fn::TestCollis__10FireBreath,
                       kDetonate = wotm::fn::DetonateWeapon__6WeaponR9_hdResult, kCrowdInto = wotm::fn::transitionInto__17StateCrowdControl;
    constexpr uint32_t kBreathOwner = 0x4, kWeaponOwnerId = 0x120, kCrowdOwner = 0xC;
    PS2Runtime::RecompiledFunction g_origFxPos = nullptr, g_origFxMat = nullptr, g_origBreathActivate = nullptr, g_origBreathCollis = nullptr,
                                   g_origDetonate = nullptr, g_origCrowdInto = nullptr;
    uint32_t g_fxOwner = 0;   // monstro dono dos efeitos criados agora (0 = nenhum)

    bool togeraFx(uint32_t preset)
    {
        switch (preset)
        {
        case 8:     // LizardBreathCore
        case 36:    // LizardFlashBang
        case 51:    // LizardBreath
        case 80:    // LizardHit
        case 81:    // LizardHitSparks
        case 95:    // LizardSpecialHit
        case 132:   // LizardCrowdControl1
        case 148:   // LizardBreathSparks
            return true;
        default:
            return false;
        }
    }
    // A cor do fogo do monstro: a da skin do traje, ou a do custom dono dela; 0 = a do jogo
    uint32_t fireColorOf(uint8_t *rdram, uint32_t m)
    {
        const int costume = m ? monsterCostume(rdram, m) : -1;
        if (costume < 4)
            return 0;
        const wotm::mods::Registry &r = wotm::mods::Registry::get();
        const wotm::mods::Skin *k = r.skinByIndex(int(cRd32(rdram, m + kTypeBits) >> 5), costume);
        if (!k)
            return 0;
        if (k->fireColor)
            return k->fireColor;
        const wotm::mods::Custom *cu = k->custom.empty() ? nullptr : r.custom(k->custom);
        return cu ? cu->fireColor : 0;
    }
    void fxCreate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t entry, PS2Runtime::RecompiledFunction orig)
    {
        const int presetReg = entry == kFxPos ? 5 : 6;   // (pos, id, ...) / (pos, mat, id, ...); fora do GPR_U32, que nao poe parenteses
        const uint32_t preset = ctx->pc == entry ? GPR_U32(ctx, presetReg) : ~0u;
        const uint32_t owner = g_fxOwner;
        orig(rdram, ctx, runtime);
        const uint32_t idx = GPR_U32(ctx, 2);
        static const bool dbg = [] { const char *e = std::getenv("WOTM_FXCREATELOG"); return e && *e && *e != '0'; }();
        if (dbg && preset != ~0u)
            std::fprintf(stderr, "[fx] criado fx[%u] preset %u por 0x%08X, dono 0x%08X\n", idx, preset, GPR_U32(ctx, 31), owner);
        if (preset == ~0u || idx >= 100)
            return;
        const uint32_t color = owner && togeraFx(preset) ? fireColorOf(rdram, owner) : 0;
        g_wotmFxTint[idx].store(color ? ((preset + 1) << 24 | (color & 0xFFFFFFu)) : 0, std::memory_order_relaxed);
        if (color)
        {
            static std::set<uint64_t> logged;   // uma linha por preset e cor
            if (logged.insert(uint64_t(preset) << 32 | color).second)
                std::fprintf(stderr, "[custom] fogo: preset %u na cor #%06X\n", preset, color & 0xFFFFFFu);
        }
    }
    void fxPos(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { fxCreate(rdram, ctx, runtime, kFxPos, g_origFxPos); }
    void fxMat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { fxCreate(rdram, ctx, runtime, kFxMat, g_origFxMat); }
    // Roda `orig` com g_fxOwner = m (o anterior volta no fim: as chamadas se aninham)
    void withFxOwner(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m, PS2Runtime::RecompiledFunction orig)
    {
        const uint32_t prev = g_fxOwner;
        g_fxOwner = m;
        orig(rdram, ctx, runtime);
        g_fxOwner = prev;
    }
    void breathActivate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = ctx->pc == kBreathActivate ? READ32(GPR_U32(ctx, 4) + kBreathOwner) : 0;
        withFxOwner(rdram, ctx, runtime, m, g_origBreathActivate);
    }
    void breathCollis(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = ctx->pc == kBreathCollis ? READ32(GPR_U32(ctx, 4) + kBreathOwner) : 0;
        withFxOwner(rdram, ctx, runtime, m, g_origBreathCollis);
    }
    void crowdInto(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = ctx->pc == kCrowdInto ? READ32(GPR_U32(ctx, 4) + kCrowdOwner) : 0;
        withFxOwner(rdram, ctx, runtime, m, g_origCrowdInto);
    }
    void detonate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t m = 0;
        if (ctx->pc == kDetonate)
            if (const uint32_t id = READ32(GPR_U32(ctx, 4) + kWeaponOwnerId))
            {
                const uint32_t o = wotm_modes4::call(rdram, ctx, runtime, kGetInteractive, {id});
                for (uint32_t i = 0; o && i < 8; ++i)   // so um monstro da luta (o dono pode ser um veiculo, uma torre)
                    if (READ32(kGameObj + kMonsters + 4 * i) == o)
                        m = o;
            }
        withFxOwner(rdram, ctx, runtime, m, g_origDetonate);
    }
} // namespace wotm_custom

// O custom de um Monster com o traje dos arquivos proprios (ou uma skin dele); nullptr = monstro do jogo. O banco de som proprio
// (wotm_snd.inc, outro TU) pergunta por aqui.
const wotm::mods::Custom *wotmCustomOwnFiles(uint8_t *rdram, uint32_t monster) { return wotm_custom::ownFilesCustom(rdram, monster); }

// Depois do wotmSkinsInstall: embrulha os ganchos do SetAIMonster que ja estiverem registrados (WOTM_AIVSAI, skins).
void wotmCustomInstall(PS2Runtime &runtime)
{
    using namespace wotm_custom;
    if ((g_origSetAI = runtime.lookupFunction(kSetAI)))
        runtime.registerFunction(kSetAI, &setAI);
    if ((g_origTakeHit = runtime.lookupFunction(kTakeHit)))
        runtime.registerFunction(kTakeHit, &takeHit);
    if ((g_origSetPov = runtime.lookupFunction(kSetPov)))
        runtime.registerFunction(kSetPov, &setPov);
    g_loadMech = runtime.lookupFunction(kLoadMech);
    g_killPickup = runtime.lookupFunction(kKillPickup);
    if ((g_origImpale = runtime.lookupFunction(kImpale)))
        runtime.registerFunction(kImpale, &impale);
    if ((g_origTazerOK = runtime.lookupFunction(kTazerOK)))
        runtime.registerFunction(kTazerOK, &tazerOK);
    if ((g_origTazerUpdate = runtime.lookupFunction(kTazerUpdate)))
        runtime.registerFunction(kTazerUpdate, &tazerUpdate);
    if ((g_origPowerUpHit = runtime.lookupFunction(kPowerUpHit)))
        runtime.registerFunction(kPowerUpHit, &powerUpHit);
    if ((g_origLookAt = runtime.lookupFunction(kLookAt)))
        runtime.registerFunction(kLookAt, &lookAt);
    if ((g_origCrowdOK = runtime.lookupFunction(kCrowdOK)))
        runtime.registerFunction(kCrowdOK, &crowdOK);
    if ((g_origEnterState = runtime.lookupFunction(kEnterState)))
        runtime.registerFunction(kEnterState, &enterState);
    if ((g_origSpinUpdate = runtime.lookupFunction(kSpinUpdate)))
        runtime.registerFunction(kSpinUpdate, &spinUpdate);
    if ((g_origSetBossState = runtime.lookupFunction(kSetBossState)))
        runtime.registerFunction(kSetBossState, &setBossState);
    if ((g_origLoadBank = runtime.lookupFunction(kLoadBank)))
        runtime.registerFunction(kLoadBank, &loadBank);
    if ((g_origLoadLoBank = runtime.lookupFunction(kLoadLoBank)))
        runtime.registerFunction(kLoadLoBank, &loadLoBank);
    if ((g_origSetCharacter = runtime.lookupFunction(kSetCharacter)))
        runtime.registerFunction(kSetCharacter, &setCharacter);
    if ((g_origSetAiReset = runtime.lookupFunction(kSetAiReset)))
        runtime.registerFunction(kSetAiReset, &setAiReset);
    if ((g_origMonsterUpdate = runtime.lookupFunction(kMonsterUpdate)))
        runtime.registerFunction(kMonsterUpdate, &monsterUpdate);
    if ((g_origTakeDamage = runtime.lookupFunction(kTakeDamage)))
        runtime.registerFunction(kTakeDamage, &takeDamage);
    if ((g_origInitDynamics = runtime.lookupFunction(kInitDynamics)))
        runtime.registerFunction(kInitDynamics, &initDynamics);
    if ((g_origFxPos = runtime.lookupFunction(kFxPos)))
        runtime.registerFunction(kFxPos, &fxPos);
    if ((g_origFxMat = runtime.lookupFunction(kFxMat)))
        runtime.registerFunction(kFxMat, &fxMat);
    if ((g_origBreathActivate = runtime.lookupFunction(kBreathActivate)))
        runtime.registerFunction(kBreathActivate, &breathActivate);
    if ((g_origBreathCollis = runtime.lookupFunction(kBreathCollis)))
        runtime.registerFunction(kBreathCollis, &breathCollis);
    if ((g_origDetonate = runtime.lookupFunction(kDetonate)))
        runtime.registerFunction(kDetonate, &detonate);
    if ((g_origCrowdInto = runtime.lookupFunction(kCrowdInto)))
        runtime.registerFunction(kCrowdInto, &crowdInto);
    if (const char *e = std::getenv("WOTM_ANIMLOG"); e && *e && *e != '0')
    {
        if ((g_origAnimStart = runtime.lookupFunction(kAnimStartFn)))
            runtime.registerFunction(kAnimStartFn, &animStart);
        if ((g_origAnimTrans = runtime.lookupFunction(kAnimTransFn)))
            runtime.registerFunction(kAnimTransFn, &animTrans);
        std::fprintf(stderr, "[anim] log das animacoes dos monstros: %s\n", g_origAnimStart && g_origAnimTrans ? "ligado" : "SEM gancho");
    }
    std::fprintf(stderr, "[custom] chefes jogaveis (Goliath, Jelly, Cerebulon): %s, %s\n", g_origSetAI ? "visivel com IAs" : "SEM gancho do SetAIMonster",
                 g_origTakeHit ? "toma dano das IAs" : "SEM gancho do takeHit");
}
