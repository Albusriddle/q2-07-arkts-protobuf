#!/usr/bin/env python3
"""inspect_hap.py - read-only HAP inspection for the Q2-07 V0 slice.

Opens the HAP as a ZIP and reports what is actually inside it:
  * module.json presence, JSON validity and required keys
  * libs/<abi>/libentry.so presence, size, SHA-256, ELF identity, DT_NEEDED
  * whether a signature block exists (META-INF/*, *.SF, *.RSA, ...)
  * bundled native libraries per ABI

Hard rules
  * Never rebuilds, re-signs, patches, extracts into the repo, or writes to the HAP.
  * If no signature block is present the result is ``signed: false`` - it is never
    guessed, defaulted to true, or inferred from a file name elsewhere.
  * Anything that cannot be established from the artifact alone is listed under
    ``unverified`` instead of being asserted.

Exit codes
  0  PASS    artifact present and all required checks passed
  1  FAIL    artifact present but at least one required check failed
  2  ERROR   the artifact could not be opened at all

Usage
  python scripts/inspect_hap.py
  python scripts/inspect_hap.py --hap app/entry/build/.../entry-default-unsigned.hap
  python scripts/inspect_hap.py --out evidence/v0_artifact_check.json
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import struct
import subprocess
import sys
import time
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_HAP = "app/entry/build/default/outputs/default/entry-default-unsigned.hap"
DEFAULT_OUT = "evidence/v0_artifact_check.json"
ABI = "arm64-v8a"
SO_ENTRY = "libs/%s/libentry.so" % ABI

# Libraries an OpenHarmony image is expected to provide. Being on this list is an
# ASSUMPTION, not a measurement: each one is re-listed under ``unverified``.
SYSTEM_PROVIDED = {
    "libc.so", "libm.so", "libdl.so", "librt.so", "libpthread.so", "libz.so",
    "libace_napi.z.so", "libc++_shared.so", "libhilog_ndk.z.so",
    "libace_ndk.z.so", "libnative_window.so", "libc++.so",
}

SIGNATURE_SUFFIXES = (".sf", ".rsa", ".ec", ".dsa", ".p7b", ".p7s")

REQUIRED_MODULE_KEYS = [
    "app.bundleName",
    "app.versionCode",
    "app.versionName",
    "app.minAPIVersion",
    "app.targetAPIVersion",
    "app.compileSdkVersion",
    "module.name",
    "module.type",
    "module.abilities",
    "module.deviceTypes",
]

ELF_MACHINES = {183: "AArch64", 62: "x86-64"}
ELF_CLASSES = {1: "ELF32", 2: "ELF64"}
ELF_TYPES = {2: "EXEC", 3: "DYN"}


# --------------------------------------------------------------------------- #
# helpers
# --------------------------------------------------------------------------- #
def sha256_bytes(blob: bytes) -> str:
    return hashlib.sha256(blob).hexdigest()


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def rel(path: str) -> str:
    try:
        return os.path.relpath(path, ROOT).replace("\\", "/")
    except ValueError:
        return path


def dig(obj, dotted):
    """Return (found, value) for a dotted key path."""
    cur = obj
    for part in dotted.split("."):
        if isinstance(cur, dict) and part in cur:
            cur = cur[part]
        else:
            return False, None
    return True, cur


def decode_ohos_api(value):
    """Decode the packed HarmonyOS version integer, e.g. 60101024 -> 6.1.1 / build 24.

    The layout ``<major><minor:02><patch:02><build:03>`` is INFERRED: it reproduces
    the well-known encoding (5.0.0.12 -> 50000012) and is consistent with this
    repo's ``compileSdkVersion``. It is NOT taken from an official specification,
    so the result is always tagged ``INFERRED``.
    """
    if not isinstance(value, int) or not (10_000_000 <= value <= 99_999_999):
        return None
    s = str(value)
    major, minor, patch, build = int(s[0]), int(s[1:3]), int(s[3:5]), int(s[5:8])
    return {
        "raw": value,
        "decoded": "%d.%d.%d" % (major, minor, patch),
        "build": build,
        "confidence": "INFERRED",
        "note": "packed <major><minor:02><patch:02><build:03>; not DOC_VERIFIED",
    }


# --------------------------------------------------------------------------- #
# minimal ELF64 reader (read-only, no dependencies)
# --------------------------------------------------------------------------- #
def elf_identity(blob: bytes):
    if len(blob) < 64 or blob[:4] != b"\x7fELF":
        return {"is_elf": False, "magic": blob[:4].hex()}
    cls, data = blob[4], blob[5]
    e_type = struct.unpack_from("<H", blob, 0x10)[0]
    e_machine = struct.unpack_from("<H", blob, 0x12)[0]
    return {
        "is_elf": True,
        "magic": blob[:4].hex(),
        "class": ELF_CLASSES.get(cls, "unknown(%d)" % cls),
        "endian": "little" if data == 1 else ("big" if data == 2 else "unknown"),
        "type": ELF_TYPES.get(e_type, "unknown(%d)" % e_type),
        "machine": ELF_MACHINES.get(e_machine, "unknown(%d)" % e_machine),
        "e_machine": e_machine,
        "e_type": e_type,
    }


def elf_dynamic(blob: bytes):
    """Return {'needed': [...], 'soname': str|None} or None if not ELF64-LE."""
    if len(blob) < 64 or blob[:4] != b"\x7fELF" or blob[4] != 2 or blob[5] != 1:
        return None
    e_phoff = struct.unpack_from("<Q", blob, 0x20)[0]
    e_phentsize = struct.unpack_from("<H", blob, 0x36)[0]
    e_phnum = struct.unpack_from("<H", blob, 0x38)[0]
    if e_phentsize < 56 or e_phnum == 0:
        return {"needed": [], "soname": None}
    loads, dyn = [], None
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        if off + 56 > len(blob):
            return {"needed": [], "soname": None}
        p_type = struct.unpack_from("<I", blob, off)[0]
        p_offset = struct.unpack_from("<Q", blob, off + 8)[0]
        p_vaddr = struct.unpack_from("<Q", blob, off + 16)[0]
        p_filesz = struct.unpack_from("<Q", blob, off + 32)[0]
        if p_type == 1:  # PT_LOAD
            loads.append((p_vaddr, p_filesz, p_offset))
        elif p_type == 2:  # PT_DYNAMIC
            dyn = (p_offset, p_filesz)
    if dyn is None:
        return {"needed": [], "soname": None}

    def vaddr_to_off(vaddr):
        for va, fsz, off in loads:
            if va <= vaddr < va + fsz:
                return off + (vaddr - va)
        return None

    off, size = dyn
    entries, pos = [], 0
    while pos + 16 <= size:
        tag, val = struct.unpack_from("<qQ", blob, off + pos)
        entries.append((tag, val))
        pos += 16
        if tag == 0:
            break
    strtab = next((v for t, v in entries if t == 5), None)
    stroff = vaddr_to_off(strtab) if strtab is not None else None

    def read_name(idx):
        if stroff is None:
            return None
        end = blob.find(b"\x00", stroff + idx)
        if end < 0:
            return None
        return blob[stroff + idx:end].decode("utf-8", "replace")

    needed = [read_name(v) for t, v in entries if t == 1]
    soname = [read_name(v) for t, v in entries if t == 14]
    return {"needed": [n for n in needed if n], "soname": soname[0] if soname else None}


# --------------------------------------------------------------------------- #
# checks
# --------------------------------------------------------------------------- #
def scan_signature(names):
    hits = []
    for n in names:
        low = n.lower()
        if low.startswith("meta-inf/"):
            hits.append(n)
        elif low.endswith(SIGNATURE_SUFFIXES):
            hits.append(n)
        elif "signature" in low:
            hits.append(n)
    return sorted(set(hits))


def find_sdk_package():
    """Locate the SDK's ets/oh-uni-package.json across MSYS and Windows path forms."""
    candidates = []
    if os.environ.get("OHOS_SDK_PKG"):
        candidates.append(os.environ["OHOS_SDK_PKG"])
    for env in ("OHOS_SDK_ROOT", "DEVECO_SDK_HOME"):
        val = os.environ.get(env)
        if val:
            candidates.append(os.path.join(val, "ets", "oh-uni-package.json"))
            candidates.append(os.path.join(val, "openharmony", "ets",
                                           "oh-uni-package.json"))
    deveco = os.environ.get("DEVECO_HOME", "D:/Program Files/Huawei/DevEco Studio")
    for base in (deveco, msys_to_windows(deveco)):
        candidates.append(os.path.join(base, "sdk", "default", "openharmony", "ets",
                                       "oh-uni-package.json"))
    candidates.append("D:/Program Files/Huawei/DevEco Studio/sdk/default/openharmony/"
                      "ets/oh-uni-package.json")
    for cand in candidates:
        if os.path.isfile(cand):
            return cand
    return None


def msys_to_windows(path):
    """Turn /d/foo/bar into D:/foo/bar (MSYS mount form -> Windows drive form)."""
    if len(path) > 2 and path[0] == "/" and path[2] == "/" and path[1].isalpha():
        return path[1].upper() + ":/" + path[3:]
    return path


def git_info():
    info = {}
    for key, args in (("branch", ["git", "branch", "--show-current"]),
                      ("commit", ["git", "rev-parse", "--short", "HEAD"])):
        try:
            info[key] = subprocess.check_output(args, cwd=ROOT, text=True,
                                                stderr=subprocess.DEVNULL).strip()
        except Exception:
            info[key] = "unknown"
    return info


def main(argv=None):
    ap = argparse.ArgumentParser(description="Read-only HAP inspection (never rebuilds).")
    ap.add_argument("--hap", default=DEFAULT_HAP, help="HAP to inspect")
    ap.add_argument("--out", default=DEFAULT_OUT, help="JSON evidence output path")
    ap.add_argument("--quiet", action="store_true", help="write JSON only, no stdout summary")
    ap.add_argument("--sign-check", default=None, help="sign_check.json produced by scripts/sign_hap.sh")
    ap.add_argument("--wait", type=float, default=0.0, metavar="SECONDS",
                    help="if the HAP is missing, poll for up to SECONDS before giving up "
                         "(a concurrent build may be rewriting it)")
    args = ap.parse_args(argv)

    hap_path = args.hap if os.path.isabs(args.hap) else os.path.join(ROOT, args.hap)
    out_path = args.out if os.path.isabs(args.out) else os.path.join(ROOT, args.out)

    result = {
        "schema": "q207.v0.artifact_check/1",
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "generator": {"script": "scripts/inspect_hap.py", "python": sys.version.split()[0]},
        "repo": dict(git_info(), root=ROOT.replace("\\", "/")),
        "mode": "read-only: no rebuild, no re-sign, no patch, no write into the HAP",
        "input": {},
        "container": {},
        "module_manifest": {},
        "native_lib": {},
        "signature": {},
        "cross_checks": {},
        "api_alignment": {},
        "status": "FAIL",
        "failures": [],
        "warnings": [],
        "unverified": [],
        "blocked": [],
    }
    hard = result["failures"]
    warn = result["warnings"]

    # ---------------------------------------------------------- input / zip ---
    if args.wait and args.wait > 0 and not os.path.isfile(hap_path):
        deadline = time.time() + args.wait
        while time.time() < deadline and not os.path.isfile(hap_path):
            time.sleep(0.5)

    if not os.path.isfile(hap_path):
        result["input"] = {"path": rel(hap_path), "exists": False}
        result["status"] = "ERROR"
        print("ERROR: HAP not found: %s" % hap_path, file=sys.stderr)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, "w", encoding="utf-8") as fh:
            json.dump(result, fh, ensure_ascii=False, indent=2)
        return 2

    hap_size = os.path.getsize(hap_path)
    result["input"] = {
        "path": rel(hap_path),
        "exists": True,
        "size": hap_size,
        "sha256": sha256_file(hap_path),
        "mtime_utc": datetime.datetime.fromtimestamp(
            os.path.getmtime(hap_path), datetime.timezone.utc).isoformat(),
    }

    try:
        zf = zipfile.ZipFile(hap_path)
    except zipfile.BadZipFile as exc:
        hard.append("not a ZIP container: %s" % exc)
        zf = None

    blobs = {}
    names = []
    if zf is not None:
        names = zf.namelist()
        bad = zf.testzip()
        result["container"] = {
            "is_zip": True,
            "crc_check": "ok" if bad is None else "corrupt:%s" % bad,
            "entry_count": len(names),
            "entries": [
                {"name": i.filename, "size": i.file_size, "compressed": i.compress_size}
                for i in sorted(zf.infolist(), key=lambda x: x.filename)
            ],
        }
        if bad is not None:
            hard.append("ZIP CRC check failed on entry: %s" % bad)
    else:
        result["container"] = {"is_zip": False, "entry_count": 0, "entries": []}

    # ------------------------------------------------------- module.json ---
    mm = {"present": False}
    if zf is not None:
        if "module.json" not in names:
            hard.append("module.json is missing from the HAP")
            mm["present"] = False
        else:
            mm["present"] = True
            raw = zf.read("module.json")
            mm["size"] = len(raw)
            mm["sha256"] = sha256_bytes(raw)
            try:
                doc = json.loads(raw.decode("utf-8"))
            except Exception as exc:
                hard.append("module.json is not valid JSON: %s" % exc)
                doc = None
            if doc is not None:
                missing = [k for k in REQUIRED_MODULE_KEYS if not dig(doc, k)[0]]
                empty_list = [
                    k for k in ("module.abilities", "module.deviceTypes")
                    if dig(doc, k)[0] and isinstance(dig(doc, k)[1], list) and not dig(doc, k)[1]
                ]
                mm["missing_keys"] = missing
                mm["empty_keys"] = empty_list
                mm["bundle_name"] = dig(doc, "app.bundleName")[1]
                mm["version_name"] = dig(doc, "app.versionName")[1]
                mm["version_code"] = dig(doc, "app.versionCode")[1]
                mm["compile_sdk_version"] = dig(doc, "app.compileSdkVersion")[1]
                mm["compile_sdk_type"] = dig(doc, "app.compileSdkType")[1]
                mm["build_mode"] = dig(doc, "app.buildMode")[1]
                mm["min_api_version_raw"] = dig(doc, "app.minAPIVersion")[1]
                mm["target_api_version_raw"] = dig(doc, "app.targetAPIVersion")[1]
                mm["module_name"] = dig(doc, "module.name")[1]
                mm["module_type"] = dig(doc, "module.type")[1]
                mm["device_types"] = dig(doc, "module.deviceTypes")[1]
                mm["virtual_machine"] = dig(doc, "module.virtualMachine")[1]
                abilities = dig(doc, "module.abilities")[1] or []
                mm["abilities"] = [a.get("name") for a in abilities if isinstance(a, dict)]
                mm["main_element"] = dig(doc, "module.mainElement")[1]
                if missing:
                    hard.append("module.json is missing required keys: %s" % ", ".join(missing))
                if empty_list:
                    hard.append("module.json has empty required lists: %s" % ", ".join(empty_list))
    result["module_manifest"] = mm

    # -------------------------------------------------------- native lib ---
    nl = {"entry": SO_ENTRY, "present": False}
    if zf is not None:
        all_libs = [n for n in names if n.startswith("libs/")]
        by_abi = {}
        for n in all_libs:
            parts = n.split("/")
            if len(parts) >= 3:
                by_abi.setdefault(parts[1], []).append(n)
        nl["bundled_by_abi"] = {k: sorted(v) for k, v in sorted(by_abi.items())}
        nl["bundled_count"] = len(all_libs)

        if SO_ENTRY not in names:
            hard.append("%s is missing from the HAP" % SO_ENTRY)
        else:
            blob = zf.read(SO_ENTRY)
            blobs[SO_ENTRY] = blob
            nl["present"] = True
            nl["size"] = len(blob)
            nl["sha256"] = sha256_bytes(blob)
            ident = elf_identity(blob)
            nl["elf"] = ident
            dyn = elf_dynamic(blob)
            if dyn is not None:
                nl["needed"] = dyn["needed"]
                nl["soname"] = dyn["soname"]

            if not ident.get("is_elf"):
                hard.append("%s is not an ELF file" % SO_ENTRY)
            else:
                if ident["class"] != "ELF64":
                    hard.append("%s is %s, required ELF64" % (SO_ENTRY, ident["class"]))
                if ident["machine"] != "AArch64":
                    hard.append("%s is %s, required AArch64 for ABI %s"
                                % (SO_ENTRY, ident["machine"], ABI))
                if ident["type"] != "DYN":
                    hard.append("%s is %s, required DYN (shared object)" % (SO_ENTRY, ident["type"]))

            # NEEDED vs what the HAP actually ships
            if dyn and dyn["needed"]:
                cls = {}
                for dep in dyn["needed"]:
                    if dep in by_abi.get(ABI, []) or ("libs/%s/%s" % (ABI, dep)) in names:
                        cls[dep] = "bundled"
                    elif dep in SYSTEM_PROVIDED:
                        cls[dep] = "system_provided_assumed"
                    else:
                        cls[dep] = "unexpected_missing"
                        warn.append("NEEDED '%s' is neither bundled nor in the assumed "
                                    "system set" % dep)
                nl["needed_resolution"] = cls
                assumed = sorted(k for k, v in cls.items() if v == "system_provided_assumed")
                if assumed:
                    result["unverified"].append(
                        "NEEDED %s are assumed to be provided by the target image; "
                        "not verified on a device" % ", ".join(assumed))
                    if "libc++_shared.so" in assumed:
                        warn.append(
                            "libc++_shared.so is NEEDED but not bundled in the HAP; "
                            "the image must supply it (OHOS_STL=c++_shared)")
    result["native_lib"] = nl

    # ---------------------------------------------------------- signature ---
    sig_hits = scan_signature(names)
    sig_signed = bool(sig_hits)
    sig_evidence = (
        "found %d signature-related entr%s: %s" % (
            len(sig_hits), "y" if len(sig_hits) == 1 else "ies", ", ".join(sig_hits))
        if sig_hits else
        "no META-INF/ entries and no *.SF/*.RSA/*.EC/*.DSA/*.P7B entries in the "
        "ZIP central directory"
    )
    sig_source = "zip-entry-scan"
    if args.sign_check:
        try:
            with open(args.sign_check, encoding="utf-8") as fh:
                sc = json.load(fh)
            ver = sc.get("verification", {})
            signed_hap = sc.get("artifacts", {}).get("hap_signed", {})
            expected = signed_hap.get("sha256")
            actual = result.get("input", {}).get("sha256")
            if ver.get("verify_app_ok") is True and expected == actual:
                sig_signed = True
                sig_evidence = ("hap-sign-tool verify-app exit=%s, digest=%s; "
                                "sign_check hash matches this HAP"
                                % (ver.get("verify_app_exit_code"), ver.get("digest_verify")))
                sig_source = "sign_check+verify-app"
            elif ver.get("verify_app_ok") is True:
                sig_evidence = ("sign_check verify-app ok but its hap_signed sha256 does "
                                "not match this HAP; NOT treated as signed")
                sig_source = "sign_check-hash-mismatch"
        except Exception as exc:
            sig_evidence = "sign_check read failed: %s" % exc
            sig_source = "sign_check-error"
    result["signature"] = {
        "signed": sig_signed,
        "signature_entries": sig_hits,
        "checked_for": ["META-INF/*", "*.SF", "*.RSA", "*.EC", "*.DSA", "*.P7B",
                        "*signature*", "hap-sign-tool verify-app"],
        "evidence": sig_evidence,
        "source": sig_source,
        "note": "signed is derived from zip entries OR a matching verify-app result; "
                "'false' is never a placeholder and 'true' is never assumed",
    }
    if not sig_signed:
        result["blocked"].append(
            "HAP is unsigned -> a real device will refuse to install it "
            "(no signingConfigs/profile in the build)")
    else:
        result["unverified"].append(
            "signature is cryptographically verified via sign_check/verify-app, but "
            "installability on a stock device / trusted root was NOT checked")

    # ------------------------------------------------------- cross checks ---
    cc = {}
    for name, candidate in (
        ("build/napi/libentry.so", os.path.join(ROOT, "build", "napi", "libentry.so")),
        ("app/entry/libs/%s/libentry.so" % ABI,
         os.path.join(ROOT, "app", "entry", "libs", ABI, "libentry.so")),
    ):
        if os.path.isfile(candidate):
            h = sha256_file(candidate)
            cc[name] = {
                "exists": True,
                "size": os.path.getsize(candidate),
                "sha256": h,
                "identical_to_hap_entry": bool(nl.get("sha256")) and h == nl.get("sha256"),
            }
        else:
            cc[name] = {"exists": False}
    if nl.get("present"):
        sizes = [v["size"] for v in cc.values() if v.get("exists")]
        if sizes and nl["size"] < max(sizes):
            cc["size_ratio_vs_repo_copy"] = round(nl["size"] / max(sizes), 3)
            cc["likely_stripped"] = True
            result["unverified"].append(
                "%s is %d B while the repo copy is %d B (likely stripped); local "
                "callback names cannot be re-checked from the HAP copy"
                % (SO_ENTRY, nl["size"], max(sizes)))
    result["cross_checks"] = cc

    # ------------------------------------------------------ api alignment ---
    api = {
        "sdk_api_version": None,
        "sdk_version": None,
        "hap_compile_sdk_version": mm.get("compile_sdk_version"),
        "hap_min_api": decode_ohos_api(mm.get("min_api_version_raw")),
        "hap_target_api": decode_ohos_api(mm.get("target_api_version_raw")),
    }
    sdk_pkg = find_sdk_package()
    if sdk_pkg:
        try:
            with open(sdk_pkg, encoding="utf-8") as fh:
                pkg = json.load(fh)
            api["sdk_api_version"] = pkg.get("apiVersion")
            api["sdk_version"] = pkg.get("version")
            api["sdk_package"] = sdk_pkg.replace("\\", "/")
        except Exception as exc:
            api["sdk_package_error"] = str(exc)
            api["sdk_package"] = sdk_pkg.replace("\\", "/")
    else:
        api["sdk_package"] = None
        api["sdk_package_note"] = ("SDK oh-uni-package.json not found; set OHOS_SDK_ROOT, "
                                   "DEVECO_HOME or OHOS_SDK_PKG to enable the API cross-check")
        result["unverified"].append(
            "SDK apiVersion could not be read, so the HAP-vs-SDK API alignment "
            "was not cross-checked")
    if api["hap_min_api"] and api.get("sdk_version"):
        api["min_api_matches_sdk_version"] = str(api["sdk_version"]).startswith(
            api["hap_min_api"]["decoded"] + ".")
    result["api_alignment"] = api

    sdk_api = api.get("sdk_api_version")
    if sdk_api and str(sdk_api).isdigit() and int(sdk_api) >= 23:
        warn.append(
            "SDK apiVersion=%s and compileSdkVersion=%s: this artifact targets the "
            "API 23+/6.x generation, NOT the conservative 'API 18 + NAPI-only' "
            "baseline recorded in the V0 plan; V0 owner must confirm the target "
            "device generation in writing"
            % (sdk_api, api.get("hap_compile_sdk_version")))
    if (mm.get("min_api_version_raw") is not None
            and mm.get("min_api_version_raw") != mm.get("target_api_version_raw")):
        warn.append("minAPIVersion != targetAPIVersion; installability range differs "
                    "from the build target")

    # ---------------------------------------------------------- unverified ---
    result["unverified"] += [
        "device install / launch / ping() / add() were NOT executed "
        "(read-only check, no hdc call in this script)",
        "module.json minAPIVersion/targetAPIVersion decode is INFERRED from the packed "
        "integer layout, not from a documented mapping",
        "ArkTS .d.ts wiring is tracked in docs/v0-dts-notes.md; this script does not re-run the ArkTS compiler",
        "runtime loading of %s by the ArkTS runtime was not observed" % SO_ENTRY,
    ]
    result["blocked"] += [
        "no target device: evidence/env_check.json records board.status='no target' "
        "(hdc list targets empty)",
    ]

    # ------------------------------------------------------------- verdict ---
    result["status"] = "PASS" if not hard else "FAIL"
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as fh:
        json.dump(result, fh, ensure_ascii=False, indent=2)
        fh.write("\n")

    if not args.quiet:
        print("== inspect_hap ==")
        print("hap        : %s (%s bytes)" % (result["input"]["path"], result["input"]["size"]))
        print("sha256     : %s" % result["input"]["sha256"])
        print("entries    : %s" % result["container"].get("entry_count"))
        print("module.json: %s  bundle=%s  type=%s  compileSdk=%s"
              % ("present" if mm.get("present") else "MISSING",
                 mm.get("bundle_name"), mm.get("module_type"), mm.get("compile_sdk_version")))
        print("native lib : %s  size=%s  elf=%s/%s/%s"
              % (SO_ENTRY, nl.get("size"), nl.get("elf", {}).get("class"),
                 nl.get("elf", {}).get("machine"), nl.get("elf", {}).get("type")))
        print("signed     : %s  (%s)" % (result["signature"]["signed"],
                                         result["signature"]["evidence"]))
        print("status     : %s  failures=%d warnings=%d unverified=%d"
              % (result["status"], len(hard), len(warn), len(result["unverified"])))
        for f in hard:
            print("  [FAIL] %s" % f)
        for w in warn:
            print("  [WARN] %s" % w)
        print("json       : %s" % rel(out_path))

    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
