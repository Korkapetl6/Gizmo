# Dedal 2

Ten sam generatywny głos co [Dedal](../dedal/README.md), ale ułożony moduł po module jak półmodularny syntezator z dwoma sekwencerami 8-bitowymi, z **patch bayem i kablami**. Nazwy gałek, przycisków i gniazd są takie jak w instrumencie, na którym się wzoruje, więc patche z tutoriali da się odtwarzać krok po kroku. Wygląd jest własny.

![Dedal 2](../docs/dedal2.png)

Pliki: **Releases** → `Dedal2-Windows-VST3.zip`, `Dedal2-macOS-VST3.zip`, `Dedal2-macOS-AU.zip`.

## Moduły

- **VCO** (sinus): FREQUENCY, EG1 AMT, SEQ1 AMT (na końcu skali: QTZ).
- **MOD VCO** (trójkąt): FREQUENCY (od LFO do ok. 1,3 kHz), EG1 AMT, SEQ2 AMT, FM AMT (thru-zero).
- **MIXER**: VCO LVL, U MIX 1 (RING), MOD VCO LVL, NOISE LVL, NOISE TONE. Powyżej połowy kanały się przesterowują (ring nie).
- **VCW**: FOLD, BIAS, EG1/CV AMT, SEQ1 AMT.
- **VCF**: CUTOFF, RESONANCE, EG1/CV AMT, SEQ2 AMT, FILTER MODE (LP → BP).
- **EG**: EG1 DECAY, EG2 DECAY, EG TRIG MIX, TRIGGER.
- **OUTPUT**: BLEND, ORDER (PARALLEL, VCW > VCF, VCF > VCW), VOLUME.
- **SEQUENCER**: dla każdego sekwencera 8 bitów (z przełącznikiem **16 STEPS** – 16, a z CHAIN SEQ razem 32), CORRUPT, CV RANGE (do ±5 V, czyli pięć oktaw), BIT FLIP, BIT SHIFT, LENGTH. Wspólne: RUN, RESET, ADVANCE, CHAIN SEQ, BUFFER.

Różnice wobec sprzętu: TEMPO to podział tempa Abletona (lista 1/1 … 1/32), a tryb kwantyzacji wybierasz z listy zamiast kombinacją przycisków. Dodatkowe małe przyciski: ZAPIS +1 / ZAPIS 0 (przesunięcie głowicy zapisu), SHIFT 0 (cofa obroty), LOSUJ.

## Patch bay

19 wejść i 12 wyjść (gniazdo MIDI jest tylko narysowane, MIDI idzie przez Abletona).

- **Kabel**: przeciągnij od jednego gniazda do drugiego (wyjście ↔ wejście, w dowolną stronę).
- **Wyjęcie**: kliknij wejście, w którym siedzi kabel. **Wyjmij kable** usuwa wszystkie.
- Z jednego wyjścia może iść wiele kabli, do wejścia wchodzi jeden.
- Kable niosą dźwięk i sterowanie, liczone co próbkę.

Połączenia domyślne, które kabel przerywa:

| Wejście | Domyślnie |
| --- | --- |
| FOLD, CUTOFF | EG1 (przez gałkę EG1/CV AMT, która tłumi też sygnał z kabla) |
| VCW VCA CV, VCF VCA CV | EG2 |
| U MIX 1 (RING) | ring modulator (VCO × MOD VCO) |
| VCW IN, VCF IN | według ORDER |
| CLOCK 1 | zegar wewnętrzny |
| CLOCK 2 | to samo, co taktuje SEQ1 |
| TRIGGER | wyzwala EG1 i EG2; gdy EG2 TRIG ma kabel, tylko EG1 |

Wyjścia: VCA, M VCO, NOISE, MIXER, EG1, EG2, U MIX 1+2, SEQ1 CV, SEQ1 TRIG, SEQ2 CV, SEQ2 TRIG, CLOCK.

## Reszta

Nuty wysyła jako MIDI (ustawienie ścieżki jak w Dedalu), ostatnie takty można przeciągnąć jako klip, presety zapisują gałki, sekwencje i kable w `Dokumenty/Dedal 2/Presety`. Nuta z klawiatury ustawia tonikę (FREQUENCY VCO).

## Czego nie sprawdziłem

Testy offline przechodzą (normalizacje, zegar z kabla, RESET, audio przez kabel, strojenie, zapis kabli). Wtyczka nie była uruchomiona w Abletonie i nie porównywałem jej ze sprzętem: zakresy gałek i charakter brzmienia są przybliżone według instrukcji.

## Presety z instrukcji

Dwanaście presetów fabrycznych (*A Simple Start* … *The Ooze*) odtwarza przykładowe patche z instrukcji sprzętu. Pozycje gałek zmierzyłem z rysunków (kąt wskaźnika każdej gałki), kable, przełącznik ORDER, CHAIN, długości i włączone bity przepisałem z rysunków, a skale z notatek. Tam, gdzie rysunek nie pokazuje bitów, sekwencja jest losowa, tak jak po włączeniu sprzętu.

Różnice, o których warto wiedzieć: TEMPO to tu podział tempa Abletona, więc pozycję gałki tempa przełożyłem na 1/4, 1/8 albo 1/16. Zakresy częstotliwości VCO, MOD VCO i czasy zaniku nie są zmierzone na sprzęcie, więc ta sama pozycja gałki może dać trochę inną wysokość lub długość niż w oryginale. Głośność poszczególnych presetów też się różni.

## Strojenie bez ucha

Pod FREQUENCY obu oscylatorów są przyciski **-12, -1, +1, +12**. Przesuwają wysokość dokładnie o półton albo oktawę i przy okazji zaokrąglają ją do pełnego półtonu, więc VCO i MOD VCO łatwo ustawić w czystym interwale (np. +7 = kwinta, +12 = oktawa).

## Akordy i flipy

- **Akordy**: VCO gra SEQ1, MOD VCO gra SEQ2, oba są w mikserze i odpalają się razem, więc na każdym kroku brzmi interwał. Presety *Akordy: dwa głosy z CHAIN* (jedna sekwencja, dwie głowice 2 kroki od siebie), *Akordy: kwinty* (MOD VCO o kwintę wyżej) i *Akordy: 32 kroki* (16 STEPS + CHAIN, głowice 8 kroków od siebie). Z MIDI na „SEQ1+2” akordy idą też do innego instrumentu.
- **Flipy**: kabel SEQ TRIG jednego sekwencera do BIT FLIP drugiego. *Flip: SEQ1 zmienia SEQ2*, *Flip: wzajemnie* (oba kable naraz) i *Flip: 5 na 16* (krótki SEQ1 przepisuje długi SEQ2 w coraz innych miejscach). Gdy oba sekwencery mają tę samą długość, flip wraca do punktu wyjścia co drugą pętlę, więc różne długości dają więcej zmian.

## Kolory

Kolor pokazuje, skąd płynie sygnał. SEQ1 jest pomarańczowy, SEQ2 morski, EG1 żółty, EG2 koralowy (legenda w nagłówku). Gałki AMT mają kolor źródła, którego słuchają, a wyjścia w patch bayu i kable kolor sygnału, który niosą (M VCO niebieski, MIXER fioletowy, U MIX różowy, CLOCK zielony, NOISE szary, VCA biały). Moduły dźwiękowe są neutralne.

## Presety „West”

Siedem presetów w duchu syntezatorów zachodniego wybrzeża: sinus z FM, barwa z wavefoldera, a EG2 otwiera naraz filtr i VCA jak bramka dolnoprzepustowa (kabel EG2 → CUTOFF). *Bongo (LPG)*, *FM dzwon*, *fałdowany bas*, *niepewność* (losowe napięcia sterują foldem, wolny MOD VCO filtrem), *pulser* (MOD VCO jest zegarem, a SEQ2 zmienia jego tempo), *krople* i *oddech*.

## SAMPLE

Trzynaste wyjście w patch bayu (zielone, w miejscu, gdzie sprzęt ma gniazdo MIDI) to odtwarzacz sampla. Sam z siebie
nie gra – trzeba go wpiąć kablem, np. w VCF IN, VCW IN, U MIX 2, M VCO SYNC, CUTOFF albo CLOCK 1.

- **Wczytaj sampel…** albo przeciągnij plik (wav, aiff, flac, mp3, ogg) na okno wtyczki. Sampel jest miksowany do mono,
  wyrównywany do pełnego poziomu i przycinany do minuty.
- **TUNE** ±24 półtony, **START** – miejsce, od którego gra (w pętli: początek pętli).
- **RETRIG** wyłączony: sampel gra w pętli. Włączony: startuje od START przy każdym wyzwoleniu EG1 i gra raz do końca.
- **PITCH**: FIX, SEQ1 albo SEQ2 – wysokość sampla idzie za skwantyzowanym napięciem wybranego sekwencera.

W projekcie i w presecie zapisuje się ścieżka do pliku, nie sam dźwięk – po przeniesieniu pliku trzeba go wczytać ponownie.
