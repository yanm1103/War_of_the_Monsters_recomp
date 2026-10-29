#!/usr/bin/env python3
"""Checa a separação dos docs: referência no presente, TODO só com o aberto, diário no git.

Três tipos de texto (regra completa no CLAUDE.md, "Documentação"):
- Referência (README.md, CLAUDE.md, docs/*.md, docs/guides/*.md): como as coisas são hoje. Sem datas, sem "antes era", sem
  "feito/corrigido em", sem relato de teste ("conferido"). A história vai na mensagem do commit.
- TODO.md: só o que está aberto. Item terminado sai do arquivo (sem "feito", sem riscado).
- Diário: mensagens de commit e docs/investigacoes/<data>/ (não são checados aqui).

Datas dentro de um caminho entre crases (`docs/screenshots/2026-10-22/...`, `<commit>:docs/investigacoes/...`) valem.

Uso:
  python tools/doclint.py              só as linhas que o commit acrescenta (git diff --cached) + o TODO.md inteiro
  python tools/doclint.py --all        tudo, com a contagem por arquivo (o que ainda falta migrar)
  python tools/doclint.py <arquivo...> esses arquivos inteiros
Sai com 1 se achar algum problema.
"""
import re
import subprocess
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
TODO = "TODO.md"

DATA = re.compile(r"\b20\d\d-\d\d-\d\d\b")
CODIGO = re.compile(r"`[^`]*`")  # crases em pares, da esquerda para a direita


def tira_caminhos(linha):
    """Apaga os trechos entre crases que são caminhos (têm "/"): data dentro de caminho vale."""
    return CODIGO.sub(lambda m: "``" if "/" in m.group(0) else m.group(0), linha)


HISTORIA = re.compile(
    r"\b(feit[oa]s? em|corrigid[oa]s? (em|no|na|de passagem)|antes (era|ficava|saía|vinha|dava|zerava|ia|não)|"
    r"na master|(depois|antes|desde) d[oe] merge\b|conferid[oa]s?\b|relato de|"
    r"fixed (in|on|by|along the way)\b|used to (be|go|come|give|stay|reset)\b|(was|were) (fixed|done) in\b|"
    r"on master\b|(after|before|since) the merge\b|verified by the owner\b)",
    re.IGNORECASE,
)
CITACAO = re.compile(r'"[^"\n]*"')  # palavra entre aspas é citada (ex.: a própria regra), não é história
FEITO = re.compile(r"(\bfeit[oa]s? (em|no|na|pel[oa]s?)\b|\*\*feit|— feit|\(feit|\bna master\b|~~|\bcorrigid[oa]s? em\b|"
                   r"\bconferid[oa]s? pelo dono\b|\bdone (in|on|by)\b|\bon master\b|\bfixed in\b)", re.IGNORECASE)
MAX_LINHAS_ITEM = 6  # um item do TODO (bullet ou número) com mais linhas que isso vira relato


def e_referencia(rel):
    rel = rel.replace("\\", "/")
    if not rel.endswith(".md") or rel == TODO:
        return False
    if rel in ("README.md", "CLAUDE.md", "CONTRIBUTING.md"):
        return True
    return re.fullmatch(r"docs/(guides/)?[^/]+\.md", rel) is not None


def problemas_referencia(linha):
    """[(chave, mensagem)]; a chave é o texto achado, para comparar com a linha que o commit tirou."""
    sem_caminhos = tira_caminhos(linha)
    achados = []
    for d in DATA.findall(sem_caminhos):
        achados.append((d, f"data ({d})"))
    for m in HISTORIA.finditer(CITACAO.sub('""', sem_caminhos)):
        achados.append((m.group(0).lower(), f'história ("{m.group(0)}")'))
    return achados


def checa_todo(texto):
    erros = []
    linhas = texto.split("\n")
    inicio, n = None, 0
    for i, linha in enumerate(linhas, 1):
        sem_caminhos = tira_caminhos(linha)
        if DATA.search(sem_caminhos):
            erros.append((i, "data no TODO (a data é do commit)"))
        m = FEITO.search(sem_caminhos)
        if m:
            erros.append((i, f'item feito no TODO ("{m.group(0)}"): apague a linha e documente no doc de referência'))
        if re.match(r"\s*(- |\d+\. )", linha) and not linha.startswith("  " * 2):
            if inicio and n > MAX_LINHAS_ITEM:
                erros.append((inicio, f"item com {n} linhas (máximo {MAX_LINHAS_ITEM}): o detalhe vai no doc de referência"))
            inicio, n = i, 1
        elif inicio and linha.startswith("  ") and linha.strip():
            n += 1
        else:
            if inicio and n > MAX_LINHAS_ITEM:
                erros.append((inicio, f"item com {n} linhas (máximo {MAX_LINHAS_ITEM}): o detalhe vai no doc de referência"))
            inicio, n = None, 0
    if inicio and n > MAX_LINHAS_ITEM:  # último item do arquivo
        erros.append((inicio, f"item com {n} linhas (máximo {MAX_LINHAS_ITEM}): o detalhe vai no doc de referência"))
    return erros


def linhas_acrescentadas():
    """({arquivo: [(num, linha)]} das linhas que o commit acrescenta, {arquivo: texto das linhas que ele tira})."""
    saida = subprocess.run(["git", "diff", "--cached", "-U0", "--no-color", "--", "*.md"], cwd=RAIZ,
                           capture_output=True).stdout.decode("utf-8", "replace")
    por_arquivo, tiradas, atual, num = {}, {}, None, 0
    for linha in saida.split("\n"):
        if linha.startswith("+++ "):
            atual = linha[6:] if linha.startswith("+++ b/") else None
        elif linha.startswith("--- "):
            continue
        elif linha.startswith("@@"):
            num = int(re.search(r"\+(\d+)", linha).group(1))
        elif atual and linha.startswith("+"):
            por_arquivo.setdefault(atual, []).append((num, linha[1:]))
            num += 1
        elif atual and linha.startswith("-"):
            tiradas.setdefault(atual, []).append(linha[1:])
    return por_arquivo, tiradas


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    args = sys.argv[1:]
    erros = []
    tiradas = {}
    if args == ["--all"]:
        arquivos = [p.relative_to(RAIZ).as_posix() for p in
                    [RAIZ / "README.md", RAIZ / "CLAUDE.md", RAIZ / "CONTRIBUTING.md", *sorted((RAIZ / "docs").glob("*.md")),
                     *sorted((RAIZ / "docs/guides").glob("*.md"))] if p.exists()]
        alvo = {a: list(enumerate((RAIZ / a).read_text("utf-8").split("\n"), 1)) for a in arquivos}
    elif args:
        alvo = {Path(a).resolve().relative_to(RAIZ).as_posix(): None for a in args}
        for a in alvo:
            alvo[a] = list(enumerate((RAIZ / a).read_text("utf-8").split("\n"), 1))
    else:
        alvo, tiradas = linhas_acrescentadas()
    for rel, linhas in alvo.items():
        if not e_referencia(rel):
            continue
        # Linha antiga só editada: o que já estava nas linhas tiradas do mesmo arquivo não conta (migra-se aos poucos).
        ja_tinha = {}
        for velha in tiradas.get(rel, []):
            for chave, _ in problemas_referencia(velha):
                ja_tinha[chave] = ja_tinha.get(chave, 0) + 1
        for num, linha in linhas:
            for chave, p in problemas_referencia(linha):
                if ja_tinha.get(chave, 0) > 0:
                    ja_tinha[chave] -= 1
                    continue
                erros.append((rel, num, p))
    todo = RAIZ / TODO
    if todo.exists() and (not args or args == ["--all"] or TODO in alvo):
        erros += [(TODO, n, p) for n, p in checa_todo(todo.read_text("utf-8"))]

    if args == ["--all"]:
        contagem = {}
        for rel, _, _ in erros:
            contagem[rel] = contagem.get(rel, 0) + 1
        for rel in sorted(contagem, key=lambda r: -contagem[r]):
            print(f"{contagem[rel]:5}  {rel}")
        print(f"{len(erros)} problema(s) em {len(contagem)} arquivo(s)" if erros else "tudo limpo")
    else:
        for rel, num, p in erros:
            print(f"{rel}:{num}: {p}")
        if erros:
            print(f"\n{len(erros)} problema(s). Referência fica no presente (história vai na mensagem do commit); "
                  "o TODO.md só tem o que está aberto. Regra: CLAUDE.md, \"Documentação\".")
    return 1 if erros else 0


if __name__ == "__main__":
    sys.exit(main())
