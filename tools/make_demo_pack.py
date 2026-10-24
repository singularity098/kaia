#!/usr/bin/env python3
"""Builds "Kaia Demo Kit.xpn": a small MPC expansion with synthesized drums.

The sounds are generated from scratch (no third-party samples), so the pack can be shared freely.
It contains two drum programs in MPC 2.x XML format with velocity layers, round robins and a
hi-hat choke group, which makes it a handy check that Kaia is installed and working.

Usage: python3 make_demo_pack.py <output.xpn>
"""
import io
import struct
import sys
import zipfile

import numpy as np

RATE = 44100
rng = np.random.default_rng(7)


def wav_bytes(data, rate=RATE):
    data = np.clip(data, -1.0, 1.0)
    if data.ndim == 1:
        data = data[:, None]
    frames, channels = data.shape
    pcm = (data * 32767.0).astype('<i2').tobytes()
    out = io.BytesIO()
    out.write(b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVE')
    out.write(b'fmt ' + struct.pack('<IHHIIHH', 16, 1, channels, rate, rate * channels * 2, channels * 2, 16))
    out.write(b'data' + struct.pack('<I', len(pcm)) + pcm)
    return out.getvalue()


def env(n, attack=0.001, decay=0.3, curve=4.0):
    t = np.arange(n) / RATE
    a = np.clip(t / max(attack, 1e-6), 0, 1)
    return a * np.exp(-curve * t / decay)


def lowpass(x, cutoff):
    # simple one-pole
    a = np.exp(-2 * np.pi * cutoff / RATE)
    y = np.empty_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc = (1 - a) * v + a * acc
        y[i] = acc
    return y


def highpass(x, cutoff):
    return x - lowpass(x, cutoff)


def stereo(x, width=0.15, seed=0):
    r = np.random.default_rng(seed)
    d = int(r.integers(5, 25))
    right = np.concatenate([np.zeros(d), x[:-d]])
    return np.stack([x * (1 - width) + right * width, right * (1 - width) + x * width], axis=1)


def kick(level, tone=1.0, seed=0):
    n = int(0.6 * RATE)
    t = np.arange(n) / RATE
    f = 45 * tone + 110 * np.exp(-t / 0.045)
    phase = 2 * np.pi * np.cumsum(f) / RATE
    body = np.sin(phase) * env(n, 0.0005, 0.35, 3.0)
    click = highpass(np.random.default_rng(seed).standard_normal(n), 2000) * env(n, 0.0001, 0.004, 5.0) * 0.4
    return (body + click) * level


def snare(level, seed=0):
    n = int(0.45 * RATE)
    t = np.arange(n) / RATE
    tone = (np.sin(2 * np.pi * 185 * t) + 0.5 * np.sin(2 * np.pi * 330 * t)) * env(n, 0.0005, 0.12, 4.0)
    noise = highpass(np.random.default_rng(seed).standard_normal(n), 1500) * env(n, 0.0005, 0.22, 4.0)
    return stereo((0.55 * tone + 0.6 * noise) * level, 0.2, seed)


def hat(level, length, seed=0):
    n = int(length * RATE)
    noise = np.random.default_rng(seed).standard_normal(n)
    metal = sum(np.sign(np.sin(2 * np.pi * f * np.arange(n) / RATE)) for f in (317, 427, 563, 709, 881, 1037)) / 6
    x = highpass(0.6 * noise + 0.4 * metal, 7000)
    return stereo(x * env(n, 0.0003, length * 0.6, 3.0) * level * 0.6, 0.25, seed)


def tom(freq, level=0.8):
    n = int(0.7 * RATE)
    t = np.arange(n) / RATE
    f = freq * (1 + 0.6 * np.exp(-t / 0.03))
    return np.sin(2 * np.pi * np.cumsum(f) / RATE) * env(n, 0.0005, 0.4, 3.0) * level


def clap(level, seed=0):
    n = int(0.4 * RATE)
    noise = highpass(np.random.default_rng(seed).standard_normal(n), 900)
    e = np.zeros(n)
    for k, off in enumerate((0.0, 0.011, 0.022, 0.034)):
        s = int(off * RATE)
        e[s:] += env(n - s, 0.0003, 0.012 if k < 3 else 0.18, 4.0)
    return stereo(noise * e * 0.5 * level, 0.3, seed)


def perc(freq, decay, level=0.7):
    n = int(decay * 2 * RATE)
    t = np.arange(n) / RATE
    return (np.sin(2 * np.pi * freq * t) + 0.3 * np.sin(2 * np.pi * freq * 2.7 * t)) * env(n, 0.0003, decay, 4.0) * level


def ride(level=0.5, seed=3):
    n = int(2.5 * RATE)
    metal = sum(np.sin(2 * np.pi * f * np.arange(n) / RATE + i) for i, f in enumerate((523, 739, 1013, 1471, 2113, 3301, 4703)))
    noise = highpass(np.random.default_rng(seed).standard_normal(n), 5000) * 0.3
    return stereo((metal / 7 + noise) * env(n, 0.0005, 1.6, 3.0) * level, 0.3, seed)


def crash(level=0.6, seed=4):
    n = int(3.0 * RATE)
    noise = highpass(np.random.default_rng(seed).standard_normal(n), 3000)
    return stereo(noise * env(n, 0.002, 1.8, 3.0) * level * 0.5, 0.4, seed)


def layer_xml(n, name, lo=0, hi=127):
    return f"""          <Layer number="{n}">
            <Active>True</Active>
            <Volume>1.000000</Volume>
            <Pan>0.500000</Pan>
            <Pitch>0.000000</Pitch>
            <TuneCoarse>0</TuneCoarse>
            <TuneFine>0</TuneFine>
            <VelStart>{lo}</VelStart>
            <VelEnd>{hi}</VelEnd>
            <SampleStart>0</SampleStart>
            <SampleEnd>0</SampleEnd>
            <Loop>False</Loop>
            <RootNote>0</RootNote>
            <KeyTrack>False</KeyTrack>
            <SampleName>{name}</SampleName>
            <SampleFile></SampleFile>
            <SliceIndex>128</SliceIndex>
            <Direction>0</Direction>
            <Offset>0</Offset>
            <SliceStart>0</SliceStart>
            <SliceEnd>0</SliceEnd>
            <SliceLoopStart>0</SliceLoopStart>
            <SliceLoop>0</SliceLoop>
          </Layer>
"""


def program_xml(name, pads):
    """pads: dict pad number (1-based) -> dict(note, layers=[(sample, lo, hi)], zone=1, mute=0, volume=0.707946, pan=0.5)"""
    out = ["""<?xml version="1.0" encoding="UTF-8"?>

<MPCVObject>
  <Version>
    <File_Version>2.1</File_Version>
    <Application>MPC-V</Application>
    <Application_Version>2.10.1.0</Application_Version>
    <Platform>Linux</Platform>
  </Version>
  <Program type="Drum">
    <ProgramName>%s</ProgramName>
    <Volume>0.707946</Volume>
    <Pan>0.500000</Pan>
    <Instruments>
""" % name]
    for number in range(1, 129):
        p = pads.get(number, {})
        layers = p.get('layers', [])
        out.append(f"""      <Instrument number="{number}">
        <Volume>{p.get('volume', 0.707946):.6f}</Volume>
        <Mute>False</Mute>
        <Pan>{p.get('pan', 0.5):.6f}</Pan>
        <TuneCoarse>0</TuneCoarse>
        <TuneFine>0</TuneFine>
        <Mono>False</Mono>
        <Polyphony>1</Polyphony>
        <ZonePlay>{p.get('zone', 1)}</ZonePlay>
        <MuteGroup>{p.get('mute', 0)}</MuteGroup>
        <MuteTarget1>0</MuteTarget1>
        <TriggerMode>0</TriggerMode>
        <FilterType>0</FilterType>
        <Cutoff>1.000000</Cutoff>
        <Resonance>0.000000</Resonance>
        <VolumeHold>0.000000</VolumeHold>
        <VolumeDecayType>True</VolumeDecayType>
        <VolumeADEnvelope>True</VolumeADEnvelope>
        <VolumeAttack>0.000000</VolumeAttack>
        <VolumeDecay>0.047244</VolumeDecay>
        <VolumeSustain>1.000000</VolumeSustain>
        <VolumeRelease>0.000000</VolumeRelease>
        <VelocitySensitivity>1.000000</VelocitySensitivity>
        <Layers>
""")
        for i in range(4):
            if i < len(layers):
                out.append(layer_xml(i + 1, *layers[i]))
            else:
                out.append(layer_xml(i + 1, ""))
        out.append("        </Layers>\n      </Instrument>\n")
    out.append("    </Instruments>\n    <PadNoteMap>\n")
    for number in range(1, 129):
        if number in pads:
            out.append(f'      <PadNote number="{number}">\n        <Note>{pads[number]["note"]}</Note>\n      </PadNote>\n')
    out.append("    </PadNoteMap>\n  </Program>\n</MPCVObject>\n")
    return "".join(out)


def main(path):
    samples = {
        'Kick Soft': kick(0.55, 1.0, 1),
        'Kick Hard': kick(0.95, 1.05, 2),
        'Snare RR1': snare(0.8, 11),
        'Snare RR2': snare(0.78, 12),
        'Snare RR3': snare(0.82, 13),
        'Rim': perc(1650, 0.03, 0.6),
        'Clap': clap(0.9, 21),
        'Hat Closed': hat(0.8, 0.09, 31),
        'Hat Pedal': hat(0.6, 0.06, 32),
        'Hat Open': hat(0.75, 0.9, 33),
        'Tom Low': tom(95),
        'Tom Mid': tom(135),
        'Tom High': tom(185),
        'Ride': ride(),
        'Crash': crash(),
        'Shaker': hat(0.5, 0.12, 41),
        'Cowbell': perc(560, 0.18, 0.5) + np.pad(perc(840, 0.12, 0.3), (0, int(0.18 * 2 * RATE) - int(0.12 * 2 * RATE))),
        'Conga': perc(330, 0.15, 0.7),
    }

    kit = {
        1: dict(note=37, layers=[('Rim',)]),
        2: dict(note=36, layers=[('Kick Soft', 0, 89), ('Kick Hard', 90, 127)]),
        3: dict(note=42, layers=[('Hat Closed',)], mute=1),
        4: dict(note=82, layers=[('Shaker',)], pan=0.62),
        5: dict(note=40, layers=[('Clap',)]),
        6: dict(note=38, layers=[('Snare RR1',), ('Snare RR2',), ('Snare RR3',)], zone=0),
        7: dict(note=46, layers=[('Hat Open',)], mute=1),
        8: dict(note=44, layers=[('Hat Pedal',)], mute=1),
        9: dict(note=48, layers=[('Tom High',)], pan=0.4),
        10: dict(note=47, layers=[('Tom Mid',)], pan=0.5),
        11: dict(note=45, layers=[('Tom Low',)], pan=0.6),
        12: dict(note=43, layers=[('Conga',)]),
        13: dict(note=49, layers=[('Crash',)], pan=0.35),
        14: dict(note=55, layers=[('Cowbell',)]),
        15: dict(note=51, layers=[('Ride',)], pan=0.65),
        16: dict(note=53, layers=[('Ride',)], volume=0.6),
    }
    for k, v in kit.items():
        v['layers'] = [(l[0], l[1], l[2]) if len(l) == 3 else (l[0], 0, 127) for l in v['layers']]

    lofi = {k: dict(v) for k, v in kit.items() if k in (2, 3, 5, 6, 7)}

    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('Expansion.xml', """<?xml version="1.0" encoding="UTF-8"?>
<expansion version="2.0.0.0" buildVersion="2.10.0.0">
  <local>en-US</local>
  <identifier>io.github.singularity098.kaia.demokit</identifier>
  <title>Kaia Demo Kit</title>
  <manufacturer>Kaia</manufacturer>
  <version>1.0.0.0</version>
  <type>drum</type>
  <priority>50</priority>
  <description>Synthesized drums for checking a Kaia installation.</description>
</expansion>
""")
        for name, data in samples.items():
            z.writestr(f'[Samples]/{name}.WAV', wav_bytes(data))
        z.writestr('Kaia Demo Kit.xpm', program_xml('Kaia Demo Kit', kit))
        z.writestr('Kaia Demo Kit - Small.xpm', program_xml('Kaia Demo Kit - Small', lofi))
    print('wrote', path)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'Kaia Demo Kit.xpn')
