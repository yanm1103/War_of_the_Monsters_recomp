// Chamada a codigo do jogo de dentro de um gancho nosso (ra = 0): termina a chamada que voltou no meio.
// O codigo gerado confere o escalonador em toda chamada e cabeca de laco; no ps2EntryRunner.exe, com um evento pendente (vblank,
// interrupcao) a funcao volta ja, com o pc no ponto de retomada, esperando o laco do escalonador continuar (lookupFunction(pc)).
// Um gancho que restaura o contexto logo depois perdia o resto da funcao (o viewSetFov da tela remontada do co-op saia com os planos
// do frustum pela metade: monstro e pedacos da fase cortados). Aqui o mesmo despacho do escalonador continua ate o pc chegar ao ra 0.
// So para funcoes que nao esperam quadros (essas vao como cauda: NATIVO.md, "hooks de host terminam com pc = $ra").
#pragma once
#include <cstdint>
#include <cstdio>

inline void wotmFinishGuestCall(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    for (uint32_t n = 0; ctx->pc != 0u; ++n)
    {
        const PS2Runtime::RecompiledFunction f = n < 1000000u ? runtime->lookupFunction(ctx->pc) : nullptr;
        if (!f)
        {
            std::fprintf(stderr, "[guest] chamada de gancho parada em pc %x (%u retomadas)\n", ctx->pc, n);
            return;
        }
        f(rdram, ctx, runtime);
    }
}
