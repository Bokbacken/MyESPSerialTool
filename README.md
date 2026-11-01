# Serial ESP Helper — 0.21.0

Portabel serialterminal för ESP-enheter, primärt Windows x64. **Etapp 1**
är denna leverans; filterparser och esptool kommer senare. Ingen Python,
.NET, installation eller administratörsbehörighet behövs för terminalen.
USB-drivrutin för den faktiska enheten kan fortfarande behövas.

## Teknikval och distribution

C++17, CMake, Qt 6 Widgets och Qt Serial Port. Qt ger fönster, serialportar,
trådar och tester utan att terminalen behöver en separat interpreter.
Dynamisk länkning är vald framför statisk enkelfils-EXE: ett portabelt ZIP
med utbytbara Qt DLL:er ger enklare LGPLv3-efterlevnad.
Qt Base och Serial Port stöder LGPLv3; inga GPL-only-moduler krävs.
Se `NOTICE.txt`, [Qt-licensiering](https://doc.qt.io/qt-6/licensing.html) och
[Windows-deployment](https://doc.qt.io/qt-6/windows-deployment.html).

Windows-paketet innehåller EXE, INI, användarguiden (denna fil), app-local
Microsoft runtime, Qt DLL:er/plugins, licenstexter och motsvarande Qt-källkod.
Källkodsarkivet gör paketet större men ska inte tas bort vid vidare distribution.
Applikationens egen kod är MIT-licensierad. Qt och Microsoft-komponenter har
egna villkor. Detta är inte juridisk rådgivning.

## Kom igång på Windows

1. Hämta `SerialESPHelper-windows-x64.zip` från en lyckad Actions-körnings
   artefakt, eller från en uttryckligen publicerad release. Actions-artefakten
   innehåller ZIP och `SHA256SUMS.txt`.
2. Packa upp **hela** ZIP-filen i en skrivbar katalog. Flytta inte bara EXE:n.
3. Starta `SerialESPHelper.exe`. Inställningar ligger normalt i
   `SerialESPHelper.ini` bredvid programmet.
4. Välj/skapa en profil. Port och baudrate är redigerbara; listorna är förslag.
   Även en port som inte finns för tillfället kan sparas. Starta separata
   sessionsfönster för flera enheter.
5. Standard är 115200, 8 databitar, ingen paritet, 1 stoppbit, ingen
   flödeskontroll och read-only. Avancerade serialinställningar finns i dialogen.
   Kontrollera DTR/RTS innan anslutning: de kan styra reset på ESP-kort.
6. Återuppta/anslut för mottagning. Manuell paus/frånkoppling förhindrar
   automatisk återanslutning tills du uttryckligen återupptar sessionen.
7. Stäng av följ senaste utdata för att läsa äldre text; mottagning och
   loggning fortsätter. Read-only blockerar användarsändning, inte mottagning.

Aktivera `autoConnect` i avancerade inställningar för övervakning och
återanslutning. Standard `false` gör ett anslutningsförsök per klick.
Ändring av read-only påverkar endast sändningsspärren; DTR/RTS ligger kvar.

Om INI-katalogen inte är skrivbar visas ett fel och du får välja en annan fil;
programmet flyttar inte inställningarna till AppData i smyg. Säkerhetskopiera
INI-filen innan du flyttar profiler mellan datorer. Se kommenterade
`example.ini` för nycklar, standardvärden och formatversion.

### Terminal, identitet och loggar

Textkodning är UTF-8 med inkrementell avkodning över läsgränser.
Ogiltiga bytes visas som `\xNN`; kontroll-/binärdata får inte
krascha terminalen. ANSI-färger och grundläggande markör/raderingssekvenser
stöds, inte en fullständig VT-emulator. Visningen och loggkön är begränsade;
begränsningar och fel visas i sessionen. GUI och disk-I/O körs åtskilda.

Automatisk anslutning övervakar portar med begränsade återförsök. När en
stabil USB-identitet (serienummer och VID/PID) finns används den framför
COM-numret. Saknad/tvetydig identitet får inte väljas godtyckligt.
Enheter utan unikt USB-serienummer måste övervakas med portnamn; det kan
inte garantera fysisk identitet efter en COM-omfördelning.

Råloggen innehåller mottagna bytes **exakt**, aldrig tidsstämplar eller
terminalens avkodade text. Metadata och valbar läsbar textlogg är separata.
Tidsstämplar avser datorns mottagningstid, inte enhetstid.
`*.session` innehåller JSON-rader för profil/inställningar, identitet,
start/slut och avbrott. Valbar `*.meta` kopplar mottagningstid till råoffset;
valbar `*.text` är läsbar UTF-8 med synliga ogiltiga bytes/kontrolltecken.
Textloggens tidsstämplar gäller mottagna fragment, inte en påstådd
enhetstidsstämpel för varje komplett rad.
Rotation och retention kan radera äldre loggsegment enligt INI-inställningarna;
retention räknar avslutade segment utöver det aktiva.
Sessionsmetadata skrivs även när råloggning är avstängd. Storleksgränsen
gäller råsegment; text/offsetlogg roteras tillsammans med råloggen och
kan bli större på grund av tidsstämplar och byte-escaping.
Skrivfel, full disk och kööverflöde visas; kontrollera varningar innan du
förlitar dig på en logg. Loggar kan innehålla privata data och laddas aldrig
upp automatiskt. Senare filter får endast påverka vyn/export, inte råloggen.

## Bygga och testa

Behöver CMake 3.21+, C++17-kompilator och dynamiskt länkad Qt 6.4+ med
Widgets, SerialPort och (för tester) Test. Windows-paketeringen använder
Qt 6.8.3 och Visual Studio 2022 x64 på `windows-2022`.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

För Visual Studio är generatorn flerkonfigurationsbaserad: använd
`cmake --build build --config Release` och `ctest --test-dir build -C Release`.
Ange `-DCMAKE_PREFIX_PATH=<Qt-installation>` om Qt inte hittas.
GUI-smoketest: `SerialESPHelper --smoke-test` (på headless Linux:
`QT_QPA_PLATFORM=offscreen`). Linux är här en utvecklings/testplattform,
inte en verifierad Raspberry Pi-distribution.

Automatiserade tester använder abstraherad/falsk serialtransport; de ersätter
inte riktiga USB-, ESP- eller Windows-hårdvarutester. Se `TODO.txt` för
acceptansmatris och kvarvarande etapper.

### GitHub Actions

`.github/workflows/build.yaml` bygger/testar Windows x64 och verifierar att
paketet startar utan Qt på utvecklings-PATH. Kod-PR:er får artefakter;
ändringar endast i dokumentation startar inte fullbygget. Workflowändringar
testar också byggflödet.

Vanliga commits publicerar **inte** releaser. Push av en versions-tag
(`v0.21.0`) eller manuell körning med en befintlig versionstag bygger den
revisionen och publicerar ZIP/checksummor i ett separat behörighetsbegränsat
jobb. Manuell körning utan tagg ger enbart artefakt.
PR-jobbet har inga release-secrets eller skrivbehörigheter.

Byggena är osignerade: Windows SmartScreen kan varna. Kodsignering är en
separat framtida uppgift. Windows 10/11 x64 på ren dator måste fortfarande
verifieras manuellt; ett hosted-runner-smoketest är inte samma sak.