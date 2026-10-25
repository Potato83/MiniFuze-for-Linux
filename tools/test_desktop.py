#!/usr/bin/env python3
"""Exercise real GTK widgets against an isolated engine, never physical routes.

Python is a development dependency only. Needs a working display and PipeWire;
in CI use dbus-run-session + xvfb-run and --start-pipewire on a host without ALSA.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--start-pipewire', action='store_true')
    parser.add_argument('--binary', type=Path, default=ROOT/'build/desktop-smoke')
    args = parser.parse_args()
    folder = Path(tempfile.mkdtemp(prefix='minifuze-desktop-test-'))
    processes, files = [], []
    env = dict(os.environ, G_DEBUG='fatal-warnings')
    config = folder/'config'
    config.mkdir(mode=0o700)
    env['XDG_CONFIG_HOME'] = str(config)
    report = {'ok': False, 'folder': str(folder)}

    def start(argv, name):
        log = (folder/name).open('w')
        files.append(log)
        p = subprocess.Popen(argv, env=env, stdout=log, stderr=subprocess.STDOUT)
        processes.append(p)
        return p

    try:
        if args.start_pipewire:
            if list(Path('/dev/snd').glob('controlC*')):
                raise RuntimeError('CI server startup requires a host without ALSA hardware')
            runtime = folder/'runtime'
            runtime.mkdir(mode=0o700)
            env['XDG_RUNTIME_DIR'] = str(runtime)
            env['PIPEWIRE_RUNTIME_DIR'] = str(runtime)
            start(['pipewire'], 'pipewire.log')
            deadline = time.monotonic()+10
            while not (runtime/'pipewire-0').exists():
                if time.monotonic()>deadline:
                    raise RuntimeError('Private PipeWire did not start')
                time.sleep(.1)
            start(['wireplumber'], 'wireplumber.log')
            time.sleep(1)
        ready = folder/'ready.json'
        engine = start([str(ROOT/'build/minifuze2-engine'), 'panel',
                        '--prefix', 'mf_desktop_test_'+str(os.getpid()),
                        '--state', str(folder/'state.json'),
                        '--session-state', str(folder/'working.json'),
                        '--ready-file', str(ready)], 'engine.log')
        deadline = time.monotonic()+25
        while not ready.exists():
            if engine.poll() is not None:
                raise RuntimeError('Private engine exited')
            if time.monotonic()>deadline:
                raise RuntimeError('Engine readiness timeout')
            time.sleep(.1)
        url = json.loads(ready.read_text())['url']
        gui = start([str(args.binary.resolve()), url, str(folder/'gtk.json'),
                     str(folder/'desktop.png'), str(ROOT/'tests/fixtures/mf2-factory.mfp')], 'gtk.log')
        gui.wait(timeout=90)
        report['exit_code'] = gui.returncode
        report['gtk'] = json.loads((folder/'gtk.json').read_text()) if (folder/'gtk.json').exists() else None
        report['engine_kept_running_after_window_closed'] = engine.poll() is None
        if engine.poll() is None:
            state = json.load(urllib.request.urlopen(url+'/api/state', timeout=5))
            report['no_physical_routes'] = state['hardware'] is None
        report['ok'] = (gui.returncode == 0 and bool(report['gtk'])
                        and report['engine_kept_running_after_window_closed']
                        and report.get('no_physical_routes', False))
    except Exception as error:
        report['error'] = str(error)
    finally:
        for p in reversed(processes):
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    p.kill(); p.wait()
        for f in files:
            f.close()
        report['gtk_log'] = (folder/'gtk.log').read_text()[-4000:] if (folder/'gtk.log').exists() else ''
        destination = args.report or folder/'report.json'
        if destination.exists():
            raise RuntimeError('Retain previous test report: '+str(destination))
        destination.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
