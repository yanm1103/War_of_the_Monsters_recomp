## What changes
<!-- TU(s) and functions touched -->

## Checklist
- [ ] `sh tools/wsl/gate.sh` ended in `ROM OK` (paste the last line below)
- [ ] No game file (ISO, `asm/`, assets) nor assembly snippet in the PR
- [ ] Functions that do not match are in `#ifdef NON_MATCHING` with a note (`nm_wrap.py`)
- [ ] Equivalents checked: return value used by callers, call sequence, struct strides, delay slots
- [ ] Functions in original address order; `static` helpers inside `NON_MATCHING`
- [ ] I did not commit `config/status.csv`
- [ ] If I touched a shared header: I compared `scoreall.sh` before/after

## Gate output
```
(paste here)
```

## Count
<!-- new matched / equivalent, e.g.: +12 matched, +5 equivalent -->
