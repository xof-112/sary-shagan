# Sary-Shagan – Arbeitsnotizen

Dattorro-Plattenhall für den disting NT, Portierung von Valley **Plateau** (ValleyRackFree, GPL-3.0), Bedienung nach **Mesa** (4ms + Valley). Drei Fassungen aus **einem** Quelltext:

1. NT-Plug-in `sary_shagan.o` (ARM, `make`)
2. nt_emu-DLL `sary_shagan.dll` (`make win`, Brücke `emu/nt_win_shim.*`)
3. natives VCV-Modul 3F4-SaryShagan (`vcv/`, NT-Host aus Perseids führt das Plug-in unverändert aus)

Vorher früherer Name: **Grando** (bis v0.3, GUID `XoGr`, Slug `3F4-Grando`). Jetzt GUID `NT_MULTICHAR('X','o','S','S')`, Slug `3F4-SaryShagan`. Herkunft: Ordner `sary-shagan/` im (privaten) Repo `xof-112/disting-nt-plugins`, Branch `grando`; seit 21.11.2026 eigenständig hier.

## Konventionen (vom Autor xof festgelegt)

- Antworten, Doku, Handbuch auf **Deutsch**; Oberfläche/Display/Parameternamen englisch. **xof** immer klein.
- VCV-Plugins werden **immer als ZIP** des Plugin-Ordners ausgeliefert (`3F4-SaryShagan-<ver>-win-x64.zip`), **nie als .vcvplugin**.
- Bedienung folgt der Hardware Mesa: vier Seiten INPUT/SPACE/TONE/MOD zu je drei Potis; 10 CV-Eingänge ±5 V = ganzer Weg, zum Regler addiert.
- Display: Elemente grob über ihrem Bedienelement verortet (WET-Pegel mittig unter Poti C, `FINE <NAME> wert` rechts in CAPS). MOD-Shape als Wellenbild mit drei Grundform-Icons.
- VCV-Panel: Hausstil 3F4 (`vcv/style/`), IN/OUT waagerecht unten. Positionen nur in `vcv/panel_def.py` ändern (erzeugt SVG und `panel_layout.hpp`).
- Handbuch nach xofs **Handbuch-Leitfaden** (Kapitelfolge Einstieg → Spielen → eigene Gruppe → Praxis → Nachschlagen; letzte sechs Kapitel fest; Du-Form; Parameter *kursiv*, Bedienelemente **fett**; Dezimalkomma; Rezepte mit `.patch`-Kasten; Prüfstand, Checklisten, Versionshistorie neueste oben). Stil 1:1 wie die Geschwister-Handbücher (Taurids, Theme „Kante“, `docs/handbuch/style.html`, Vorlese-Baustein `vorlesen.html`).

## Version erhöhen – alle Stellen

1. `sary_shagan.cpp`: `#define SARY_SHAGAN_VERSION`
2. `vcv/plugin.json`: `"version"` (eigene Nummer des VCV-Moduls)
3. `docs/handbuch/kapitel.html`: Zeile in der Versionshistorie (neueste oben); Eyebrow/Chip/Fußzeile ziehen `{{VER}}`/`{{VCV}}`/`{{DATUM}}` automatisch
4. `README.md` / `vcv/README.md`: ZIP-Namen
5. `make test && make -C vcv/test && make handbuch && make release RACK_DIR=…` und `release/` einchecken

## Prüfen

- `make test` (65 Prüfungen), `make compare` (83–101 dB ohne, ~36 dB mit Modulation), `make -C vcv/test` (25 Prüfungen), `make render`.
- ARM-Build muss ohne Warnungen bleiben; aus libm nur `expf` (keine `sinf`, `exp2f`, `log` am NT).
- Neue Parameter: Text in `docs/handbuch/wirkung.tsv` ergänzen, sonst bricht `build.py` ab.
- nt_emu-Ladetest: `emu/testhost.cpp` unter Wine (am 21.11.2026 lief Wine in der Bauumgebung nicht; zuletzt bestanden 24.10.2026).

## Bau-Umgebung (Linux)

- `arm-none-eabi-gcc`, `g++`, `mingw-w64`, Python 3 mit Pillow, `zip`.
- Rack-SDK unter `../Rack-SDK` (neben dem Repo) oder `RACK_DIR=`. Ohne offizielles SDK: `vcv/tools/rack-sdk-from-source.sh` + `vcv/tools/mklibrack.py` (Schritte im Skriptkopf).
- `make dist` scheitert am Ende an fehlendem `zstd` – egal, gezippt wird der Ordner `vcv/dist/3F4-SaryShagan`.

## Offen

- Noch nie in echtem VCV Rack, nt_emu oder auf NT-Hardware gelaufen → Checklisten im Handbuch, Kapitel „Grenzen und Stand der Prüfung“.
- CPU-Last am NT unbekannt. Mesas *Reverse* nicht umgesetzt (neue DSP). Keine Werks-Presets.
