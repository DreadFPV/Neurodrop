# NeuroDrop

DnB-/Neurofunk-Drop-Generator für Psytrance-Tracks (VST3 · Windows · macOS · Linux).
NeuroDrop hört sich deinen Psy-Track an (Tonart, 432/440-Tuning, Kick, Spektrum, Lautheit) und rendert einen passenden
Drum-&-Bass-/Neurofunk-Drop als **einzelne WAV-Stems** zum Reinziehen – ohne das Tempo deines Projekts anzufassen.

## Ablauf in Cubase

1. **Einfügen**: NeuroDrop als letzten Insert auf den **Stereo Out** (oder die Psy-Gruppe) legen.
   Alternativ auf eine eigene Spur und die Psy-Gruppe per **Sidechain** hineinschicken – NeuroDrop nimmt automatisch die Sidechain, sobald dort Signal ankommt (Anzeige oben rechts).
2. **VORHER aufnehmen**: Teil vor dem Drop abspielen (Breakdown/Build-up), `AUFNEHMEN` → `STOP`.
   Das Ende der Aufnahme markiert den Drop-Start (auf den Takt gerundet).
3. **NACHHER aufnehmen** (optional): Teil nach dem Drop. Der Übergang zurück wird darauf abgestimmt; die Info-Zeile zeigt die Länge des Original-Drops in Host-Takten.
4. **Einstellen**: Stil, Groove, Takte (1–32), Vorlauf, Tempo-Modus, Intensität/Aggression/Fit-in.
5. **GENERIEREN** oder **NEU WÜRFELN** (neuer Seed).
6. **Anhören**: `VORHÖREN` (sofort) oder `IM KONTEXT` – spielt synchron zu Cubase genau an der Drop-Stelle, Psy wird ab Drop-Start stummgeschaltet.
7. **Reinziehen**: einzelne Stems oder `ALLE STEMS` auf die Spuren ziehen. Dateistart = Drop-Start minus Vorlauf.

Alle Analysen werden mit dem Cubase-Projekt gespeichert. Gleiche Einstellungen + gleicher Seed = identischer Drop.

## Tempo-Modi

| Modus | Verhalten |
|---|---|
| **DnB-Tempo** | Drop wird mit ~174 BPM gerendert. Mit **Grid-Lock** wird das Tempo minimal angepasst, sodass der Drop exakt eine ganze Zahl Host-Takte lang ist (z. B. 6 DnB-Takte @ 174 = genau 5 Takte @ 145). |
| **Host-Tempo** | Drop läuft im Projekttempo mit Halftime-Feel. |

Die WAVs enthalten bewusst **keine Tempo-Metadaten** – Cubase importiert sie ohne Musical Mode, das DnB-Tempo bleibt erhalten.

## Stems

`Kick` · `Snare` · `Hats` · `Neuro` (Reese/Growl/Stab/Dive/Wobble) · `Sub` · `FX` (Riser, Impact, Zaps, Reverse-Swell) + `Mix`.
Alle Stems teilen sich einen gemeinsamen Limiter-Verlauf: zusammen ergeben sie exakt den Mix (≤ −1 dBFS).
Gespeichert unter `Dokumente/NeuroDrop/Renders/`.

## Installation

| System | Ordner |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3\` |
| macOS | `~/Library/Audio/Plug-Ins/VST3/` – danach im Terminal: `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/NeuroDrop.vst3` |
| Linux | `~/.vst3/` |

macOS-Build ist Universal (Apple Silicon + Intel).

## Bauen

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target NeuroDrop_VST3 NeuroDropTest
./build/NeuroDropTest_artefacts/Release/NeuroDropTest   # Analyse- und Render-Tests
```

Linux-Pakete: `libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev libgl1-mesa-dev`.
GitHub Actions baut Windows, macOS (Universal), Linux x64 und ARM64, testet mit `pluginval` und hängt bei Tags `v*` die ZIPs an ein Release.

Hinweis: JUCE 8 steht unter AGPLv3 bzw. kommerzieller JUCE-Lizenz – für einen Verkauf wird eine JUCE-Lizenz benötigt.
