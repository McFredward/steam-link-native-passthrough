# Steam Link Native Passthrough

<p align="center">
  <img src="docs/img/steam-link-icon.png" width="56" height="56" alt="Steam Link">
  &nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/img/steam-frame-logo-dark.svg">
    <img src="docs/img/steam-frame-logo.svg" width="240" alt="Steam Frame">
  </picture>
</p>

<p align="center">
  <a href="README.md"><img src="docs/img/flag-en.png" width="24" alt="English">&nbsp;English</a>
  &nbsp;&nbsp;|&nbsp;&nbsp;
  <img src="docs/img/flag-de.png" width="24" alt="Deutsch">&nbsp;<b>Deutsch</b>
</p>

> [!IMPORTANT]
> **Aktuell nur für <img src="docs/img/windows.svg" width="18" height="18" alt=""> Windows-PCs mit <img src="docs/img/nvidia.svg" width="20" height="20" alt=""> NVIDIA-GPU.**
>
> Ungetestete SteamVR-Versionen lassen sich nach einer Warnung mit **„Trotzdem installieren“** ausprobieren.

Spiele unterstützte PC-VR-Spiele über Steam Link in deinem echten Zimmer.

**Aktuelles Release: [0.3.15](https://github.com/McFredward/steam-link-native-passthrough/releases/tag/v0.3.15) · Veröffentlicht am 30. Oktober 2026**

| Komponente | Release-Version |
|---|---|
| Installer | **0.3.15** |
| PC-Sender | **0.3.11** |
| Frame-Empfänger | **0.3.5** |

Die Installationszeile unten installiert dieses Release von `main` mit normalen Logs.

Für den **experimentellen Arcturus-Raumscan 0.3.16-roomscan.9** gibt es den
separaten Installer auf dem Branch `arcturus-room-setup`:

```powershell
irm https://raw.githubusercontent.com/McFredward/steam-link-native-passthrough/arcturus-room-setup/get.ps1 | iex
```

Der Installer richtet **Room Setup** im SteamVR-Dashboard der Frame ein und
aktiviert nach dem bestätigten gemeinsamen Update den schreibgeschützten
Arcturus-Leser für diese Sitzung. Nach einem Frame-Neustart bleibt der Zugriff
aus; erneutes Öffnen des geprüften aktuellen Feature-Installers aktiviert ihn
wieder. **Stop room capture** beendet ihn sofort. Firmware, Kameratreiber und
der native Kameradienst werden nicht ersetzt.

Der Raumscan ist in diesem Branch Pflichtbestandteil. Das empfohlene
Install/Update anwenden; eine zusätzliche Enable-Option gibt es nicht.
Erst nach geprüftem Kamera-Zugriff, Einrichtung der PC-Laufzeit, Modell-Warmup
und Verbindung zur Frame meldet der Installer **Room scanning is ready**.
Fehler lassen sich mit Try again wiederholen; die bestätigte native
Installation bleibt dabei erhalten.

PC-Dateien liegen unter `%LOCALAPPDATA%\steam-link-native-passthrough`, Modell
und Rechenlaufzeit im Unterordner `room-pc-runtime`. Auf der Frame liegen die
Programmdateien unter `~/.local/share/SteamLinkNativePassthrough/stream-bridge/room-setup`
und Räume unter `~/.local/share/SteamLinkNativePassthrough/rooms`.

Diese Variante berechnet den Scan auf einem **Windows-PC mit NVIDIA-GPU**.
Der Installer bereitet eine private CUDA/ONNX-Laufzeit und das Modell NVIDIA
C-Fast-FoundationStereo vor: beim ersten Mal etwa 1,7 GB Download, mindestens
5 GB freier Speicherplatz. System-Python, CUDA-Toolkit und GPU-Treiber werden
nicht geändert. Auf **PC reconstruction is running** warten und den
**Installer während des Scans geöffnet lassen**. Mindestens 3 GB freier
NVIDIA-Grafikspeicher werden benötigt; Auflösung und Kartengröße passen sich
an. Dies ist ein erster Teststand, keine vermessene GPU-Kompatibilitätsliste.

Auf der Frame **Scan a new room → Start scan** wählen und Farb-Passthrough
sichtbar lassen. Geprüfte Stereobilder und die Kopfposen zum Aufnahmezeitpunkt
werden über die authentifizierte SSH-Verbindung des Installers an den PC
übertragen. Die Frame zeichnet die zurückgesendeten Gauß-Flächen lokal:
Türkis zeigt neue Abdeckung, Bernstein fordert weitere Blickwinkel an,
unbeobachtete Lücken bleiben offen. Erneutes Betrachten von der Seite
verfeinert vorhandene Flächen. Die Vorschau verwendet geometrische,
aus Tiefendaten aufgebaute Gauß-Flächen; ein fotometrisch optimiertes
Radiance Field ist noch nicht enthalten.

**Continue** erzeugt Polygone aus gemessenen Gauß-Nachbarschaften zum Prüfen
und Speichern. **Add furniture or surfaces** bereitet dieselben Flächen zum
Markieren von Boden, Wänden, Tischen, Türen, Fenstern und Möbeln vor.
Buttons verwenden den nativen Menü-Laser; Raumecken werden mit den Aim-Aktionen
der aktiven XR-Ansicht platziert. **X oben rechts** beendet die Aufnahme und
kehrt zum SteamVR-Menü zurück; Dashboard und Eintrag bleiben erhalten.
Schließen des Installers pausiert die PC-Berechnung und erhält die aktuelle
Karte auf der Frame.

Softwareprüfungen umfassen das echte ONNX-Modell, Aufnahmezeit-Geometrie,
beidseitige Strahlprüfung, laufende Gauß-Verfeinerung, offene Messlücken,
native Vulkan-Gauß-Shader und Installation/Rollback. Geschwindigkeit und
Genauigkeit mit NVIDIA/Frame sowie die Bedienung benötigen noch den Tragetest.
Automatische Erkennung und Spiele-/Quest-Scene-API-Adapter stehen noch aus.
Siehe [PC-Rekonstruktion](docs/development/ROOM-PC-RECONSTRUCTION.md).

## Native Transparenz, kein Farbersatz

Das Spiel sendet Transparenz getrennt von seinen Farben; keine Farbe wird aus
dem Bild entfernt. Die Frame verbindet das Spiel mit ihrem eigenen Kamerabild,
auch bei Teiltransparenz; das Kamerabild bleibt auf der Brille.

## Unterstützte Spiele

| Spiel | Benötigte Mod | Mindestversion |
|---|---|---|
| Gloomhaven Digital | [GloomhavenVR](https://github.com/McFredward/GloomhavenVR) | **1.2.0** |
| Demeo | [Demeo Mixed Reality](https://github.com/McFredward/Demeo_GreenScreen) | **2.0.0** |
| Demeo Battles | [Demeo Mixed Reality](https://github.com/McFredward/Demeo_GreenScreen) | **2.0.0** |
| Demeo Battlemarked | [Demeo Mixed Reality](https://github.com/McFredward/Demeo_GreenScreen) | **2.0.0** |

Andere Spiele benötigen eine Integration; der Patch allein reicht nicht aus.

Steam Link kann mit **Auto oder fester Bitrate** verwendet werden.
**OVR Advanced Settings bei Demeo schließen**; es kann ungewollte Recenter auslösen.

## Positiv getestete SteamVR-Versionen

| Komponente | Positiv getestete Versionen |
|---|---|
| PC SteamVR | **2.18.4**, 2.17.10 |
| Steam Frame SteamVR | **2.18.2** |

## Installation

**SSH einmalig auf der Steam Frame aktivieren:**

1. Öffne **Steam-Einstellungen → System → Entwicklermodus aktivieren**.
2. Öffne **Entwickler → Benutzerpasswort festlegen** und setze ein Passwort für
   `steamos` (alternativ im Desktop-Terminal: `passwd`). Dieses Passwort brauchst
   du später im Installer; es ist unabhängig von deinem Steam-Passwort.
3. Öffne im Desktop-Modus ein Terminal und aktiviere SSH dauerhaft:

   ```bash
   sudo systemctl enable --now sshd.service
   ```

   Gib auf Nachfrage dieses Passwort ein. Bei der Eingabe erscheinen keine Zeichen.

[Valves Geräteanleitung](https://partner.steamgames.com/doc/steamhardware/steamframe/setup) zeigt die Einstellungen.
Lass die Frame eingeschaltet und im selben Netzwerk wie den PC; beende den VR-Stream.

**Auf deinem Windows-Gaming-PC ausführen — PowerShell:**

```powershell
irm https://raw.githubusercontent.com/McFredward/steam-link-native-passthrough/main/get.ps1 | iex
```

Der Assistent findet die Frame, prüft beide Geräte und führt durch die Installation.
Er lädt seine Werkzeuge selbst herunter. Gib auf Nachfrage das Frame-Passwort ein
und lass sie bis zum Abschluss eingeschaltet.

Starte deinen Steam-Link-Stream und aktiviere natives Steam-Link-Passthrough im Spiel.
Dieselbe Zeile bietet später **Update, Deinstallation oder Rücknahme** an.

## Für Entwickler

Für die Integration weiterer Spiele gibt es einen [Leitfaden](docs/development/GAME-INTEGRATION.md)
und [Erkenntnisse und Stolperfallen](docs/development/INTEGRATION-LESSONS.md).
Die [Entwicklerdokumentation](docs/DEVELOPING.md) beschreibt Architektur, Protokolle sowie Build und Tests.

## Credits

Auf Grundlage von [spyjacobs steam-frame-chroma-key](https://github.com/spyjacob/steam-frame-chroma-key).
Die Vorgeschichte und [MIT-Lizenz](LICENSE) bleiben erhalten; native Transparenz
wird hier von McFredward und Mitwirkenden entwickelt.

Inoffizielles Projekt, nicht mit Valve verbunden.
