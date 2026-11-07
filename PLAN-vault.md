> **Plik tymczasowy.** Plan rozbudowy na czas pracy. Do usunięcia po zakończeniu etapu 7.
>
> **Stan na 2026-11-06, gałąź `vault`:**
>
> | Etap | Stan |
> |---|---|
> | 0. Domknięcie obecnej pracy | gotowe, commit `1a7b57d` |
> | 1. Pasek boczny | w trakcie: commit na `vault` jako praca w toku, czeka na sprawdzenie zwijania i odświeżania drzewa |
> | 2. Tryb vaulta i indeks | do zrobienia; działa już wybór folderu vaulta (`vault/path`) i jego drzewo |
> | 3. Linki w edytorze | do zrobienia |
> | 4. Prawy panel, backlinki, wyszukiwanie, zmiana nazwy | do zrobienia |
> | 5. Tagi | do zrobienia |
> | 6. Kalendarz | do zrobienia |
> | 7. Graf | do zrobienia |
>
> Zmiana względem planu: sekcja Vault jest widoczna zawsze, bez vaulta pokazuje zachętę do wyboru folderu.
>
> Otwarte: drzewo Files nie odświeża się po wybraniu katalogu do czasu zmiany fokusu. Nie udało się tego odtworzyć bez
> prawdziwego ekranu. Dodane jest przerysowanie po `directoryLoaded`, ale nie wiadomo, czy to naprawia problem.

# Pasek boczny w trzech sekcjach, vault zgodny z Obsidianem i kalendarz

## Kontekst

MDEditor był celowo edytorem bez vaultów (`CLAUDE.md`: „No vaults”). Właściciel zmienia tę decyzję: program ma domyślnie
działać jak dotąd, jako zwykły edytor Markdown, a po włączeniu dawać funkcje vaulta jak w Obsidianie. Dzisiejszy
`FolderPanel` to jedno drzewo, a `[[linki]]` otwierają tylko plik względem bieżącego folderu.

Ustalone decyzje:

- **Pasek boczny:** trzy sekcje w jednym widoku, w stylu paska Windows: Ulubione, Vault, Pliki.
- **Zakres vaulta:** linki z podpowiedziami, backlinki, wyszukiwanie, tworzenie z linku, aktualizacja linków przy zmianie
  nazwy, tagi oraz graf.
- **Kalendarz:** notatki dzienne `YYYY-MM-DD.md` oraz daty i godziny z tekstu i frontmatter.
- **Zgodność:** ten sam folder działa w Obsidianie. MDEditor nie zapisuje niczego w `.obsidian` ani obok notatek.
- **Przed startem:** usunąć niebieskie strzałki z karty Strzałki i zrobić commit obecnej pracy.

Założenie projektowe: każdy etap to osobny commit na gałęzi `vault`. Po każdym etapie zatrzymuję się na sprawdzenie
przez właściciela. Commit etapów 1–7 tylko na jego prośbę.

## Etap 0: domknięcie obecnej pracy

1. W `src/app/SymbolSets.cpp` usunąć z karty Arrows znaki rysowane jako emoji (`↔ ↕ ↖ ↗ ↘ ↙ ↩ ↪ ◀ ▶`). Sprawdzić renderem.
2. Testy i `dist/`, commit na `main`: panel symboli, naprawa emoji, podpowiedzi z encjami.
3. Nowa gałąź `vault`.

## Etap 1: pasek boczny w trzech sekcjach

- Nowy `src/panels/Sidebar`, który zastępuje `m_folderPanel` w `MainWindow`: pionowy `QSplitter` ze zwijanymi sekcjami
  i zapamiętanymi proporcjami (`saveState`, jak `window/splitter`).
- Sekcje, od góry:
  - **Ulubione:** nowy `FavoritesPanel`, lista folderów w stylu paska Windows. Klik otwiera folder w sekcji Pliki.
  - **Vault:** druga instancja `FolderPanel`, widoczna tylko przy włączonym vaulcie.
  - **Pliki:** obecny `FolderPanel`, bez zmiany działania.
- Dodawanie do ulubionych z menu kontekstowego drzewa i przyciskiem, usuwanie i zmiana kolejności na liście.
- Trwałość: `sidebar/favorites` w `AppSettings`, czyli w profilu użytkownika, więc przetrwa aktualizacje.
- Nie sprawdzać istnienia ulubionych w głównym wątku: mogą leżeć na dysku sieciowym (`runInBackground` z
  `src/app/Background.h`). Dotyczy to też folderu zamontowanego przez `CloudDrive`.
- `MainWindow` przestaje znać `FolderPanel` bezpośrednio: `rootPath`, `setRootPath`, `selectFile` idą przez `Sidebar`.

## Etap 2: tryb vaulta i indeks

**Tryb.** Domyślnie wyłączony. Włącza go strona ustawień **Vault** i **File → Open Vault…**. Jeden aktywny vault:
`vault/enabled`, `vault/path`. Funkcje vaulta działają tylko dla plików wewnątrz vaulta; reszta plików zachowuje się
jak dotąd.

**Parser bez Qt:** `src/markdown/NoteParser` w `mdcore`, wyciąga z jednej notatki frontmatter (`aliases`, `tags`, `date`),
nagłówki, linki, tagi i daty. Korzysta z `parseLine` i `parseInline`, więc kod i URL-e są automatycznie pomijane.

**Indeks:** `src/vault/VaultIndex` w aplikacji:

- skan w tle przez `runInBackground`, bez `.obsidian`, `.trash` i folderów ukrytych,
- po zapisie aktualizacja tylko jednego pliku, przy powrocie do okna przeskan po dacie i rozmiarze (bez
  `QFileSystemWatcher`, ta sama zasada co w `EditorTab::checkDisk`),
- pamięć podręczna w `AppLocalDataLocation/vaults/<hash ścieżki>/index.json`, nigdy w samym vaulcie.

**Rozwiązywanie linków** jak w Obsidianie: nazwa bez rozróżniania wielkości liter, ścieżka częściowa, alias, a przy
kilku trafieniach najkrótsza ścieżka. Czysta funkcja w `mdcore`, żeby testować ją tabelami przypadków.

## Etap 3: linki w edytorze

- `MarkdownEditor` dostaje funkcję rozwiązującą link, ustawianą przez `EditorTab`. Bez niej `openLink` i `resolvePath`
  działają jak dotąd, więc tryb zwykłego edytora się nie zmienia.
- `[[nota]]`, `[[nota|alias]]`, `[[nota#nagłówek]]`, `![[obraz.png]]` po nazwie w całym vaulcie. `#nagłówek` przewija do
  nagłówka. `InlineParser` obsługuje już wiki linki i alias, tu dojdzie sekcja `#`.
- Nierozwiązany link ma inny kolor, a klik tworzy notatkę.
- Podpowiedzi po wpisaniu `[[`: `QCompleter` na liście z indeksu, z nagłówkami po `#` i aliasami.

## Etap 4: prawy panel, backlinki, wyszukiwanie, zmiana nazwy

- Nowy `src/panels/NotePanel`, trzecia kolumna głównego splittera, tylko w trybie vaulta, **View → Right Panel**.
- **Backlinki:** notatki linkujące do bieżącej, z linią kontekstu; klik przechodzi do miejsca.
- **Wyszukiwanie w vaulcie:** `Ctrl+Shift+F`, w tle, wyniki z kontekstem.
- **Zmiana nazwy i przenoszenie:** z menu kontekstowego sekcji Vault. Najpierw pokazuje, ile plików i linków się zmieni, i
  pyta. Podmienia wyłącznie cel linku, zachowując BOM, CRLF i alias (`Document::encode`). Otwarte karty aktualizuje przez
  edytor, w tym karty z niezapisanymi zmianami, z zachowaniem cofania. Pliki zmienione poza aplikacją pomija i zgłasza,
  zgodnie z ochroną z `Document::check`. Na koniec raport, co się nie udało.

## Etap 5: tagi

- `#tag` i `#tag/podtag` w `InlineParser` jako nowy styl, poza kodem i URL-ami, oraz `tags:` z frontmatter.
- Styl tagu widoczny tylko w notatkach vaulta.
- Karta **Tags** w `NotePanel`: drzewo z licznikami, klik pokazuje notatki z tagiem.

## Etap 6: kalendarz

- Karta **Calendar** w `NotePanel`: podklasa `QCalendarWidget`, która w `paintCell` oznacza dni z notatką dzienną i dni z
  wydarzeniami, a pod nią lista wydarzeń wybranego dnia, posortowana po godzinie.
- Klik w dzień otwiera notatkę dzienną, a jeśli jej nie ma, tworzy ją.
- Ustawienia notatek dziennych: folder i format czytane tylko do odczytu z `.obsidian/daily-notes.json`; bez pliku domyślnie
  katalog główny i `YYYY-MM-DD`. Nieobsługiwane tokeny formatu dają domyślny format i komunikat.
- Daty z treści: `YYYY-MM-DD`, opcjonalnie `HH:MM`, oraz `date:` z frontmatter. Bez bloków kodu i URL-i. Klik w wydarzenie
  otwiera notatkę w tej linii.

## Etap 7: graf

- Nowe `src/panels/GraphWindow`: osobne okno, **View → Graph**. Osobne, bo `MainWindow` zakłada, że każda karta to
  `EditorTab` (`tabAt`, `currentEditor`).
- Widok rysowany w `QPainter` z układem siłowym na timerze, przybliżanie, przesuwanie, klik otwiera notatkę, kolor dla
  nierozwiązanych linków, filtr tagu.
- Układ O(n²) testuję na vaulcie 2000 notatek. Przy gorszym wyniku ograniczę liczbę iteracji lub widok do sąsiadów.

## Dokumentacja

W każdym etapie aktualizować `README.md` i `CLAUDE.md`. Decyzję „No vaults” zastąpić opisem trybu opcjonalnego. Dodać
`docs/vaults.md`: zgodność z Obsidianem, gdzie MDEditor trzyma dane, czego nie obsługuje (m.in. wtyczek Obsidiana i
Canvas).

## Poza zakresem pierwszej wersji

Wiele aktywnych vaultów jednocześnie, szablony, `[[nota#^blok]]`, Canvas, wtyczki Obsidiana, synchronizacja, pełny język
zapytań wyszukiwania.

## Weryfikacja

- `mdcore_tests`: `NoteParser` i rozwiązywanie linków tabelami przypadków, z kodem, URL-ami, aliasami, polskimi nazwami i
  kilkoma plikami o tej samej nazwie. Daty z niepoprawnymi wartościami, np. `2026-13-45`.
- `app_tests` na vaulcie w `QTemporaryDir`: skan, indeks po zapisie, backlinki, zmiana nazwy zachowująca bajty (BOM, CRLF),
  konflikt przy zmianie nazwy, kalendarz, ulubione po ponownym utworzeniu obiektów jak po aktualizacji.
- **Regresja trybu edytora:** przy wyłączonym vaulcie render identyczny co do piksela z obecną wersją i brak plików
  tworzonych w folderze użytkownika.
- **Zgodność:** test, że praca w vaulcie nie zmienia niczego w `.obsidian` i nie dodaje plików do vaulta.
- Po każdym etapie: testy mutacyjne kluczowych zachowań, render na prawdziwych czcionkach przez `WA_DontShowOnScreen` w
  obu motywach, paczka w `dist/`.
- Wydajność: skan i wyszukiwanie na syntetycznym vaulcie 2000 notatek, interfejs bez blokad.

Do ręcznego sprawdzenia przez właściciela:

- otwarcie tego samego folderu w prawdziwym Obsidianie, bo tutaj Obsidiana nie ma,
- `QCompleter` i praca paska na prawdziwym pulpicie,
- vault na zamontowanym Dysku Google.

Windows sprawdzi dopiero CI, a to wymaga `git push origin main`, którego w tej sesji nie można wykonać.
