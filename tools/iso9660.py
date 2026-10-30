#!/usr/bin/env python3
"""Leitura de uma imagem ISO9660 (o disco do jogo como .iso), so o necessario para o kit de mods: listar pastas e copiar arquivos.

  python tools/iso9660.py <iso> [pasta]      lista a pasta (padrao: a raiz)

Nomes sem diferenciar maiusculas e sem a versao ";1", como o native/wotm_disc.hpp.
"""
import os
import struct
import sys
import threading

SECTOR = 2048


class Iso:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        self.lock = threading.RLock()   # a interface le retratos numa thread enquanto extrai em outra
        pvd = self._read(16 * SECTOR, SECTOR)
        if pvd[0] != 1 or pvd[1:6] != b"CD001":
            self.f.close()
            raise ValueError("%s is not an ISO9660 image" % path)
        self.root = self._entry(pvd[156:156 + 34])
        self._dirs = {}   # "MON" -> {"LIZARD0.TEX": (lba, tamanho, e_pasta, nome)}

    def close(self):
        self.f.close()

    def _read(self, off, n):
        with self.lock:
            self.f.seek(off)
            return self.f.read(n)

    @staticmethod
    def _entry(rec):
        lba, size = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
        flags, nlen = rec[25], rec[32]
        name = rec[33:33 + nlen].decode("ascii", "replace").split(";")[0].rstrip(".")
        return lba, size, bool(flags & 2), name

    def _dir(self, rel):
        with self.lock:
            return self._dir_locked(rel)

    def _dir_locked(self, rel):
        key = rel.strip("/").upper()
        if key in self._dirs:
            return self._dirs[key]
        if key:
            parent, _, base = key.rpartition("/")
            e = self._dir(parent)
            e = e and e.get(base)
            if not e or not e[2]:
                return None
        else:
            e = self.root
        lba, size = e[0], e[1]
        data = self._read(lba * SECTOR, size)
        out, o = {}, 0
        while o < len(data):
            n = data[o]
            if n == 0:   # registros nao cruzam setores
                o = (o // SECTOR + 1) * SECTOR
                continue
            ent = self._entry(data[o:o + n])
            if data[o + 32] == 1 and data[o + 33] in (0, 1):   # "." e ".."
                o += n
                continue
            out[ent[3].upper()] = ent
            o += n
        self._dirs[key] = out
        return out

    def find(self, rel):
        """(lba, tamanho, e_pasta, nome) de um caminho ("MON/LIZARD0.TEX"), ou None."""
        rel = rel.replace("\\", "/").strip("/")
        if not rel:
            return self.root
        parent, _, base = rel.rpartition("/")
        d = self._dir(parent)
        return d.get(base.upper()) if d else None

    def listdir(self, rel=""):
        """Nomes da pasta como estao no disco; [] se ela nao existe."""
        d = self._dir(rel.replace("\\", "/"))
        return [e[3] for e in d.values()] if d else []

    def extract(self, rel, dst):
        e = self.find(rel)
        if not e or e[2]:
            raise FileNotFoundError(rel)
        tmp = dst + ".part"
        os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
        with open(tmp, "wb") as out, self.lock:
            self.f.seek(e[0] * SECTOR)
            left = e[1]
            while left:
                b = self.f.read(min(left, 1 << 20))
                if not b:
                    raise IOError("%s: image ends before %s" % (self.path, rel))
                out.write(b)
                left -= len(b)
        os.replace(tmp, dst)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    iso = Iso(sys.argv[1])
    sub = sys.argv[2] if len(sys.argv) > 2 else ""
    for n in sorted(iso.listdir(sub)):
        e = iso.find((sub + "/" if sub else "") + n)
        print("%-20s %s" % (n, "<dir>" if e[2] else e[1]))
