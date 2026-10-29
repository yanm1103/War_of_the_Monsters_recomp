#ifndef WOTM_VU0_NATIVE_H
#define WOTM_VU0_NATIVE_H

#include "runtime/ps2_vu1.h"

#include <cstddef>
#include <cstdint>

// Microprograma do VU0 em C++ (gerado por tools/vu0native.py): recebe o estado ja copiado do contexto do EE e a memoria de
// dados do VU0, e deixa estado e memoria como o interpretador deixaria ao fim do programa (inclusive pc, ciclos e flags).
using WotmVu0Fn = void (*)(VU1State &state, uint8_t *vu0Data);

struct WotmVu0Program
{
    uint32_t start;       // pc de entrada (bytes)
    uint32_t hashStart;   // inicio do trecho conferido pelo hash (todos os pares que o programa pode executar)
    uint32_t length;      // bytes desse trecho
    uint64_t hash;     // FNV-1a 64 desses bytes
    WotmVu0Fn fn;
};

// Versao nativa do programa em pc, ou nullptr (sem versao nativa, codigo diferente do esperado, ou WOTM_VU0_INTERP definido).
WotmVu0Fn wotmVu0NativeLookup(const uint8_t *vu0Code, uint32_t codeSize, uint64_t generation, uint32_t pc);
// Imagem de 4096 bytes da memoria de codigo do VU0 que o pacote do ELF deixa (o hierLoadVu0Ucode a manda pelo VIF0), ou nullptr
// se o .gen.inc for anterior a ela. O executavel do jogo a copia direto, sem VIF0 (BOUNDARY.md, passo 7).
const uint8_t *wotmVu0CodeImage();
// Roda com o arredondamento do VU (para zero).
void wotmVu0NativeRun(WotmVu0Fn fn, VU1State &state, uint8_t *vu0Data);

#endif
