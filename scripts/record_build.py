#!/usr/bin/env python3
"""Write evidence/v0_build.json without inventing facts."""
import argparse, datetime, hashlib, json, os, subprocess

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--status', required=True, choices=['PASS', 'PASS-UNSIGNED', 'PARTIAL', 'FAIL'])
    ap.add_argument('--so', action='append', default=[])
    ap.add_argument('--hap', action='append', default=[])
    ap.add_argument('--hap-status', required=True)
    ap.add_argument('--hdc-status', required=True)
    ap.add_argument('--command', action='append', default=[])
    ap.add_argument('--blocked', action='append', default=[])
    ap.add_argument('--log-dir', default='')
    args = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, 'evidence', 'v0_build.json')
    os.makedirs(os.path.dirname(out), exist_ok=True)

    try:
        branch = subprocess.check_output(['git', 'branch', '--show-current'], cwd=root, text=True).strip()
    except Exception:
        branch = 'unknown'
    try:
        commit = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD'], cwd=root, text=True).strip()
    except Exception:
        commit = 'unknown'

    artifacts = []
    for p in args.so:
        apath = p if os.path.isabs(p) else os.path.join(root, p)
        if os.path.exists(apath):
            artifacts.append({
                'path': os.path.relpath(apath, root).replace('\\', '/'),
                'size': os.path.getsize(apath),
                'sha256': sha256(apath),
            })
        else:
            artifacts.append({'path': p, 'missing': True})

    hap_artifacts = []
    for p in args.hap:
        apath = p if os.path.isabs(p) else os.path.join(root, p)
        if os.path.exists(apath):
            hap_artifacts.append({'path': os.path.relpath(apath, root).replace('\\', '/'), 'size': os.path.getsize(apath), 'sha256': sha256(apath), 'signed': False})
        else:
            hap_artifacts.append({'path': p, 'missing': True})

    commands = []
    for item in args.command:
        parts = item.split('|', 2)
        rec = {'command': parts[0], 'exit_code': int(parts[1]) if len(parts) > 1 and parts[1].lstrip('-').isdigit() else None}
        if len(parts) > 2:
            rec['log'] = parts[2]
        commands.append(rec)

    data = {
        'slice': 'V0-min: NAPI .so + ArkTS caller',
        'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'branch': branch,
        'commit': commit,
        'status': args.status,
        'commands': commands,
        'artifacts': {'so': artifacts, 'hap': hap_artifacts},
        'hap_status': args.hap_status,
        'hdc_status': args.hdc_status,
        'blocked': args.blocked,
        'notes': 'No device run. No installation result is claimed. HAP status is recorded independently.',
    }
    with open(out, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    print('wrote', out)
    print(json.dumps({'status': data['status'], 'so': len(artifacts), 'hap_status': data['hap_status'], 'hdc_status': data['hdc_status']}, ensure_ascii=False))

if __name__ == '__main__':
    main()
