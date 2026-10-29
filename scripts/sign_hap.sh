#!/usr/bin/env bash
# scripts/sign_hap.sh — local self-signed signing for the V0 unsigned HAP.
#
# What it does (no device, no network, no Android/Harmony SDK install):
#   1. generate a throw-away local PKI in ./signing/  (root CA -> app sub-CA -> app leaf,
#      root CA -> profile sub-CA -> profile leaf) using DevEco's hap-sign-tool.jar;
#   2. build a *release-type* Provision Profile for bundle com.example.q207 and sign it;
#   3. sign app/entry/build/default/outputs/default/entry-default-unsigned.hap
#      -> entry-default-signed.hap (same directory);
#   4. verify the profile and the signed HAP, and write evidence/sign_check.json.
#
# IMPORTANT: the CA is self-signed and the profile is not issued by OpenHarmony.
# The resulting HAP is *signature-verifiable* but NOT installable on a real device:
# a stock device only trusts the OpenHarmony release roots, and the debug path would
# additionally need the device UDID. We therefore record installable=false.
#
# Usage:
#   bash scripts/sign_hap.sh            # idempotent: reuses signing/q207-local.p12 if present
#   bash scripts/sign_hap.sh --force    # wipe ./signing and regenerate all materials
#   UNSIGNED_HAP=... SIGNED_HAP=... bash scripts/sign_hap.sh
#
# Exit codes: 0 = signed + verified (not installable), 2 = prerequisites missing,
#             3 = signing/verification failed (details in signing/logs + evidence/sign_check.json)
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EVIDENCE="$ROOT/evidence"
SIGNING="${SIGNING_DIR:-$ROOT/signing}"
CERTS="$SIGNING/certs"
PROFILE_DIR="$SIGNING/profile"
LOGS="$SIGNING/logs"
VERIFY="$SIGNING/verify"

# DevEco Studio bundled JBR + hap-sign-tool (no extra install; nothing is started on a device).
DEVECO="${DEVECO_HOME:-/d/Program Files/Huawei/DevEco Studio}"
JAVA="$DEVECO/jbr/bin/java.exe"
SIGN_JAR="$DEVECO/sdk/default/openharmony/toolchains/lib/hap-sign-tool.jar"

UNSIGNED_HAP="${UNSIGNED_HAP:-$ROOT/app/entry/build/default/outputs/default/entry-default-unsigned.hap}"
SIGNED_HAP="${SIGNED_HAP:-$(dirname "$UNSIGNED_HAP")/entry-default-signed.hap}"

BUNDLE_NAME="com.example.q207"
COMPATIBLE_VERSION=24        # matches app/build-profile.json5 compatibleSdkVersion 6.1.1(24)
KEYSTORE="$SIGNING/q207-local.p12"
STORE_PWD="Q207Local#2026"   # local test value only; never used for a real release
KEY_PWD="Q207Local#2026"

ROOT_SUBJECT="C=CN,O=Q207,OU=Local Dev,CN=Q207 Local Root CA"
APP_CA_SUBJECT="C=CN,O=Q207,OU=Local Dev,CN=Q207 App Signing CA"
PROFILE_CA_SUBJECT="C=CN,O=Q207,OU=Local Dev,CN=Q207 Profile Signing CA"
APP_SUBJECT="C=CN,O=Q207,OU=Local Dev,CN=Q207 App Release"
PROFILE_SUBJECT="C=CN,O=Q207,OU=Local Dev,CN=Q207 Profile Release"

ALIAS_ROOT="q207-root-ca"
ALIAS_APP_CA="q207-app-sub-ca"
ALIAS_PROFILE_CA="q207-profile-sub-ca"
ALIAS_APP="q207-app-key"
ALIAS_PROFILE="q207-profile-key"

ROOT_CER="$CERTS/q207-root-ca.cer"
APP_CA_CER="$CERTS/q207-app-sub-ca.cer"
PROFILE_CA_CER="$CERTS/q207-profile-sub-ca.cer"
APP_CERT="$CERTS/q207-app-cert.pem"
PROFILE_CERT="$CERTS/q207-profile-cert.pem"
PROFILE_JSON="$PROFILE_DIR/q207-profile.json"
PROFILE_P7B="$PROFILE_DIR/q207-profile.p7b"

RECORDS="$LOGS/records.tsv"
FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

# --- helpers ---------------------------------------------------------------
# MSYS2 would rewrite POSIX-looking arguments; wrap them as Windows paths instead.
wp() { printf '%s' "$1" | sed -E 's|^/([a-zA-Z])/|\U\1:/|; s|/|\\|g'; }
fsize() { wc -c <"$1" | tr -d '[:space:]'; }   # 'stat' is not present in this MSYS2 env
export MSYS2_ARG_CONV_EXCL='*'

die() { echo "FAIL: $*" >&2; exit 2; }

# run_step <name> <log> <cmd...> ; records exit code, never aborts
run_step() {
  local name="$1" log="$2"; shift 2
  "$@" >"$log" 2>&1
  local ec=$?
  printf '%s\t%s\t%s\n' "$name" "$ec" "$(basename "$log")" >>"$RECORDS"
  if [ "$ec" -ne 0 ]; then
    echo "  ! $name failed (exit $ec) — see $log"
  else
    echo "  - $name ok"
  fi
  return "$ec"
}

# --- preflight -------------------------------------------------------------
echo "== Q2-07 V0 local self-signed HAP =="
[ -f "$JAVA" ] || die "java not found: $JAVA"
[ -f "$SIGN_JAR" ] || die "hap-sign-tool.jar not found: $SIGN_JAR"
[ -f "$UNSIGNED_HAP" ] || die "unsigned HAP not found: $UNSIGNED_HAP (run scripts/build.sh first)"

if [ "$FORCE" = "1" ]; then
  echo "  --force: removing generated signing artifacts (keeps signing/README.md)"
  rm -rf "$CERTS" "$PROFILE_DIR" "$LOGS" "$VERIFY" "$KEYSTORE"
fi
mkdir -p "$CERTS" "$PROFILE_DIR" "$LOGS" "$VERIFY" "$EVIDENCE"
: >"$RECORDS"

JAVA_W="$(wp "$JAVA")"
JAR_W="$(wp "$SIGN_JAR")"
KS_W="$(wp "$KEYSTORE")"

sign_tool() { "$JAVA_W" -jar "$JAR_W" "$@"; }

echo "  java      : $JAVA_W"
echo "  tool      : $JAR_W"
echo "  unsigned  : $UNSIGNED_HAP ($(fsize "$UNSIGNED_HAP") bytes)"
echo "  signed    : $SIGNED_HAP"

# --- 1. local PKI (only when missing) -------------------------------------
if [ ! -f "$KEYSTORE" ]; then
  echo "-- 1/5 generating local root CA + sub CAs + leaf keys"
  for alias in "$ALIAS_ROOT" "$ALIAS_APP_CA" "$ALIAS_PROFILE_CA" "$ALIAS_APP" "$ALIAS_PROFILE"; do
    run_step "generate-keypair:$alias" "$LOGS/keypair-$alias.log" \
      sign_tool generate-keypair -keyAlias "$alias" -keyPwd "$KEY_PWD" \
      -keyAlg ECC -keySize NIST-P-256 -keystoreFile "$KS_W" -keystorePwd "$STORE_PWD" || exit 3
  done
  run_step "generate-cert:root" "$LOGS/cert-root.log" \
    sign_tool generate-cert -keyAlias "$ALIAS_ROOT" -keyPwd "$KEY_PWD" \
    -issuer "$ROOT_SUBJECT" -issuerKeyAlias "$ALIAS_ROOT" -issuerKeyPwd "$KEY_PWD" \
    -subject "$ROOT_SUBJECT" -validity 3650 \
    -keyUsage certificateSignature,crlSignature \
    -basicConstraints true -basicConstraintsCa true -basicConstraintsPathLen 2 \
    -signAlg SHA256withECDSA -keystoreFile "$KS_W" -keystorePwd "$STORE_PWD" \
    -outFile "$(wp "$ROOT_CER")" || exit 3
  for pair in "app:$ALIAS_APP_CA:$APP_CA_SUBJECT:$APP_CA_CER" \
              "profile:$ALIAS_PROFILE_CA:$PROFILE_CA_SUBJECT:$PROFILE_CA_CER"; do
    IFS=':' read -r tag alias subject out <<<"$pair"
    run_step "generate-cert:$tag-ca" "$LOGS/cert-$tag-ca.log" \
      sign_tool generate-cert -keyAlias "$alias" -keyPwd "$KEY_PWD" \
      -issuer "$ROOT_SUBJECT" -issuerKeyAlias "$ALIAS_ROOT" -issuerKeyPwd "$KEY_PWD" \
      -subject "$subject" -validity 1825 \
      -keyUsage certificateSignature,crlSignature \
      -basicConstraints true -basicConstraintsCa true -basicConstraintsPathLen 1 \
      -signAlg SHA256withECDSA -keystoreFile "$KS_W" -keystorePwd "$STORE_PWD" \
      -outFile "$(wp "$out")" || exit 3
  done
  run_step "generate-app-cert" "$LOGS/cert-app-leaf.log" \
    sign_tool generate-app-cert -keyAlias "$ALIAS_APP" -keyPwd "$KEY_PWD" \
    -issuer "$APP_CA_SUBJECT" -issuerKeyAlias "$ALIAS_APP_CA" -issuerKeyPwd "$KEY_PWD" \
    -subject "$APP_SUBJECT" -validity 365 -signAlg SHA256withECDSA \
    -rootCaCertFile "$(wp "$ROOT_CER")" -subCaCertFile "$(wp "$APP_CA_CER")" \
    -keystoreFile "$KS_W" -keystorePwd "$STORE_PWD" \
    -outForm certChain -outFile "$(wp "$APP_CERT")" || exit 3
  run_step "generate-profile-cert" "$LOGS/cert-profile-leaf.log" \
    sign_tool generate-profile-cert -keyAlias "$ALIAS_PROFILE" -keyPwd "$KEY_PWD" \
    -issuer "$PROFILE_CA_SUBJECT" -issuerKeyAlias "$ALIAS_PROFILE_CA" -issuerKeyPwd "$KEY_PWD" \
    -subject "$PROFILE_SUBJECT" -validity 365 -signAlg SHA256withECDSA \
    -rootCaCertFile "$(wp "$ROOT_CER")" -subCaCertFile "$(wp "$PROFILE_CA_CER")" \
    -keystoreFile "$KS_W" -keystorePwd "$STORE_PWD" \
    -outForm certChain -outFile "$(wp "$PROFILE_CERT")" || exit 3
else
  echo "-- 1/5 reusing existing local PKI: $KEYSTORE"
fi

# --- 2. provision profile (release type; no device UDID needed) ------------
echo "-- 2/5 building profile JSON (type=release, bundle=$BUNDLE_NAME)"
python - "$(wp "$APP_CERT")" "$(wp "$PROFILE_JSON")" "$BUNDLE_NAME" <<'PY' || exit 3
import json, re, sys, time, uuid
cert_chain, out_json, bundle = sys.argv[1], sys.argv[2], sys.argv[3]
pem = open(cert_chain, encoding="utf-8").read()
certs = re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----\n?", pem, re.S)
if len(certs) < 1:
    sys.exit("no certificate found in %s" % cert_chain)
now = int(time.time())
profile = {
    "version-name": "2.0.0",
    "version-code": 2,
    "app-distribution-type": "os_integration",
    "uuid": str(uuid.uuid4()),
    "validity": {"not-before": now - 3600, "not-after": now + 300 * 86400},
    "type": "release",
    "bundle-info": {
        "developer-id": "Q207Local",
        "distribution-certificate": certs[0],
        "bundle-name": bundle,
        "apl": "normal",
        "app-feature": "hos_normal_app",
    },
    "acls": {"allowed-acls": [""]},
    "permissions": {"restricted-permissions": []},
    "issuer": "pki_internal",
}
json.dump(profile, open(out_json, "w", encoding="utf-8"), ensure_ascii=False, indent=4)
print("  profile json: certs_in_chain=%d bundle=%s type=%s" % (len(certs), bundle, profile["type"]))
PY

# --- 3. sign profile -------------------------------------------------------
echo "-- 3/5 signing profile -> $(basename "$PROFILE_P7B")"
run_step "sign-profile" "$LOGS/sign-profile.log" \
  sign_tool sign-profile -mode localSign -keyAlias "$ALIAS_PROFILE" -keyPwd "$KEY_PWD" \
  -profileCertFile "$(wp "$PROFILE_CERT")" -inFile "$(wp "$PROFILE_JSON")" \
  -signAlg SHA256withECDSA -keystoreFile "$KS_W" -keystorePwd "$STORE_PWD" \
  -outFile "$(wp "$PROFILE_P7B")" || exit 3

run_step "verify-profile" "$LOGS/verify-profile.log" \
  sign_tool verify-profile -inFile "$(wp "$PROFILE_P7B")" \
  -outFile "$(wp "$VERIFY/profile_verify.json")" || true

# --- 4. sign HAP -----------------------------------------------------------
echo "-- 4/5 signing HAP -> $(basename "$SIGNED_HAP")"
mkdir -p "$(dirname "$SIGNED_HAP")"
run_step "sign-app" "$LOGS/sign-app.log" \
  sign_tool sign-app -mode localSign -keyAlias "$ALIAS_APP" -keyPwd "$KEY_PWD" \
  -appCertFile "$(wp "$APP_CERT")" -profileFile "$(wp "$PROFILE_P7B")" \
  -inFile "$(wp "$UNSIGNED_HAP")" -signAlg SHA256withECDSA \
  -keystoreFile "$KS_W" -keystorePwd "$STORE_PWD" -outFile "$(wp "$SIGNED_HAP")" \
  -compatibleVersion "$COMPATIBLE_VERSION" -signCode 1 || exit 3

# --- 5. verify HAP ---------------------------------------------------------
echo "-- 5/5 verifying signed HAP"
run_step "verify-app" "$LOGS/verify-app.log" \
  sign_tool verify-app -inFile "$(wp "$SIGNED_HAP")" \
  -outCertChain "$(wp "$VERIFY/verified-certchain.cer")" \
  -outProfile "$(wp "$VERIFY/verified-profile.p7b")"
VERIFY_APP_EXIT=$?

# --- evidence --------------------------------------------------------------
python - \
  "$(wp "$ROOT")" "$(wp "$EVIDENCE/sign_check.json")" "$(wp "$RECORDS")" \
  "$(wp "$UNSIGNED_HAP")" "$(wp "$SIGNED_HAP")" "$(wp "$PROFILE_P7B")" \
  "$(wp "$PROFILE_JSON")" "$(wp "$PROFILE_CERT")" "$(wp "$APP_CERT")" \
  "$(wp "$ROOT_CER")" "$(wp "$VERIFY/profile_verify.json")" \
  "$BUNDLE_NAME" "$COMPATIBLE_VERSION" "$VERIFY_APP_EXIT" <<'PY'
import hashlib, json, os, subprocess, sys, time, zipfile

(root, out_json, records, unsigned, signed, p7b, profile_json, profile_cert,
 app_cert, root_cert, profile_verify_path, bundle, compat_ver, verify_app_exit) = sys.argv[1:15]
verify_app_exit = int(verify_app_exit)

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def art(path, **extra):
    if not os.path.isfile(path):
        return None
    name = os.path.basename(path)
    # evidence paths are repo-relative and POSIX-style
    rel = os.path.relpath(path, root).replace("\\", "/")
    d = {"path": rel, "size": os.path.getsize(path), "sha256": sha256(path)}
    d.update(extra)
    return d

commands = []
if os.path.isfile(records):
    for line in open(records, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line:
            continue
        name, ec, log = (line.split("\t") + ["", ""])[:3]
        commands.append({"command": name, "exit_code": int(ec),
                         "log": "signing/logs/" + log})

profile_verify = {}
if os.path.isfile(profile_verify_path):
    try:
        profile_verify = json.load(open(profile_verify_path, encoding="utf-8"))
    except Exception:
        profile_verify = {}

entries = None
if os.path.isfile(signed):
    try:
        with zipfile.ZipFile(signed) as zf:
            entries = len(zf.namelist())
    except Exception:
        entries = None

try:
    commit = subprocess.run(["git", "-C", root, "rev-parse", "--short", "HEAD"],
                            capture_output=True, text=True, timeout=20).stdout.strip()
    branch = subprocess.run(["git", "-C", root, "rev-parse", "--abbrev-ref", "HEAD"],
                            capture_output=True, text=True, timeout=20).stdout.strip()
except Exception:
    commit, branch = "", ""

sign_ok = bool(os.path.isfile(signed)) and verify_app_exit == 0
status = "PASS-SIGNED-NOT-INSTALLABLE" if sign_ok else "FAIL"

blocked = [
    "install/verify-with-system-signature not possible: no board/device (no UDID, no hdc target)",
    "HAP signature is anchor-less for a stock device: local self-signed CA is not in the "
    "OpenHarmony trusted root store, so a real device would reject the profile/cert chain",
]
if not sign_ok:
    blocked.insert(0, "signing or verify-app failed (see signing/logs/)")

doc = {
    "slice": "V0-sign: local self-signed HAP (no device, no install)",
    "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    "branch": branch,
    "commit": commit,
    "status": status,
    "signing": {
        "tool": "<DevEco>/sdk/default/openharmony/toolchains/lib/hap-sign-tool.jar",
        "java": "<DevEco>/jbr/bin/java.exe (JBR 21)",
        "sign_alg": "SHA256withECDSA",
        "key_alg": "ECC NIST-P-256",
        "ca": "self-signed local root -> app sub-CA / profile sub-CA -> leaf (3-level chain)",
        "keystore": "signing/q207-local.p12 (contains PRIVATE KEYS; git-ignored, local test only)",
        "profile_type": "release",
        "profile_issuer": "pki_internal (local value; NOT an OpenHarmony-issued profile)",
        "bundle_name": bundle,
        "compatible_version": int(compat_ver),
        "sign_code": True,
        "store_password_source": "hard-coded local test value in scripts/sign_hap.sh "
                                 "(never used for a real release)",
    },
    "commands": commands,
    "artifacts": {
        "hap_unsigned": art(unsigned, signed=False),
        "hap_signed": art(signed, signed=True, installable=False),
        "profile_signed": art(p7b),
        "profile_json": art(profile_json),
        "app_cert_chain": art(app_cert),
        "profile_cert_chain": art(profile_cert),
        "root_ca": art(root_cert),
    },
    "signed_hap_zip_entries": entries,
    "verification": {
        "verify_app_exit_code": verify_app_exit,
        "verify_app_ok": verify_app_exit == 0,
        "verify_app_log": "signing/logs/verify-app.log",
        "verify_app_cert_chain_out": "signing/verify/verified-certchain.cer",
        "verify_app_profile_out": "signing/verify/verified-profile.p7b",
        "verify_profile_passed": bool(profile_verify.get("verifiedPassed")),
        "verify_profile_message": profile_verify.get("message", ""),
        "digest_verify": "SHA-256 (reported by verify-app)",
    },
    "installable": False,
    "installable_reason": "self-signed CA (not in device trust store) + no device UDID for a "
                          "debug profile; release-type profile signed locally only",
    "blocked": blocked,
    "next_materials_needed": [
        "OpenHarmony-issued app signing cert chain + Provision Profile (release) for the "
        "issuer/developer-id used by the target device image",
        "OR a debug profile signed by the official debug CA with this device's UDID "
        "(hdc shell bm get -u) to install a debug HAP",
        "a target board/flash image to run 'hdc install' and verify on-device acceptance",
    ],
    "notes": "Signed HAP is signature-verifiable locally (verify-app exit 0) but is NOT "
             "installed and NOT claimed installable. No device was touched. Private key "
             "material stays in signing/ and is git-ignored.",
}

json.dump(doc, open(out_json, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
print("  wrote %s" % out_json)
print("  signed_ok=%s verify_app_exit=%s installable=%s"
      % (sign_ok, verify_app_exit, doc["installable"]))
PY

WRITE_EXIT=$?

echo
echo "== summary =="
echo "signed_hap      = $SIGNED_HAP"
[ -f "$SIGNED_HAP" ] && echo "signed_hap_size = $(fsize "$SIGNED_HAP")"
echo "verify_app_exit = $VERIFY_APP_EXIT"
echo "installable     = false (self-signed CA + no device UDID/profile)"
echo "evidence        = $EVIDENCE/sign_check.json"
echo "logs            = $LOGS"

[ "$WRITE_EXIT" -eq 0 ] || exit 3
[ "$VERIFY_APP_EXIT" -eq 0 ] || exit 3
exit 0
