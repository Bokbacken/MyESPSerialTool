# Serial ESP Helper — 0.27.0

Portabel serialterminal för ESP-enheter, primärt Windows x64. Terminal,
trafikloggning, uttrycksfilter och separat esptool-diagnostik finns. Ingen Python,
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
   Fönstertiteln och statusraden längst ned visar versionsnumret.
   Om Expression-väljaren saknas och
   titeln inte visar 0.27.0 körs en äldre EXE. Stäng den och starta EXE-filen
   direkt från det nyuppackade paketet, inte via en gammal genväg.
   Vid uppdatering: stäng programmet och behåll din `SerialESPHelper.ini`.
   Om Qt/runtime/plugins är oförändrade räcker det normalt att ersätta EXE
   och guiden. Annars uppdaterar du hela paketet utan att skriva över INI.
3. Den första profilen öppnas automatiskt. Öppna andra med **File → Open profile**
   eller skapa en ny med **File → New profile…**. Skriv in
   portnamnet, till exempel `COM7`, eller välj en upptäckt port. Portlistan
   uppdateras automatiskt, men portfältet är redigerbart så att en frånkopplad
   port kan anges i förväg. Baudrate är också redigerbar.
4. Standardinställningen är 115200 baud, 8 databitar, ingen paritet, 1 stoppbit,
   ingen flödeskontroll och **Read-only**. Använd **Settings → Connection…**
   och **Settings → Logging…** för fler inställningar. Kontrollera DTR/RTS: de kan påverka reset
   på ESP-kort. Spara med **File → Save profile** för att behålla ändringarna i INI-filen.
5. Klicka **Connect** (F5) för att ansluta. Inkommande data visas i
   terminalen. Anslutningstillstånd visas i den färgade statusetiketten och fel
   i händelseloggen under terminalen. **Pause** (F6) kopplar från och blockerar
   automatisk återanslutning tills du klickar **Connect** igen.
6. För att skicka data, avmarkera **Read-only**, skriv text och välj
   radslut (`none`, `lf`, `cr` eller `crlf`). Markera **Hex bytes** för att
   skicka hexadecimala bytepar, till exempel `41 00 FF`. Lokal echo är valfri.
   Read-only skyddar bara mot sändning; det stoppar inte mottagning.
7. Öppna ytterligare en profil för nästa enhet. Varje session har en egen
   serialanslutning. **Edit → Clear terminal view** rensar bara terminalvisningen, inte
   mottagningen eller loggfilerna. Stäng av **Follow new output** för att läsa äldre
   utdata medan mottagning och loggning fortsätter.

### Automatisk anslutning och profiler

**Session → Reconnect automatically after connection loss** styr återanslutning
för just denna session. Den är på som standard och kan ändras även
under anslutning utan att porten öppnas om. Avmarkering medan sessionen väntar
på en bortkopplad enhet avbryter väntan. **Pause** blockerar alltid
återanslutning tills du uttryckligen klickar **Connect / Resume** igen.
Spara med **File → Save profile** för att behålla inställningen (`autoReconnect`).

Öppna **Settings → Connection…** och aktivera automatisk anslutning om profilen dessutom ska
ansluta när dess session öppnas och försöka igen om den första anslutningen
misslyckas. Spara profilen för att behålla valet. Standard är av: fönstret
ansluter inte vid start, och ett manuellt första anslutningsförsök görs en gång
om porten inte kan öppnas. Återförsök efter att en session har varit ansluten
begränsas av profilens `retryLimit`.

När enheten har ett USB-serienummer och VID/PID binds profilen till den
identiteten första gången den ansluter. Då följer återanslutningen enheten även
om COM-numret ändras. **Session → Forget USB binding** rensar bindningen; spara profilen
för att behålla ändringen. Saknad eller tvetydig identitet leder inte till att
programmet väljer en annan enhet godtyckligt. Utan unikt serienummer används
portnamnet, vilket inte garanterar fysisk identitet om operativsystemet byter
COM-nummer.

### Installation med MSI (aktuell användare)

Som alternativ till ZIP finns `SerialESPHelper-windows-x64.msi`.
Stäng programmet och kör MSI-filen. Den installerar för **bara aktuell
användare**, utan begäran om administratörsrättigheter, under
`%LOCALAPPDATA%\Programs\SerialESPHelper` och skapar en startmenygenväg.
USB-drivrutiner ingår inte och kan fortfarande kräva separat installation.
Paketet är osignerat; Windows kan visa en varning om okänd utgivare.

Vid första normala start skapar appen sin INI bredvid den installerade EXE:n.
Nya installerade profiler använder **Dokument\SerialESPHelper\Logs** för
loggar, via Windows användarspecifika Dokument-mapp (även om den är omdirigerad).
ZIP-versionen behåller `logs` relativt INI-filens katalog.
Sparade sökvägar i en befintlig INI ändras aldrig vid uppgradering.
För att flytta från ZIP: stäng programmet och kopiera din befintliga INI
till installationsmappen före första start, eller använd `--config`.

MSI äger inte INI, backup eller loggar: de **bevaras vid uppgradering och
avinstallation**. Ett senare MSI-paket uppgraderar den befintliga
installationen; nedgradering blockeras. Avinstallera via Windows appar.
Kvarvarande INI/loggar kan tas bort manuellt om du vill.
MSI och ZIP innehåller samma app, DLL:er, licenser och motsvarande Qt-källor.

## Programöversikt och menyer

Från 0.27.0 har programmet ett huvudfönster med vanliga menyer. Varje profil
öppnas som en egen session i ett underfönster. **Window** ordnar sessionerna
sida vid sida (**Tile**), överlappande (**Cascade**) eller som flikar
(**Show as tabs**). Menyerna gäller alltid den aktiva sessionen, och
kortkommandon som F5 påverkar aldrig en session i bakgrunden.

Varje session har en kompakt verktygsrad: **Port**, **Baud**, **Connect**,
**Pause**, **Split log**, **Read-only**, **Diagnostics** och **Flash…**.
Under den visas en färgad statusetikett (grön = ansluten), USB-bindning och
loggstatus. Terminalen följs av händelseloggen, filterraden och sändraden.

| Meny | Innehåll |
|------|----------|
| **File** | **Open profile** (öppnar profilen i en ny session eller aktiverar en redan öppen), **New profile…** (Ctrl+N), **Save profile** (Ctrl+S), **Save INI as…**, **Export terminal text** (hela bufferten, markering, filtrerade rader), **Open log folder**, **Close session** (Ctrl+W), **Exit** |
| **Edit** | **Copy**, **Select all**, **Filter lines…** (Ctrl+F), **Clear terminal view** |
| **View** | **Follow new output**, visa/dölj **Filter bar**, **Send bar** och **Event log** |
| **Session** | **Connect / Resume** (F5), **Pause** (F6), **Split log** (F7), **Read-only**, **Reconnect automatically**, **Forget USB binding** |
| **ESP** | **ESP diagnostics…** (F9), **Flash firmware…** |
| **Settings** | Profilens **Connection…**, **Logging…**, **Terminal…**, **ESP tools…** samt gemensamma **Global settings…** |
| **Window** | **Tile**, **Cascade**, **Show as tabs**, nästa/föregående session och lista över öppna sessioner |
| **Help** | **Filter syntax**, **About** (version, INI-sökväg) |

**Split log** stänger den aktuella loggserien och startar direkt en ny utan att
porten stängs, så ingen mottagen byte går förlorad och enheten återställs inte.
Byten före delningen hamnar i den gamla filen och byten efter i den nya.
Terminalen visar en markeringsrad med tid och namnet på den nya loggserien.
Nya filnamn följer namnmallen; om mallen ger samma namn läggs `-1`, `-2` osv. till.
Funktionen är aktiv endast när sessionen är ansluten.

### ESP-diagnostik med esptool

Välj först den officiella, separat installerade `esptool.exe` i
**Settings → Global settings… → esptool executable**. Verktyget
buntas inte med appen. **ESP → ESP diagnostics…** kontrollerar verktygets version
innan det körs och stöder esptool 4.x och 5.x. **Save** i dialogen sparar
sökvägen direkt i INI-filen. Profilspecifika undantag finns under
**Settings → ESP tools…**. Diagnostikvyn har också **Save global esptool path to INI**;
en ändrad sökväg märks **NOT saved** tills du sparar.
Från 0.26.1 känns även chipraden `Chip type:` i esptool 5.3.1 igen,
utöver det äldre `Chip is`-formatet. Meddelandet att ESP32-S3 saknar chip-ID
och att MAC läses i stället är normalt och blockerar inte chipidentifieringen.

Diagnostikfönstret kan visa chip-/revisions- och flash-ID-information, läsa
ESP-IDF:s partitionstabell från adressen i **Explicit partition table address**
(standard `0x8000`), läsa ett uttryckligt
adressintervall eller spara en hel vald partition, inklusive en eventuell
coredump-partition. Rådumpar verifieras mot förväntad storlek och skrivs
atomiskt till målfiler. Förlopp, fullständig stdout/stderr och exitstatus visas.

**Flashläsning är inte reset-fri.** Före varje enhetsoperation visas en
bekräftelse med **Cancel** som standard. esptool kan starta om ESP:n till
bootloaderläge och tillbaka; körande firmware och serialinsamling avbryts,
och osparat tillstånd i RAM kan gå förlorat. **Chip + flash info** kör två
kommandon och kan ge flera resetcykler, liksom **Read layout + OTA selection**.
Omstarter kan dessutom påverka pågående OTA-rollback. Versionskontrollen påverkar inte
enheten och kräver därför ingen sådan bekräftelse. Bootloaderläge kan inte
avgöras tillförlitligt från terminaltrafik; tidigare diagnostik eller
bootmeddelanden gör inte nästa operation reset-fri.

Porten stängs exklusivt innan esptool startas. Efter avslut eller fel återställs
den tidigare anslutningsavsikten; **Pause** under en operation avbryter
verktyget och hindrar återanslutning. All inmatning skickas som separata
`QProcess`-argument, aldrig via ett shell. Diagnostikvyn har bara chip-/flash-ID
och flashläsningskommandon samt den särskilt granskade flashplanen nedan; det
finns ingen fristående erase-funktion eller automatisk OTA-startväxling. En rå
coredump sparas som bytes; avkodning kräver separat matchande ELF och ingår
inte här.

### OTA-layout och kontrollerad flashning (etapp 4)

**Read layout + OTA selection** läser först partitionstabellen och sedan
de två sektorerna i en entydig, standardiserad `otadata`-partition.
Partitionslistan och utdataloggen visar OTA0/OTA1:s startadresser och storlekar
från den faktiska tabellen, inte hårdkodade exempel. OTA0 har subtype `0x10`,
OTA1 `0x11`. Tabellens MD5 kontrolleras när den finns; överlapp och felaktig
alignment avvisas.

Vyn visar båda `otadata`-posternas sekvens, tillstånd och CRC samt
**ESP-IDF metadata candidate**, när uppgifterna går att tolka.
Det är **inte bevis på körande eller senast körd partition**:
rollback, anti-rollback och bildvalidering kan välja en annan app.
NEW/PENDING_VERIFY markeras särskilt. Tomma, korrupta eller okända metadata
ger ingen säker kandidat; krypterad, tvetydig eller anpassad OTA-layout tolkas
inte automatiskt. Ingen partition betecknas därför automatiskt som säker
att skriva över. Uppgifterna är en läst ögonblicksbild, inte kontinuerlig övervakning.

**Flash firmware...** återanvänder chip och flashstorlek från senaste
**Chip + flash info** i samma diagnostikfönster. Källan till förvalen visas;
uppgifterna är sessionsbundna ögonblicksbilder, inte sparade enhetsegenskaper.
En ny informationsläsning rensar tidigare förval, så ett misslyckat försök
inte lämnar gamla värden som nya. Chip och flashstorlek kontrolleras alltid
på nytt före skrivning.

För en vanlig merged-fil: kör **Chip + flash info**, öppna **Flash firmware...**,
välj **Add merged BIN...**, granska **Inspect plan** och fortsätt med
**Verify device and flash…**. Filrollen blir Merged image, flashadressen
föreslås som `0x0` och bildhuvudets offset som `0x1000` för ESP32/S2,
annars `0x0`. Förslagen måste stämma med hur filen skapades; granskningen
kontrollerar det valda bildhuvudet men inte hela det inbäddade paketet.

Planen kan fortfarande granskas utan en ansluten enhet. Chip och vanliga
flashstorlekar väljs i dropdowns; saknas läst information måste de väljas
manuellt. Flashstorlek, adresser och bildhuvudets offset accepterar även egna
numeriska värden (decimal eller `0x`-hex). Adressförslag beror på filrollen
och innehåller relevanta partitioner från den lästa tabellen. Förslagen är
normala värden, inte en garanti för att de passar varje paket.
Fyra indataflöden stöds:

- ESP-IDF-byggkatalog: välj dess `flasher_args.json`; relativa filer och
  adresser hämtas från manifestet. Roller hämtas bara från uttryckliga
  `app`/`bootloader`/`partition-table`-poster; övriga filer förblir opak data.
- Separata Arduino-/PlatformIO-BIN: lägg till varje fil och välj roll och
  flashadress uttryckligen. Filnamnet avgör aldrig rollen.
- En app-BIN: välj **App** och välj önskad inläst app-partition i dropdownen;
  startadressen fylls i automatiskt. Välj uttryckligen OTA0, OTA1 eller **båda OTA-slotarna**.
  "Båda" skriver samma app-BIN en gång till varje explicit OTA-partitionsstart;
  det gissar inte vilken partition som är inaktiv.
- Merged/full image: välj **Merged image**, startadress och det explicita
  bildhuvudets offset **inne i filen** (exempelvis `0x1000` om bootloadern
  börjar där). Bara den valda ESP-bilden valideras; alla inbäddade bilder och
  partitionstabellen är **inte** verifierade. Hela filspannet inklusive
  padding visas som potentiellt berört.

**Inspect plan** visar filstorlekar, SHA-256, skrivintervall och erase-sektorer.
App-/bootloaderhuvuden kontrolleras mot valt chip samt XOR-checksumma och
eventuell bifogad SHA-256. Detta är inte signatur- eller hårdvaruverifiering.
En separat partitionstabell i paketet används för appens storlekskontroll i
stället för den gamla lästa layouten. Saknas layout är partitionspassning
uttryckligen overifierad. Överlapp, fel chip, trasiga bilder, saknade/tomma filer
och intervall utanför angiven kapacitet avvisas. Filer är begränsade till 1 GiB.
**Recheck file fingerprints** upptäcker ändrade filer; ändrade indata kräver
ny förhandsgranskning. Manifestets flashläge/frekvens och övriga skrivargument
används inte; esptool använder sin automatiska flashstorleksdetektering vid
skrivning. Manifest och preview ersätter inte en fullständig kontroll av
enhetens säkerhetskonfiguration, signaturer eller kompatibilitet.

**Verify device and flash…** öppnar först resetvarningen (om den är aktiverad),
kontrollerar sedan ansluten chipmodell och flashkapacitet med esptool och
stoppar skrivningen vid avvikelse eller om kapaciteten inte exakt matchar
planen. Filerna och SHA-256 kontrolleras igen precis före skrivning. Icke-app-
roller (bootloader, partitionstabell, merged image och opak data) kräver en
extra rollvarning. Därefter krävs alltid en separat slutbekräftelse med exakta
filer, adresser, storlekar och erase-sektorer; den kan inte stängas av i INI.
Alla valda filer skickas i en `write_flash`-kommandokörning. Vid fel eller
avbrott rapporteras att fullständigt resultat inte kunde bekräftas; flash kan
vara delvis ändrat och enheten inte startbar. Kontrollera att ström och kabel
är stabila.

Flashfönstret växlar automatiskt till **Live device log** när en operation
startar. Där visas samma stdout/stderr och fel som i diagnostikfönstret,
utan att växla fönster. Status, förlopp och **Cancel device operation** finns
direkt i flashfönstret; slutbekräftelserna öppnas framför det. Planens indata
är låsta medan operationen körs. Avbrytning eller stängning under körning
begär avbrott och behåller resultatet synligt tills du stänger fönstret igen.
Fliken **Plan preview** behåller den granskade planen.

Flashprogrammering kan starta om enheten och avbryta firmware och serial capture.
Den ändrar aldrig `otadata` eller OTA-startflaggan automatiskt. Att skriva
OTA0/OTA1 är inte samma sak som att välja vilken som startar; ändra startval
separat endast när du uttryckligen vet att det är önskat. Metadata-kandidaten
är inte bevis på körande app, och app-/bootloaderhuvudvalidering bekräftar inte
signatur, kryptering, anti-rollback eller säker bootkompatibilitet.

Inställningar ligger i `SerialESPHelper.ini` bredvid programmet. **Save INI as…**
väljer en annan plats uttryckligen; programmet flyttar inte inställningar i
bakgrunden. `example.ini` dokumenterar profilnycklar och standardvärden.

### Loggningsinställningar

**Settings → Logging…** öppnar profilinställningarna direkt på loggningsfliken.
Profildialogen har flikarna **Connection**, **Logging**, **Terminal** och
**ESP tools** och kan öppnas även under pågående anslutning. **Cancel** ändrar ingenting;
**Apply to session (not saved)** tillämpar ändringarna utan att skriva INI.
**Save profile** sparar samtliga flikar och tillämpar dem. Båda pausar
sessionen; klicka **Connect / Resume** efteråt.

Profilinställningar gäller den namngivna profilen: serialport,
eventuella esptool-overrides, loggning och terminalval. De sparas i
`[profile:namn]`. Tomma loggmapp-, esptool-path- och chipfält samt baud `0`
ärver de globala värdena, som visas som grå text i de tomma fälten.
**Settings → Global settings…** är en egen dialog för standardloggmapp (`[app]`),
esptool-sökväg/förväntat chip/baud (`[esptool]`) och resetbekräftelse
(`[warnings]`), delade av alla profiler. Ändringar där sparas direkt och
gäller öppna sessioner vid nästa anslutning eller diagnostik.
Resetvarningen kan stängas av; chipavvikelse blockerar alltid skrivning och
slutbekräftelsen för flash kan aldrig stängas av.
**SESSION ONLY** markerar sådant som inte sparas: anslutningstillstånd,
terminalbuffert, sändhistorik, lästa partitioner/OTA-metadata och flashplaner.
Sparande ändrar inte andra redan öppna sessioner; nya/laddade profiler använder
det sparade innehållet. Behåll rätt INI-fil vid uppackning av ett nytt bygge.

Aktivera **Receive logging enabled** för att spara trafiken. Standard är av;
statusfältet ovanför terminalen visar tydligt om trafikloggning är av, väntar
eller är på och visar filens fullständiga sökväg. **Open log folder** öppnar
loggmappen. Loggfiler skapas när de första mottagna byten skrivs.
Loggningen avser mottagna data, inte skickade kommandon.

På loggningsfliken finns följande val:

- **Log format**: `text` (standard), `raw` eller `both`.
  Text är läsbar UTF-8 där ogiltiga bytes/kontrolltecken visas som `\xNN`;
  Raw är exakta mottagna bytes, även binära inspelningar; Both sparar båda separat.
  Gamla INI-filer med `textLog=false/true` och utan `logMode` behåller
  tidigare läge genom migrering till Raw/Both. Välj Text uttryckligen för att byta.
- **Text log extension** är `.log` som standard; **Raw exact byte-log extension**
  är `.raw`. Båda är valbara (punkt och 1–10 bokstäver/siffror).
  `.meta`, `.session` och `.lock` är reserverade; Text/Raw måste ha olika
  ändelser i Both-läge. Ändelsen avgör aldrig innehållet, det gör loggformatet.
- Mapp: absolut sökväg eller relativt INI-filens katalog.
- Filnamnsmall utan filändelse, exempelvis `{profile}-{date}-{time}`.
  Tillåtna fält är `{profile}`, `{date}`, `{time}`, `{port}`, `{serial}` och
  `{id}`. Datum/tid är datorns UTC. ID ingår **bara** om `{id}` finns i mallen.
  Vid befintligt/upptaget filnamn får en ny session ett numeriskt suffix,
  exempelvis `capture-1`, i stället för att skriva över. Aktiva loggserier låses
  mot samtidig användning. Osäkra tecken i
  profil-/enhetsnamn ersätts med understreck.
- Maximal råsegmentstorlek i byte och maximal segmenttid i sekunder.
  `0` sekunder stänger av tidsrotation. Den gräns som nås först roterar loggen;
  tidsrotation sker först när nya data tas emot, inte genom tomma timerfiler.
- Rullande lagring: antal avslutade segment att behålla **utöver den aktiva
  filen**. Vid rotation blir föregående segment exempelvis `.log.1`/`.raw.1`, därefter `.2`
  osv. Äldsta segment raderas. Gränsen gäller samma loggserie, inte andra
  sessioners loggar; metadata/textfiler är separata sidofiler.
- Ny logg vid återanslutning: av innebär fortsatt skrivning i samma loggserie
  med oförändrade rotationsgränser. På skapar en ny serie med nytt unikt namn.
  Manuell paus/ny Connect eller ändrade inställningar startar alltid en ny serie.
- Valbara **host reception UTC timestamps** för Text-loggen och terminalvyn;
  båda är **av som standard**. Välj **3 decimal places in seconds** för
  millisekunder, exempelvis `2026-01-02 12:34:56.123 UTC`.
  Prefixet läggs vid början av en text-/terminalrad och avser mottagna
  fragment från datorns klocka, inte en garanterad enhetstid.
  Terminalprefix ändrar inte ANSI-markörpositioner, filtermatchning eller
  export av hela/filtrerade bufferten. Markeringsexport sparar det markerade
  visade innehållet, inklusive eventuella prefix.
- Separat tids-/offsetmetadata för Raw och storlek på diskens skrivkö.

### Terminalbuffert, filter och export

**Settings → Terminal…** innehåller antal lagrade terminalrader, kolumngräns
och storlek på terminalens mottagningskö. Standard är 2000 rader, 4096
kolumner och 262144 byte i kön. Terminalen har dessutom ett fast tak på
1048576 textceller. Äldre rader kan försvinna ur vyn när gränserna nås;
det påverkar inte diskloggen.
Byte av loggningsinställningar rensar inte terminalbufferten. Om dess gränser
minskas tas överskjutande äldre rader/kolumner bort.

Välj **Text** för vanlig bokstavlig textsökning eller **Expression** för
uttrycken nedan. **Match case** styr skiftlägeskänslighet i både sökord och
fältnamn. Töm fältet för ofiltrerad vy. **Edit → Filter lines…** (Ctrl+F) visar filterraden och flyttar markören dit. **Highlight only** behåller alla rader
med färgmarkerade träffrader; annars visas bara träffarna som oformaterad text.
Träffräknaren avser raderna i den kvarvarande bufferten, inte hela sessionen.
**File → Save profile** sparar filtertext, läge, skiftlägesval och markering per profil.
Knappen **?** på filterraden och **Help → Filter syntax** visar skrivregler och exempel direkt i programmet.

Under **File → Export terminal text** sparar **Whole terminal buffer…** hela den kvarvarande terminalbufferten, oavsett
filter. **Selected text…** sparar den markerade texten från vyn.
**Filtered rows…** sparar enbart träffande rader. Exporten är en UTF-8-ögonblicksbild
utan ANSI-formatering, inte en exakt råbyte-logg och inte hela sessionens historik
om äldre rader redan rullat ur bufferten. Råloggning och mottagning fortsätter
oberoende av filter/export; exporten ändrar aldrig befintliga råloggar.

### Uttrycksfilter: wildcards, logik och tal

I **Expression** ska sökord stå inom dubbla citattecken. De matchar var som
helst på samma terminalrad. `?` matchar exakt ett Unicode-tecken och `*`
noll eller fler tecken. `"ord??tex*detta"` matchar exempelvis
`ordettexharvadsomhelsthärdetta`. `"\?"` och `"\*"` söker de bokstavliga
tecknen, `\"` söker citattecken och `\\` söker backslash. I **Text** är
alla tecken bokstavliga och inga citattecken eller escapes behövs.

Kombinera villkor med `!` (inte), `&&` (och), `||` (eller) och parenteser.
Prioriteten är `!` före `&&` före `||`. Exempel:

```text
("error*flash" || "warn??") && !"ignored"
temp >= 20 && temp < 30
"sensor*" && voltage >= 3.1
number(2) >= -1.5e2 && number(2) <= 100
```

`temp >= 20` läser ett fält som `temp=23.5` eller `temp: 23.5`.
Fältnamn börjar med bokstav/understreck och får innehålla bokstäver,
siffror, understreck, punkt och bindestreck (ASCII). Vid upprepade fält
används första giltiga numeriska tilldelningen.
`number(2)` läser det andra fristående talet från vänster i raden;
positioner är 1–1000. Alla fristående tal räknas, även numeriska
tidsstämpeldelar, så välj namngivna fält när loggformatet tillåter det.

Jämförelser: `<`, `<=`, `==`, `!=`, `>=`, `>`. Intervall skrivs med två
jämförelser och `&&`. Tal får ha tecken, decimalpunkt och exponent,
exempelvis `-23.5`, `.5` och `1e-3`. Decimalkomma och enheter stöds inte:
`23C` och siffrorna i `ESP32` räknas inte som fristående tal.
Saknade, ogiltiga och oändliga tal uppfyller ingen jämförelse, inte ens `!=`.
`!(temp > 20)` matchar däremot en rad där `temp` saknas, eftersom hela
jämförelsens resultat negeras. `==` använder exakt flyttalsjämförelse;
använd intervall för mätvärden med tolerans.

Syntaxfel visas med position. Ett ogiltigt filter tillämpas inte:
alla rader visas och filtrerad export är spärrad tills felet rättats.
Parsern kör ingen kod, shell eller användarstyrda reguljära uttryck.
Gränser: 4096 tecken, 128 villkor/operatorer och 32 nästlingsnivåer.
INI-filens vanliga strängregler gäller även sparade filter, se `example.ini`.
Filtren använder terminalrader efter UTF-8/ANSI-tolkning och eventuell
kolumnbrytning, aldrig råloggen. Tidsintervallsbaserad export återstår.

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
Text-loggen (`*.log` som standard) är läsbar UTF-8 med synliga ogiltiga bytes/kontrolltecken.
Textloggens valbara tidsstämplar gäller mottagna fragment, inte en påstådd
enhetstidsstämpel för varje komplett rad.
Rotation och retention kan radera äldre loggsegment enligt INI-inställningarna;
retention räknar avslutade segment utöver det aktiva.
Sessionsmetadata skrivs även när råloggning är avstängd. Storleksgränsen
räknar mottagna bytes per segment; valda Text/Raw-/offsetloggar roteras tillsammans och Text
kan bli större på grund av tidsstämplar och byte-escaping.
Skrivfel, full disk och kööverflöde visas; kontrollera varningar innan du
förlitar dig på en logg. Loggar kan innehålla privata data och laddas aldrig
upp automatiskt. Filter påverkar endast vyn/export, inte råloggen.

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
(`v0.27.0`) eller manuell körning med en befintlig versionstag bygger den
revisionen och publicerar ZIP/checksummor i ett separat behörighetsbegränsat
jobb. Manuell körning utan tagg ger enbart artefakt.
PR-jobbet har inga release-secrets eller skrivbehörigheter.

Byggena är osignerade: Windows SmartScreen kan varna. Kodsignering är en
separat framtida uppgift. Windows 10/11 x64 på ren dator måste fortfarande
verifieras manuellt; ett hosted-runner-smoketest är inte samma sak.