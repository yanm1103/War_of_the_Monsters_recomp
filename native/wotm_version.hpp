// Versao do build: a tag do release ou o commit do repo (git describe --tags --always --dirty), escrita pelo tools/build_runtime.ps1
// em <build>\wotm_gen\wotm_version.gen.h. Vai para a primeira linha do log e para o titulo da janela, para casar um relato (ou um
// screenshot) com o build e o PDB dele.
#pragma once
#if __has_include("wotm_version.gen.h")
#include "wotm_version.gen.h"
#endif
#ifndef WOTM_VERSION
#define WOTM_VERSION "dev"
#endif
