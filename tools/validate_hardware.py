#!/usr/bin/env python3
"""Opt-in MiniFuse acceptance on the user's PipeWire server.

Uses its own engine, state files and low-level generated tones. Leaves default
devices, forced clock settings and phantom power untouched. An optional analog
return requires OUTPUT L -> INPUT 2, MIX at USB, GAIN 2 minimum and INST 2 off.
Only numeric measurements are retained; captured audio stays in memory.
"""
import argparse
import array
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
RATE = 48000
FREQUENCIES = (997, 1237, 1499, 1741)
AMPLITUDE = .005


def command(argv):
    return subprocess.check_output(list(map(str, argv)), text=True, timeout=15)


def snapshot():
    return json.loads(command(['pw-dump']))


def props(obj):
    return obj.get('info', {}).get('props', {})


def defaults():
    return {kind: command(['pactl', 'get-default-'+kind]).strip()
            for kind in ('sink', 'source')}


def clock_settings():
    text = command(['pw-metadata', '-n', 'settings'])
    return {key: value for key, value in re.findall(
        r"key:'(clock\.force-(?:rate|quantum))' value:'([^']*)'", text)}


class Probe:
    def __init__(self, args, folder):
        self.args, self.folder = args, folder
        self.prefix = 'mf_hardware_test_'+str(os.getpid())
        self.engine, self.players, self.files = None, [], []
        self.original_switches = None
        self.analog_noise_rms = 0
        self.report = {'version': (ROOT/'VERSION').read_text().strip(),
                       'kernel': os.uname().release, 'rate': RATE,
                       'probe_amplitude_dbfs': 20*math.log10(AMPLITUDE),
                       'analog_return': args.analog_return,
                       'phantom_power_changed': False, 'stages': []}
        self.basis = [(tuple(math.cos(2*math.pi*hz*i/RATE) for i in range(RATE//2)),
                       tuple(math.sin(2*math.pi*hz*i/RATE) for i in range(RATE//2)))
                      for hz in FREQUENCIES]

    def start(self):
        ready = self.folder/'ready.json'
        ready.unlink(missing_ok=True)
        log = (self.folder/'engine.log').open('a')
        self.files.append(log)
        self.engine = subprocess.Popen([str(ROOT/'build/minifuze2-engine'), 'panel',
            '--prefix', self.prefix, '--state', str(self.folder/'saved.json'),
            '--session-state', str(self.folder/'working.json'),
            '--ready-file', str(ready), '--ctl', self.args.ctl],
            stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic()+25
        while not ready.exists():
            if self.engine.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError('Acceptance engine did not become ready')
            time.sleep(.1)
        self.url = json.loads(ready.read_text())['url']
        page = urllib.request.urlopen(self.url, timeout=10).read()
        self.token = re.search(rb'name="csrf-token" content="([a-f0-9]{64})"', page)[1].decode()

    def stop_engine(self):
        if self.engine and self.engine.poll() is None:
            self.engine.terminate()
            try:
                self.engine.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.engine.kill()
                self.engine.wait(timeout=5)
                raise RuntimeError('Acceptance engine shutdown timed out')
            if self.engine.returncode != 0:
                raise RuntimeError('Acceptance engine exited unsuccessfully')
        self.engine = None

    def get(self):
        return json.load(urllib.request.urlopen(self.url+'/api/state', timeout=15))

    def post(self, path, value=None):
        request = urllib.request.Request(self.url+'/api/'+path,
            data=json.dumps(value if value is not None else {}, allow_nan=False).encode(),
            headers={'Origin': self.url, 'Content-Type': 'application/json',
                     'X-CSRF-Token': self.token})
        with urllib.request.urlopen(request, timeout=20) as response:
            return json.load(response)

    def wait_connected(self):
        deadline = time.monotonic()+12
        while time.monotonic() < deadline:
            state = self.get()
            if state['hardware'] and state['audio']['available']:
                return state
            time.sleep(.15)
        raise RuntimeError('Hardware route did not recover')

    def play(self):
        for index, endpoint in enumerate(('main', 'virtual34')):
            signal = array.array('f')
            for frame in range(RATE):
                signal.extend(AMPLITUDE*math.sin(2*math.pi*hz*frame/RATE)
                              for hz in FREQUENCIES[2*index:2*index+2])
            path = self.folder/(endpoint+'.raw')
            with path.open('wb') as output:
                for _ in range(90):
                    output.write(signal.tobytes())
            source, log = path.open('rb'), (self.folder/(endpoint+'.log')).open('a')
            self.files.extend((source, log))
            self.players.append(subprocess.Popen(['pw-cat', '--playback', '--raw',
                '--target', self.prefix+'_'+endpoint, '--properties', json.dumps({
                    'node.name': self.prefix+'_probe_'+endpoint,
                    'node.dont-reconnect': True}), '--channels', '2',
                '--channel-map', 'FL,FR', '--rate', str(RATE), '--format', 'f32', '-'],
                stdin=source, stdout=subprocess.DEVNULL, stderr=log))
        time.sleep(.6)

    def stop_players(self):
        for process in self.players:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=8)
        self.players.clear()

    def capture(self):
        run = subprocess.run(['pw-cat', '--record', '--raw', '--target', self.prefix+'_output',
            '--channels', '16', '--channel-map', ','.join('AUX'+str(i) for i in range(16)),
            '--rate', str(RATE), '--format', 'f32', '--sample-count', str(RATE), '-'],
            capture_output=True, timeout=10)
        samples = array.array('f')
        samples.frombytes(run.stdout)
        if len(samples) != RATE*16 or not all(math.isfinite(x) for x in samples):
            raise RuntimeError('Incomplete or invalid audio capture')
        return [samples[RATE//5*16+channel:RATE*7//10*16:16] for channel in range(16)]

    def measure(self, label, mixer, analog_reference=None):
        self.post('mixer', mixer)
        time.sleep(.15)
        channels = self.capture()
        measured, expected = [], []
        for mix in mixer['mixes']:
            solos = any(strip['solo'] for strip in mix['strips'])
            master = 0 if mix['master_mute'] else 10**(mix['master_db']/20)
            master *= .1 if mix['dim'] else 1
            for side in range(2):
                expected.append([])
                for source in range(2):
                    strip = mix['strips'][source+2]
                    gain = 0 if strip['mute'] or (solos and not strip['solo']) else 10**(strip['level_db']/20)
                    if strip['level_db'] <= -59.9:
                        gain = 0
                    for pan in strip['pans']:
                        weight = math.sin(pan*math.pi/2) if side else math.cos(pan*math.pi/2)
                        expected[-1].append(AMPLITUDE*gain*master*weight)
        for channel in channels[:6]:
            n = len(channel)
            measured.append([math.hypot(sum(v*c for v, c in zip(channel, cosine)),
                                       sum(v*s for v, s in zip(channel, sine)))*2/n
                             for cosine, sine in self.basis])
        error = max(abs(a-b) for row, target in zip(measured, expected) for a, b in zip(row, target))
        stage = {'name': label, 'max_software_amplitude_error': error,
                 'ok': error < 3e-6}
        rms = lambda values: math.sqrt(sum(v*v for v in values)/len(values))
        if self.args.analog_return:
            returned = rms(channels[7])
            output = rms(channels[0])
            stage.update(analog_return_rms=returned, software_left_rms=output)
            if analog_reference:
                ref_out, ref_return = analog_reference
                # ADC noise remains when the software mix is muted. Measure
                # that floor separately and combine independent noise/signal
                # power; never demand digital zero from an analog input.
                signal_power = max(0, ref_return**2-self.analog_noise_rms**2)
                expected_return = math.sqrt(signal_power*(output/ref_out)**2+self.analog_noise_rms**2)
                stage['expected_analog_rms'] = expected_return
                tolerance = max(3e-6, self.analog_noise_rms*.3, expected_return*.05)
                stage['analog_tolerance_rms'] = tolerance
                stage['analog_ok'] = abs(returned-expected_return) < tolerance
                stage['ok'] &= stage['analog_ok']
            else:
                stage['analog_ok'] = returned > max(1e-4, self.analog_noise_rms*5) and output > 1e-3
                stage['ok'] &= stage['analog_ok']
        self.report['stages'].append(stage)
        print(json.dumps(stage), flush=True)
        return stage

    def switches(self):
        device = self.get()['device']
        if not device['ok']:
            raise RuntimeError('Hardware control endpoint unavailable')
        return device['status']['switches']

    def probe_switches(self):
        before = self.switches()
        self.original_switches = before
        for index in (1, 2):
            value = self.post('inst', {'channel': index, 'enabled': not before['inst'+str(index)]})
            ok = value['device']['status']['switches']['inst'+str(index)] != before['inst'+str(index)]
            self.post('inst', {'channel': index, 'enabled': before['inst'+str(index)]})
            self.report['stages'].append({'name': 'INST '+str(index)+' readback', 'ok': ok})
        for path, key, extra in [('mono', 'direct_mono', {}),
                                 ('hardware-routing', 'loopback', {'route': 'loopback'}),
                                 ('hardware-routing', 'loopback_to_usb12', {'route': 'usb12'}),
                                 ('hardware-routing', 'loopback_to_monitor', {'route': 'monitor'})]:
            value = self.post(path, dict(extra, enabled=not before[key]))
            ok = value['device']['status']['switches'][key] != before[key]
            self.post(path, dict(extra, enabled=before[key]))
            self.report['stages'].append({'name': key+' readback', 'ok': ok})
        self.report['stages'].append({'name': 'All switches restored; +48V retained',
                                      'ok': self.switches() == before})

    def restore_switches(self):
        if not self.original_switches:
            return
        selectors = {'inst1': '0', 'inst2': '1', 'direct_mono': '0x500',
                     'loopback': '0x1300', 'loopback_to_usb12': '0x1400',
                     'loopback_to_monitor': '0x1500'}
        current = json.loads(command([ROOT/'build/minifuze2-engine', 'status', '--ctl', self.args.ctl]))['status']['switches']
        for key, selector in selectors.items():
            if current[key] != self.original_switches[key]:
                command([ROOT/'build/minifuze2-engine', 'switch', selector,
                         'on' if self.original_switches[key] else 'off', '--ctl', self.args.ctl])

    def hotplug(self, base):
        changed = copy.deepcopy(base)
        changed['mixes'][0]['master_db'] = -3
        self.post('rename', {'kind': 'mix', 'index': 0, 'name': 'USB recovery'})
        self.post('mixer', changed)
        before = self.get()
        analog = None
        if self.args.analog_return:
            noise = self.capture()[7]
            self.analog_noise_rms = math.sqrt(sum(v*v for v in noise)/len(noise))
            self.report['analog_noise_rms'] = self.analog_noise_rms
            self.play()
            initial = self.measure('Signal before USB unplug', changed)
            analog = initial['software_left_rms'], initial['analog_return_rms']
            self.stop_players()
        print('WAITING_FOR_USB_UNPLUG: generated tones stopped', flush=True)
        deadline = time.monotonic()+self.args.wait_hotplug
        while time.monotonic() < deadline:
            state = self.get()
            if not state['devices'] and state['hardware'] is None:
                break
            time.sleep(.15)
        else:
            raise RuntimeError('No USB unplug was observed before timeout')
        self.report['stages'].append({'name': 'USB removal observed; route selection retained',
                                      'ok': state['route_selected'] and state['graph_alive']})
        print('USB_REMOVAL_OBSERVED: waiting for the same device to return', flush=True)
        deadline = time.monotonic()+self.args.wait_hotplug
        while time.monotonic() < deadline:
            state = self.get()
            if state['hardware'] and state['audio']['available'] and state['device']['ok']:
                break
            time.sleep(.15)
        else:
            raise RuntimeError('MiniFuse did not reconnect before timeout')
        self.report['stages'].append({'name': 'Same USB identity returned with a new PipeWire object',
            'ok': state['hardware']['uid'] == before['hardware']['uid']
                  and state['hardware']['serial'] != before['hardware']['serial']})
        self.report['stages'].append({'name': 'USB recovery preserves unsaved mix and names',
            'ok': state['mixer'] == changed and state['dirty']
                  and state['preset'] == before['preset'] and self.engine.poll() is None})
        if self.args.analog_return:
            self.play()
            self.measure('Analog signal after physical USB reconnect', changed, analog)

    def run(self):
        self.start()
        first = self.get()
        selected = [d for d in first['devices'] if d['port'] == self.args.port]
        if len(selected) != 1:
            raise RuntimeError('Selected MiniFuse is unavailable or ambiguous')
        if self.args.analog_return:
            controls = self.switches()
            if controls['inst2'] or controls['phantom48v']:
                raise RuntimeError('Analog probe requires INST 2 and +48V off')
        self.post('routes', {'enabled': True, 'port': self.args.port})
        connected = self.wait_connected()
        self.report['hardware_audio_connected'] = True
        self.report['hardware_control_available'] = connected['device']['ok']
        self.report['actual_rate'] = connected['audio']['actual_rate']
        if connected['device']['ok']:
            fw = int(connected['device']['identity']['bcd_device'], 16)
            self.report['firmware'] = f'{fw >> 8}.{(fw >> 4) & 15}.{fw & 15}'
        base = connected['mixer']
        if self.args.wait_hotplug:
            self.hotplug(base)
            return
        if self.args.analog_return:
            noise = self.capture()[7]
            self.analog_noise_rms = math.sqrt(sum(v*v for v in noise)/len(noise))
            self.report['analog_noise_rms'] = self.analog_noise_rms
        self.play()
        initial = self.measure('Factory routes and analog return', base)
        analog = (initial['software_left_rms'], initial['analog_return_rms']) if self.args.analog_return else None
        cases = [('Source -6 dB', lambda s: s['mixes'][0]['strips'][2].update(level_db=-6)),
                 ('Master -12 dB', lambda s: s['mixes'][0].update(master_db=-12)),
                 ('Dim -20 dB', lambda s: s['mixes'][0].update(dim=True)),
                 ('Master Mute', lambda s: s['mixes'][0].update(master_mute=True)),
                 ('Source Mute', lambda s: s['mixes'][0]['strips'][2].update(mute=True)),
                 ('Pan both channels right', lambda s: s['mixes'][0]['strips'][2].update(pans=[1, 1])),
                 ('Pan both channels left', lambda s: s['mixes'][0]['strips'][2].update(pans=[0, 0])),
                 ('Solo main with another active source', lambda s: (s['mixes'][0]['strips'][2].update(solo=True), s['mixes'][0]['strips'][3].update(mute=False))),
                 ('Mute wins over Solo', lambda s: s['mixes'][0]['strips'][3].update(solo=True)),
                 ('Multiple Solo channels', lambda s: (s['mixes'][0]['strips'][2].update(solo=True), s['mixes'][0]['strips'][3].update(mute=False, solo=True))),
                 ('Solo on Mix 3/4 leaves monitor independent', lambda s: s['mixes'][1]['strips'][2].update(mute=False, solo=True))]
        for label, change in cases:
            candidate = copy.deepcopy(base)
            change(candidate)
            self.measure(label, candidate, analog)
        self.post('mixer', base)
        self.post('save')
        changed = copy.deepcopy(base)
        changed['mixes'][0]['dim'] = True
        self.post('mixer', changed)
        undone = self.post('undo')
        self.report['stages'].append({'name': 'Undo returns to saved mix', 'ok': undone['mixer'] == base and not undone['dirty']})
        self.post('redo')
        self.measure('Redo restores Dim on physical return', changed, analog)
        self.post('rename', {'kind': 'mix', 'index': 0, 'name': 'Hardware acceptance'})
        exported = json.loads(self.post('export-native')['text'])
        self.post('factory')
        imported = self.post('import-native', {'text': json.dumps(exported)})
        self.report['stages'].append({'name': 'Portable preset with selected hardware',
            'ok': imported['mixer'] == changed and imported['preset']['mix_names'][0] == 'Hardware acceptance' and bool(imported['hardware'])})
        self.stop_players()
        self.stop_engine()
        self.start()
        restored = self.wait_connected()
        self.report['stages'].append({'name': 'Service restart retains route and unsaved mix',
            'ok': restored['mixer'] == changed and restored['dirty'] and restored['preset']['mix_names'][0] == 'Hardware acceptance'})
        self.play()
        self.measure('Signal after service restart', changed, analog)
        self.post('mixer', base)
        objects = snapshot()
        own = {o['id'] for o in objects if props(o).get('node.name') == self.prefix+'_output'}
        candidates = [o for o in objects if o['type'] == 'PipeWire:Interface:Link' and o['info']['output-node-id'] in own and o['info']['input-node-id'] != o['info']['output-node-id']]
        hardware = {o['id'] for o in objects if props(o).get('device.id') == restored['hardware']['id']}
        link = next(o for o in candidates if o['info']['input-node-id'] in hardware)
        command(['pw-link', '-d', str(link['id'])])
        time.sleep(1.3)
        self.wait_connected()
        self.measure('Signal after an owned hardware link is removed', base, analog)
        self.post('routes', {'enabled': False, 'port': None})
        disconnected = self.get()
        self.report['stages'].append({'name': 'Explicit disconnect', 'ok': disconnected['hardware'] is None and not disconnected['route_selected']})
        self.post('routes', {'enabled': True, 'port': self.args.port})
        self.wait_connected()
        self.measure('Signal after explicit reconnect', base, analog)
        self.stop_players()
        if self.args.probe_switches:
            self.probe_switches()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='Explicit MiniFuse USB port, e.g. 3-2.1')
    parser.add_argument('--ctl', default='/dev/minifuze2ctl')
    parser.add_argument('--analog-return', action='store_true', help='Operator confirms OUTPUT L -> INPUT 2, MIX at USB, GAIN 2 minimum, INST 2/+48V off')
    parser.add_argument('--probe-switches', action='store_true', help='Toggle INST, Mono and routing, then restore; never toggle +48V')
    parser.add_argument('--wait-hotplug', type=int, default=0, metavar='SECONDS',
                        help='Instead of the full suite, wait for operator USB unplug/replug; maximum 600 seconds per phase')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[0-9]+-[0-9]+(?:\.[0-9]+)*', args.port):
        parser.error('Use a USB port path')
    if args.report.exists():
        parser.error('Retain the existing report')
    if not 0 <= args.wait_hotplug <= 600 or (args.wait_hotplug and args.probe_switches):
        parser.error('Hotplug mode uses 1..600 seconds and cannot probe switches')
    before_defaults, before_clock = defaults(), clock_settings()
    with tempfile.TemporaryDirectory(prefix='minifuze-hardware-test-') as folder:
        probe = Probe(args, Path(folder))
        try:
            probe.run()
        except Exception as error:
            probe.report['error'] = type(error).__name__+': '+str(error).replace(str(folder), '<test-directory>')
        finally:
            for action in (probe.stop_players, probe.restore_switches, probe.stop_engine):
                try:
                    action()
                except Exception as error:
                    probe.report.setdefault('cleanup_errors', []).append(type(error).__name__)
            for file in probe.files:
                file.close()
        probe.report['stages'].extend([
            {'name': 'Default devices unchanged', 'ok': before_defaults == defaults()},
            {'name': 'Forced clock settings unchanged', 'ok': before_clock == clock_settings()},
            {'name': 'Owned graph removed', 'ok': not any(props(o).get('node.name', '').startswith(probe.prefix+'_') for o in snapshot())}])
        probe.report['engine_sha256'] = hashlib.sha256((ROOT/'build/minifuze2-engine').read_bytes()).hexdigest()
        probe.report['ok'] = (not probe.report.get('error') and not probe.report.get('cleanup_errors')
                              and all(stage['ok'] for stage in probe.report['stages']))
        with args.report.open('x') as file:
            json.dump(probe.report, file, indent=2)
            file.write('\n')
        print(json.dumps(probe.report, indent=2))
        return 0 if probe.report['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
