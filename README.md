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

1. Hämta `SerialESPHelper-windows-x64.zip` från en lyckad Actions-körning
   eller en publicerad release. Actions-artefakten innehåller även
   `SHA256SUMS.txt`.
2. Packa upp **hela** ZIP-filen i en skrivbar katalog. Starta
   `SerialESPHelper.exe` därifrån; flytta inte EXE-filen ensam. Ingen
   installation krävs. USB-drivrutin för USB-till-serial-adaptern kan behövas.
3. Välj en profil eller klicka **New profile** för att skapa en. Skriv in
   portnamnet, till exempel `COM7`, eller välj en upptäckt port. Portlistan
   uppdateras automatiskt, men portfältet är redigerbart så att en frånkopplad
   port kan anges i förväg. Baudrate är också redigerbar.
4. Standardinställningen är 115200 baud, 8 databitar, ingen paritet, 1 stoppbit,
   ingen flödeskontroll och **Read-only**. Öppna **Advanced…** för fler
   serialinställningar och loggning. Kontrollera DTR/RTS: de kan påverka reset
   på ESP-kort. Spara med **Save profile** för att behålla ändringarna i INI-filen.
5. Klicka **Connect / Resume** för att ansluta. Inkommande data visas i
   terminalen och anslutningstillstånd/fel visas under statusraden och i
   händelselistan längst ned. **Manual pause** kopplar från och blockerar
   automatisk återanslutning tills du klickar **Connect / Resume** igen.
6. För att skicka data, avmarkera **Read-only**, skriv text och välj
   radslut (`none`, `lf`, `cr` eller `crlf`). Markera **Hex bytes** för att
   skicka hexadecimala bytepar, till exempel `41 00 FF`. Lokal echo är valfri.
   Read-only skyddar bara mot sändning; det stoppar inte mottagning.
7. Använd **New window** för ytterligare en enhet. Varje fönster har en egen
   serialsession. **Clear rendered view** rensar bara terminalvisningen, inte
   mottagningen eller loggfilerna. Stäng av **Follow scroll** för att läsa äldre
   utdata medan mottagning och loggning fortsätter.

### Automatisk anslutning och profiler

När en profil väl har anslutit återansluter sessionen automatiskt efter
frånkoppling eller omstart av enheten. Detta gäller även om automatisk anslutning
är avstängd. **Manual pause** blockerar återanslutning tills du uttryckligen
klickar **Connect / Resume** igen.

Öppna **Advanced…** och aktivera automatisk anslutning om profilen dessutom ska
ansluta när dess fönster öppnas och försöka igen om den första anslutningen
misslyckas. Spara profilen för att behålla valet. Standard är av: fönstret
ansluter inte vid start, och ett manuellt första anslutningsförsök görs en gång
om porten inte kan öppnas. Återförsök efter att en session har varit ansluten
begränsas av profilens `retryLimit`.

När enheten har ett USB-serienummer och VID/PID binds profilen till den
identiteten första gången den ansluter. Då följer återanslutningen enheten även
om COM-numret ändras. **Forget USB binding** rensar bindningen; spara profilen
för att behålla ändringen. Saknad eller tvetydig identitet leder inte till att
programmet väljer en annan enhet godtyckligt. Utan unikt serienummer används
portnamnet, vilket inte garanterar fysisk identitet om operativsystemet byter
COM-nummer.

Inställningar ligger i `SerialESPHelper.ini` bredvid programmet. **Save INI as…**
väljer en annan plats uttryckligen; programmet flyttar inte inställningar i
bakgrunden. `example.ini` dokumenterar profilnycklar och standardvärden.

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
acceptansmatris, planerade förbättringar och en enkel rutin för att fylla på
backloggen.

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