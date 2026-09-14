#!/usr/bin/env python3
"""Generate deterministic board-specific SVG wiring diagrams from projects.json."""

import json
import textwrap
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "web" / "wiring"
COLORS = {
    "red": "#e53935",
    "black": "#20252b",
    "green": "#2e7d32",
    "yellow": "#f9a825",
    "blue": "#1565c0",
    "purple": "#8e24aa",
    "orange": "#ef6c00",
}
COMPONENTS = {
    "bme280-mqtt-sensor": "BME280 I²C breakout",
    "hc-sr04-parking": "HC-SR04 ultrasonic sensor",
    "pir-occupancy-timer": "AM312 PIR sensor",
    "ntp-desk-clock": "TM1637 four-digit display",
    "mqtt-home-status-panel": "LCD1602 MQTT status panel",
    "unifi-network-panel": "LCD1602 UniFi network panel",
    "space-satellite-tracker": "LCD1602 ISS tracker",
    "wifi-weather-station": "LCD1602 weather station",
}


def board_pin(destination: str) -> str:
    return destination.split(" through ", 1)[0]


def component_pin(source: str) -> str:
    return source.rsplit(" ", 1)[-1]


def component_art(slug: str, variant_id: str | None = None) -> str:
    """Return a simple, recognizable module silhouette without implying exact scale."""
    if slug == "hc-sr04-parking":
        return '<g><circle cx="950" cy="265" r="42" fill="#c8d0d4" stroke="#65727a" stroke-width="5"/><circle cx="1040" cy="265" r="42" fill="#c8d0d4" stroke="#65727a" stroke-width="5"/><circle cx="950" cy="265" r="25" fill="#66757d"/><circle cx="1040" cy="265" r="25" fill="#66757d"/></g>'
    if slug == "pir-occupancy-timer":
        return '<g><circle cx="990" cy="305" r="70" fill="#eef1e9" stroke="#a7aea5" stroke-width="5"/><path d="M940 305h100M949 275h82M949 335h82M990 245v120" stroke="#c4cac1" stroke-width="3" opacity=".8"/></g>'
    if slug in {"ntp-desk-clock", "mqtt-home-status-panel", "unifi-network-panel", "space-satellite-tracker", "wifi-weather-station"}:
        if variant_id == "lcd1602-i2c":
            return '<g><rect x="860" y="230" width="260" height="110" rx="8" fill="#1c6d67" stroke="#0c3734" stroke-width="4"/><rect x="880" y="250" width="220" height="70" fill="#9acb63" stroke="#233b19" stroke-width="3"/><text x="990" y="280" text-anchor="middle" style="font:700 18px ui-monospace,monospace;fill:#193111">Date 2026-09-13</text><text x="990" y="306" text-anchor="middle" style="font:700 18px ui-monospace,monospace;fill:#193111">Time 12:34:56</text></g>'
        if slug != "ntp-desk-clock":
            return '<g><rect x="860" y="230" width="260" height="110" rx="8" fill="#1c6d67" stroke="#0c3734" stroke-width="4"/><rect x="880" y="250" width="220" height="70" fill="#9acb63" stroke="#233b19" stroke-width="3"/><text x="990" y="280" text-anchor="middle" style="font:700 18px ui-monospace,monospace;fill:#193111">LCD1602 PANEL</text><text x="990" y="306" text-anchor="middle" style="font:700 18px ui-monospace,monospace;fill:#193111">STATUS READY</text></g>'
        return '<g><rect x="910" y="245" width="160" height="82" rx="8" fill="#120b0b" stroke="#724343" stroke-width="4"/><text x="990" y="302" text-anchor="middle" style="font:700 45px ui-monospace,monospace;fill:#e83b3b">12:34</text></g>'
    return '<g><rect x="945" y="260" width="90" height="90" rx="8" fill="#2d805c" stroke="#174732" stroke-width="4"/><rect x="970" y="285" width="40" height="40" rx="3" fill="#c8d0d4" stroke="#65727a" stroke-width="3"/><circle cx="955" cy="272" r="4" fill="#d5b642"/><circle cx="1025" cy="338" r="4" fill="#d5b642"/></g>'


def wire_path(index: int, board_y: int, component_y: int, color: str, is_echo: bool, connect_ground_rail: bool) -> str:
    if not is_echo:
        bend_a = 500 + index * 18
        bend_b = 735 - index * 12
        path = f"M410 {board_y} C {bend_a} {board_y}, {bend_b} {component_y}, 845 {component_y}"
        branch = ""
        if connect_ground_rail:
            branch = f'<path d="M410 {board_y} C450 {board_y}, 450 535, 495 535" fill="none" stroke="#20252b" stroke-width="6"/><circle cx="495" cy="535" r="6" fill="#20252b"/>'
        return (
            f'<path d="{path}" fill="none" stroke="#fff" stroke-width="10" opacity=".92"/>'
            f'<path d="{path}" fill="none" stroke="{color}" stroke-width="6"/>'
            f'{branch}'
        )

    # The Echo divider is part of the signal path: 1 kΩ from Echo to the
    # junction, 2 kΩ from the junction to common ground, then to the GPIO.
    return f'''
<path d="M845 {component_y}H760" fill="none" stroke="#fff" stroke-width="10"/><path d="M845 {component_y}H760" fill="none" stroke="{color}" stroke-width="6"/>
<path d="M760 {component_y}h-18l-10 -12l-20 24l-20 -24l-20 24l-10 -12h-22" fill="none" stroke="#6d4c41" stroke-width="4"/>
<text x="700" y="{component_y-18}" text-anchor="middle" class="resistor">1 kΩ</text>
<circle cx="640" cy="{component_y}" r="7" fill="{color}" stroke="#fff" stroke-width="2"/>
<path d="M640 {component_y} C 575 {component_y}, 510 {board_y}, 410 {board_y}" fill="none" stroke="#fff" stroke-width="10"/><path d="M640 {component_y} C 575 {component_y}, 510 {board_y}, 410 {board_y}" fill="none" stroke="{color}" stroke-width="6"/>
<path d="M640 {component_y}v10l-10 7l20 12l-20 12l20 12l-10 7v14" fill="none" stroke="#6d4c41" stroke-width="4"/>
<text x="660" y="{component_y+43}" class="resistor">2 kΩ</text><text x="660" y="{component_y+62}" class="tiny">to common GND rail</text>
<circle cx="640" cy="535" r="6" fill="#20252b"/>
'''


def diagram(project: dict, target: dict, configuration: dict | None = None) -> str:
    configuration = configuration or target
    is_variant = configuration is not target
    configuration_label = configuration["name"] if is_variant else COMPONENTS[project["slug"]]
    subtitle = configuration["name"] if is_variant else "exact target wiring"
    variant_id = configuration["id"] if is_variant else None
    wiring = configuration["wiring"]
    connections = wiring["connections"]
    board_labels = []
    component_labels = []
    wires = []
    start_y = 200 if len(connections) > 4 else 230
    spacing = 50 if len(connections) > 4 else 72
    for index, connection in enumerate(connections):
        board_y = start_y + index * spacing
        component_y = (205 + index * 50) if len(connections) > 4 else (215 + index * 82)
        color = COLORS[connection["wire"]]
        destination = board_pin(connection["to"])
        source_pin = component_pin(connection["from"])
        board_labels.append(
            f'<circle cx="410" cy="{board_y}" r="8" fill="#d7dee4" stroke="#17232c" stroke-width="3"/>'
            f'<text x="392" y="{board_y+5}" text-anchor="end" class="pin">{escape(destination)}</text>'
        )
        shifted_signal = "through bidirectional I2C level shifter" in connection["to"]
        shifter_power = connection["from"].startswith("Level shifter ")
        if not shifter_power:
            component_labels.append(
                f'<circle cx="845" cy="{component_y}" r="8" fill="#d7dee4" stroke="#17232c" stroke-width="3"/>'
                f'<text x="865" y="{component_y+5}" class="pin">{escape(source_pin)}</text>'
            )
        is_ultrasonic = project["slug"] == "hc-sr04-parking"
        if shifted_signal:
            path = f"M410 {board_y} C470 {board_y}, 505 {component_y}, 540 {component_y} M780 {component_y} C805 {component_y}, 825 {component_y}, 845 {component_y}"
            wires.append(f'<path d="{path}" fill="none" stroke="#fff" stroke-width="10" opacity=".92"/><path d="{path}" fill="none" stroke="{color}" stroke-width="6"/>')
        elif shifter_power:
            target_x = 780 if connection["from"].endswith("HV") else 540 if connection["from"].endswith("LV") else 660
            target_y = 405 if target_x != 660 else 435
            path = f"M410 {board_y} C485 {board_y}, 535 {target_y}, {target_x} {target_y}"
            wires.append(f'<path d="{path}" fill="none" stroke="#fff" stroke-width="10" opacity=".92"/><path d="{path}" fill="none" stroke="{color}" stroke-width="6"/>')
        else:
            wires.append(wire_path(
                index,
                board_y,
                component_y,
                color,
                is_ultrasonic and "ECHO" in connection["from"],
                is_ultrasonic and "GND" in connection["from"],
            ))

    holes = "".join(
        f'<circle cx="{505 + col * 28}" cy="{176 + row * 55}" r="3.2" fill="#a8b0b5"/>'
        for row in range(8) for col in range(19)
    )
    connection_text = "".join(
        f'<circle cx="72" cy="{650+i*27-5}" r="6" fill="{COLORS[item["wire"]]}"/>'
        f'<text x="88" y="{650+i*27}" class="connection">{escape(item["from"])} → {escape(item["to"])}</text>'
        for i, item in enumerate(connections)
    )
    notes_y = 650 + len(connections) * 27 + 35
    warning_lines = []
    for warning in wiring["warnings"]:
        lines = textwrap.wrap(warning, width=105, break_long_words=False, break_on_hyphens=False)
        warning_lines.extend((line, warning, index == 0) for index, line in enumerate(lines))
    warning_text = "".join(
        f'<text x="70" y="{notes_y+32+i*24}" class="warning" data-full="{escape(full, quote=True) if first else ""}">{escape(("⚠ " if first else "  ") + line)}</text>'
        for i, (line, full, first) in enumerate(warning_lines)
    )
    view_height = max(890, notes_y + 32 + len(warning_lines) * 24 + 30)
    level_shifter_art = ""
    if any("through bidirectional I2C level shifter" in item["to"] for item in connections):
        level_shifter_art = '''<g filter="url(#shadow)"><rect x="540" y="270" width="240" height="190" rx="10" fill="#6b4aa0" stroke="#362052" stroke-width="4"/>
<text x="660" y="292" text-anchor="middle" class="board">I2C SHIFTER</text>
<circle cx="540" cy="305" r="7" fill="#d7dee4"/><text x="550" y="301" class="tiny">LV1 / 3.3 V</text><circle cx="780" cy="305" r="7" fill="#d7dee4"/><text x="770" y="301" text-anchor="end" class="tiny">HV1 / 5 V</text>
<circle cx="540" cy="355" r="7" fill="#d7dee4"/><text x="550" y="351" class="tiny">LV2 / 3.3 V</text><circle cx="780" cy="355" r="7" fill="#d7dee4"/><text x="770" y="351" text-anchor="end" class="tiny">HV2 / 5 V</text>
<circle cx="540" cy="405" r="7" fill="#d7dee4"/><text x="550" y="409" class="tiny">LV</text><circle cx="780" cy="405" r="7" fill="#d7dee4"/><text x="770" y="409" text-anchor="end" class="tiny">HV</text>
<circle cx="660" cy="435" r="7" fill="#d7dee4"/><text x="670" y="439" class="tiny">GND</text></g>'''
    description = "; ".join(f'{item["from"]} to {item["to"]}' for item in connections)

    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1200 {view_height}" role="img" aria-labelledby="title desc">
<title id="title">{escape(project['name'])} wiring for {escape(target['name'])}</title>
<desc id="desc">Fritzing-inspired board and breadboard diagram. {escape(description)}.</desc>
<style>.heading{{font:700 30px system-ui;fill:#17232c}}.sub{{font:500 17px system-ui;fill:#51616d}}.board{{font:700 18px system-ui;fill:white}}.pin{{font:700 15px ui-monospace,monospace;fill:#17232c}}.section{{font:700 14px system-ui;fill:#17232c;letter-spacing:.08em}}.connection{{font:600 14px ui-monospace,monospace;fill:#34444f}}.warning{{font:650 14px system-ui;fill:#7a2f16}}.resistor{{font:700 12px ui-monospace,monospace;fill:#5d4a18}}.tiny{{font:11px system-ui;fill:#5d4a18}}</style>
<defs><pattern id="grid" width="24" height="24" patternUnits="userSpaceOnUse"><path d="M24 0H0V24" fill="none" stroke="#dfe4e7" stroke-width="1"/></pattern><filter id="shadow"><feDropShadow dx="0" dy="5" stdDeviation="5" flood-opacity=".18"/></filter></defs>
<rect width="1200" height="{view_height}" fill="#f2f4f5"/><rect width="1200" height="{view_height}" fill="url(#grid)"/>
<text x="60" y="55" class="heading">{escape(project['name'])}</text><text x="60" y="84" class="sub">{escape(target['name'])} · {escape(subtitle)}</text>
<g filter="url(#shadow)"><rect x="70" y="130" width="360" height="450" rx="24" fill="#146c94" stroke="#0b3f59" stroke-width="4"/><rect x="182" y="100" width="135" height="58" rx="8" fill="#c9d0d4" stroke="#65727a" stroke-width="4"/><rect x="205" y="112" width="88" height="26" rx="5" fill="#343c42"/><rect x="137" y="195" width="225" height="170" rx="12" fill="#20282e"/><path d="M155 215h188v130H155z" fill="#303a41" stroke="#64727b"/><text x="250" y="272" text-anchor="middle" class="board">ESP32</text><text x="250" y="300" text-anchor="middle" class="board">{escape(target['id'].replace('-', ' ').upper())}</text><text x="250" y="545" text-anchor="middle" class="board">USB</text>{''.join(board_labels)}</g>
<g filter="url(#shadow)"><rect x="475" y="130" width="660" height="450" rx="18" fill="#fafafa" stroke="#c3c9cd" stroke-width="4"/><rect x="493" y="153" width="624" height="404" rx="12" fill="#fff" stroke="#e0e3e5"/>{holes}<path d="M495 190h620M495 520h620" stroke="#e64a4a" stroke-width="3"/><path d="M495 205h620M495 535h620" stroke="#3b70c4" stroke-width="3"/></g>
{level_shifter_art}
<g filter="url(#shadow)"><rect x="810" y="155" width="280" height="{max(390, 84*len(connections)+30)}" rx="16" fill="#263238" stroke="#11181c" stroke-width="4"/><text x="950" y="188" text-anchor="middle" class="board">{escape(configuration_label)}</text>{component_art(project['slug'], variant_id)}{''.join(component_labels)}</g>
{''.join(wires)}
<text x="70" y="615" class="section">CONNECTIONS</text>{connection_text}
<text x="70" y="{notes_y}" class="section">ELECTRICAL NOTES</text>{warning_text}
</svg>'''


def main() -> None:
    catalog = json.loads((ROOT / "projects.json").read_text())
    expected = {
        ROOT / "web" / configuration["wiring"]["diagram"].removeprefix("./")
        for project in catalog if project["slug"] in COMPONENTS
        for target in project["targets"]
        for configuration in [target, *target.get("variants", [])]
    }
    for unexpected in OUTPUT.glob("*/*.svg"):
        if unexpected not in expected:
            unexpected.unlink()
    count = 0
    generated = set()
    for project in catalog:
        if project["slug"] not in COMPONENTS:
            continue
        directory = OUTPUT / project["slug"]
        directory.mkdir(parents=True, exist_ok=True)
        for target in project["targets"]:
            for configuration in [target, *target.get("variants", [])]:
                path = ROOT / "web" / configuration["wiring"]["diagram"].removeprefix("./")
                if path.parent != directory:
                    raise ValueError(f"unexpected wiring output path: {path}")
                if path in generated:
                    raise ValueError(f"duplicate wiring output path: {path}")
                generated.add(path)
                path.write_text(diagram(project, target, configuration))
                count += 1
    print(f"generated {count} wiring diagrams in {OUTPUT}")


if __name__ == "__main__":
    main()
