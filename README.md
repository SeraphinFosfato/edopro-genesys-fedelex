# Genesys Deckbuilder

Minor client modifications to support the Genesys banlists in the EDOPro deckbuilder.

![client.png](client.png)

1. Support for point-based banlists
2. Point costs are displayed on each card
3. The total point cost of the deck is maintained on the top right of the screen
4. ATK/DEF filters are compressed together in a single Stats label
5. New points filter, with 2 independent inputs to achieve conditions of the type `A < x < B`

## Download

Vai alla pagina [Releases](../../releases) di questo repository e prendi
l'ultimo tag pubblicato.

<!-- artifacts:start -->
Ogni release allega questi file (generato da
`tools/release/artifacts.json`, non modificare a mano qui sotto):

- **`ygoprodll`** (Linux, da scaricare a mano)
  Client Linux x64 nudo. Linka dinamicamente le librerie del sistema che ha compilato (ubuntu-latest): su una distribuzione diversa puo' non partire ("error while loading shared libraries"). Resta per chi ha gia' le librerie giuste.
- **`edopro-custom-linux-x64.tar.gz`** (Linux, da scaricare a mano)
  Client Linux x64 con la propria chiusura di dipendenze accanto (lib/), un installatore (install.sh, mai sudo) e le licenze delle librerie ridistribuite (notices/). Da preferire per un'installazione manuale: vedi il LEGGIMI.txt dentro il pacchetto.
- **`edopro-custom-windows-x86-package.zip`** (Windows, da scaricare a mano)
  Client Windows x86, da preferire per un'installazione manuale. Contiene ygopro.exe, strings/fedelex.conf (senza, le etichette del filtro punti restano "???", D202) e notices/ con le licenze delle 19 librerie collegate staticamente (D219). Istruzioni nel LEGGIMI.txt dentro il pacchetto.
- **`edopro-custom-linux-x64.zip`** (Linux, SOLO per l'aggiornatore integrato — non scaricarlo a mano)
  Stesso eseguibile Linux di sopra, impacchettato nel solo formato che il ClientUpdater integrato sa installare da solo (ZIP). Non scaricarlo a mano: e' per il manifesto di aggiornamento firmato.
- **`edopro-custom-windows-x86.zip`** (Windows, SOLO per l'aggiornatore integrato — non scaricarlo a mano)
  Stesso eseguibile Windows di sopra, impacchettato nel solo formato che il ClientUpdater integrato sa installare da solo (ZIP). Non scaricarlo a mano: e' per il manifesto di aggiornamento firmato.
<!-- artifacts:end -->

Le istruzioni d'installazione per ciascun file sono nel `LEGGIMI.txt` dentro
il pacchetto stesso (dove esiste) o nella sezione **Installation** sotto.

## Installation

Every mentioned path is relative to your preexisting edopro installation (the folder where you have your `EDOPro` executable).

- Generate the banlist file and move the generated `genesys.lflist.conf` inside `/lflists`.

```bash
python3 generate_genesys_banlist.py
```

- Add the these lines to `/config/strings.conf`:

```
!system 3315 Points:
!system 3316 Stats:
```

- Compile this project following the official [instructions](https://github.com/edo9300/edopro/wiki/1.-Prerequisites), except for the repository you're cloning:

```bash
git clone https://github.com/efinauri/edopro.git edopro_genesys
cd edopro_genesys
git submodule update --init --recursive
./travis/install-premake5.sh windows|linux|osx
```

- Take the `ygoprodll` executable and place it in the same directory as the `EDOPro` executable.

## Maintaining the banlist

You can rerun the script every time the official [cardlist](https://registration.yugioh-card.com/genesys/CardList/) updates.

Pay attention to the script ouput, as it lists the cards that could not be matched against EDOPro's current card database.
For example, at the time of writing, DOOD is still not promoted as a TCG set, and as such the cards from that set with a point cost cannot be automatically added:

```
[!] 2 cards were not found in the database:
  - Dominus Spiral 10
  - K9-04 Noroi 10
```

If you need to modify it manually, you can add entries in the form of `<card id> <maximum copies> <point cost>`. Here's the first lines of a Genesys banlist for reference:

```
!Genesys
21044178 3 100
9464441 3 20
62320425 3 50
38811586 3 33
[...]
```
