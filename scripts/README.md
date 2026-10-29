# Scripts (implemented)

| Script | Purpose |
|---|---|
| `check_env.sh` | Detect DevEco/SDK/NDK/cmake/ninja/hdc; write `evidence/env_check.json` |
| `gen_fields.py` / `codegen.sh` | Generate/verify `src/host/decoder/generated/q207_demo_fields.h` from `proto/example.proto` |
| `build.sh` | V0: host `.so` + `ohpm install` + hvigor HAP; `PASS-UNSIGNED` / exit 0 when unsigned HAP built |
| `build_hap.sh` | hvigor-only HAP shortcut; runs `ohpm install` first |
| `build_host.sh` | HOST-ONLY: build C++/TS slice, 433 tests, data cards, bench, differential, evidence |
| `inspect_so.sh` | Read-only ELF/NAPI inspection; PASS/FAIL/ERROR exit codes |
| `inspect_hap.py` | Read-only HAP ZIP/module/native-lib/signature inspection; optional `--sign-check` |
| `sign_hap.sh` | Local self-signed HAP: PKI/profile → sign-profile → sign-app → verify-app; **not installable** |
| `record_build.py` | Write `evidence/v0_build.json` with commands, exits, artifacts, hashes |

Not present yet: full `.proto`-to-POD/ArkTS codegen, schema inventory, statistics/replay layers. Those are
tracked as remaining work in the README/report, not hidden behind these scripts.
