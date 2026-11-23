# synth — moteur de synthèse modulaire (Mac + STM32)

## Objectif

Un moteur DSP propre, portable et évolutif, qui **sonne**. Il est développé et écouté sur macOS, puis porté tel quel sur STM32.

Moteurs visés (on reproduit un comportement, on ne copie pas de code) :

| Id        | Inspiration          | Contenu                                                                                              |
|-----------|----------------------|------------------------------------------------------------------------------------------------------|
| `va`      | Ableton Analog       | 2 osc + bruit, 2 filtres (série/parallèle), 2 amplis, 2 enveloppes, 2 LFO                            |
| `mono`    | Minimoog Model D     | 3 osc libres, bruit blanc/rose, mixeur qui sature et réinjection de la sortie, ladder 24 dB sans compensation, contours ADS, mix de modulation (osc 3 ou contour, bruit ou LFO), priorité de note, glide |
| `fm4`     | Ableton Operator     | 4 opérateurs, 11 algorithmes, formes d'onde, filtre, enveloppe de hauteur                            |
| `fm6`     | Yamaha DX7           | 6 opérateurs, 32 algorithmes, enveloppes rate/level, keyboard scaling, feedback                      |
| `mseg`    | Absynth (enveloppes) | Enveloppes multi-segments (MSEG) bouclables, assignables à tout paramètre via la matrice de modulation |

Priorités, dans cet ordre : **1. le son, 2. la sûreté temps réel, 3. la lisibilité et l'évolutivité, 4. la vitesse.**

Cibles :
- macOS (Apple Silicon) : développement, écoute en temps réel avec le MiniLab 3 en USB, tests, rendus WAV.
- STM32H745I-DISCO (M7 à 400 MHz + M4 à 200 MHz : le maximum avec l'alimentation SMPS de la carte) : cible principale.
- STM32F746G-DISCO (M7 à 216 MHz) : cible du cours, 10 cartes à l'IUT.

## Décisions d'architecture (ne pas changer sans me demander)

- **Langage : C11** pour `dsp/` et `midi/`. C'est le même langage que le cours et le HAL ST, et il reste lisible par les étudiants.
- **`float` 32 bits partout.** Jamais de `double` : suffixe `f` sur toutes les constantes, fonctions `sinf`/`expf` et non `sin`/`exp`. Compiler avec `-Wdouble-promotion -fsingle-precision-constant`.
- **Fréquence d'échantillonnage fixée à l'exécution**, 48 000 Hz par défaut. Ne jamais la coder en dur.
- **Traitement par blocs.** `SYNTH_BLOCK = 32` échantillons. Les modulations sont calculées une fois par bloc (taux de contrôle = SR/32), puis interpolées linéairement échantillon par échantillon pour tout ce qui touche au gain, à la coupure ou à la hauteur. Les callbacks audio de l'hôte ou de la carte (64 à 256 échantillons) sont découpés en blocs de 32.
- **Voix mono, sommées dans un bus stéreo** (panoramique par voix), puis un bus d'effets, puis le master.
- **Aucune allocation après l'init.** Tout l'état vit dans des structs fournies par l'appelant ou dans une arène statique. Les tables (sinus, mipmaps, courbes) sont générées à l'init ou précalculées en `const`.
- **`dsp/` n'a aucune dépendance** : ni libc lourde, ni OS, ni HAL. Seulement `<stdint.h>`, `<stddef.h>`, `<math.h>` (et `math.h` uniquement à l'init ou dans les générateurs de tables).
- **API publique unique : `dsp/synth.h`, avec une poignée par instance.** `synth_create(mem, bytes, sr)` place une instance dans la mémoire de l'appelant (`synth_size()` octets), `synth_default()` donne l'instance statique (carte, outils, tests) ; puis `synth_init(s, sr)`, `synth_set_engine(s, e)`, `synth_midi(s, …)`, `synth_set_param(s, id, v)` et `synth_render(s, L, R, n)`.

## Arborescence

```
dsp/            # code portable (C11, sans dépendance) ; index de l'API : docs/api.md
  core/         # dsp.h (bloc, macros mémoire, aléatoire, rampes), fastmath, smooth, denormal, table de sinus
  osc/ filter/ env/ mod/ fx/   # oscillateurs, filtres (SVF, ladder), ADSR, LFO et descripteurs, effets
  voice/        # vpoly (polyphonie, vol, mono/legato), voice.h (sortie de voix commune), dérive
  engines/      # un fichier par moteur + engine.h (interface) + registry.c
  synth.h / synth.c   # API publique unique
midi/           # USB-MIDI 4 octets et flux DIN ; lien réseau des cartes (UDP, docs/link.md)
host/           # macOS : synth_play, synth_render, synth_catalog, fm6_import, synth_link ; JSON, patchs, morceaux, WAV
tests/          # suites unitaires (liste : tests/suites.h), bench, empreintes audio (golden.json)
tools/          # Python : analyse, validation, règles, index d'API, empreintes, générateurs
patches/ songs/ controllers/ spec/   # format ouvert OID (patchs, morceaux, cartes de contrôleurs, schémas)
docs/           # api.md et catalog.md (générés), architecture, décisions, bench, progress.md
web/            # browser prototype: wasm (emscripten) + AudioWorklet + Svelte UI (web/README.md)
plugin/         # CLAP plugin (synth.clap) and its test host (plugin/README.md); SDK headers in third_party/clap
examples/ cmake/                     # minimal uses of the API ; toolchain ARM
target/h745/                         # projet CubeMX (2 cœurs) + app/ (audio du M7) ; target/f746/ prévu
```

## Conventions de code

Un module DSP suit toujours ce schéma :

```c
typedef struct { /* état uniquement */ } svf_t;
void svf_init(svf_t *s, float sample_rate);
void svf_reset(svf_t *s);                         // remet l'état à zéro sans clic
void svf_set(svf_t *s, float cutoff_hz, float q); // unités physiques
void svf_process(svf_t *s, const float *restrict in, float *restrict out, int n);
```

- Préfixe du module sur tous les symboles. Les petites fonctions vont en `static inline` dans le header. Pas de variables globales mutables.
- **Unités physiques dans l'API DSP** (Hz, secondes, dB, demi-tons). La normalisation 0..1 n'existe que dans la couche paramètres.
- **Pas d'appel à la libm par échantillon** (`sinf`, `expf`, `powf`, `tanhf`). Utiliser `dsp/core/fastmath.h` : `fm_sin` (table + interpolation), `fm_exp2`, `fm_tanh`, `fm_mtof`, `fm_db2lin`.
- Pas de division dans les boucles internes : calculer l'inverse une fois par bloc.
- Macros `DSP_FAST_DATA` / `DSP_FAST_CODE` pour placer l'état et le code chaud en DTCM/ITCM sur STM32. Elles sont vides sur l'hôte.
- **Tout le code en anglais** : identifiants, commentaires, messages. Chaque fichier commence par 2 ou 3 lignes qui disent ce qu'il fait et sur quelle référence il s'appuie.

## Règles temps réel

« Chemin audio » = tout ce qui peut être appelé depuis `synth_render`.

- Interdit dans le chemin audio : `malloc`/`free`, `printf`, entrées-sorties fichier, verrous, appels système, boucles non bornées, récursion.
- **Denormals** : flush-to-zero activé à l'init (registre FPCR sur Apple Silicon, FPSCR.FZ sur Cortex-M7).
- En build debug : garde anti-NaN/Inf en sortie de chaque voix (assert hôte, mise à zéro et compteur sur la carte).
- Marge : chaque voix vise environ −18 dBFS, et le master passe dans un DC blocker puis un soft clipper. Jamais de clipping dur.

## Règles de qualité sonore

- **Anti-repliement.** polyBLEP pour scie, carré et pulse (avec PWM). polyBLAMP pour triangle et sync. Tables d'onde en mipmaps limitées en bande. Non-linéarités (drive du ladder, waveshaper) en suréchantillonnage ×2 avec filtre demi-bande, activable par paramètre.
- **Filtres TPT/ZDF** (Zavalishin) : stables même avec une modulation rapide. Coupure bornée à 0,45 × SR, suivi de clavier.
- **Enveloppes à segments exponentiels** façon analogique. Un redéclenchement repart du niveau courant (pas de clic). Temps de 0,5 ms à 30 s.
- **Lissage** de tout paramètre touché par l'utilisateur ou le MIDI : rampe par bloc, ou filtre à un pôle d'environ 5 ms.
- **Vol de voix sans clic** : fondu de 2 à 5 ms avant réutilisation. Priorité : voix relâchée la plus ancienne, puis la plus faible.
- **Saveur analogique**, discrète par défaut et réglable : dérive lente et aléatoire par voix (hauteur, coupure), léger désaccord entre voix.
- **FM** : modulation de phase comme sur le DX7. Sinus par table de 2048 points avec interpolation linéaire. Feedback moyenné sur 2 échantillons. Ratios coarse/fine/fixed et detune.
- Courbes de vélocité et keyboard scaling configurables.

## Modulation

- **Sources** : MSEG 1..N, LFO 1..N, vélocité, note (suivi de clavier), molette, aftertouch, pitch bend, CC, aléatoire par note, constante.
- **Destinations** : tout paramètre marqué `modulatable`.
- **Matrice** : des slots `{src, dst, amount, via}` (via = source qui pondère le montant), évalués au taux de contrôle. Le résultat s'additionne dans le domaine du paramètre : demi-tons pour la hauteur, octaves pour la coupure, dB pour le gain, valeur normalisée sinon.
- **MSEG** (façon Absynth) : jusqu'à 16 points. Chaque segment a un temps, un niveau et une courbe (−1..1). Un point de sustain, des points de boucle début et fin, trois modes de boucle (aucune, boucle pendant le sustain, boucle libre), synchro tempo en option. C'est la même structure pour l'ampli, le filtre, la hauteur ou n'importe quelle destination.

## Paramètres et presets

- **Une table de descripteurs comme source unique** pour l'interface, le MIDI learn et les presets. Chaque descripteur contient : `id`, `name`, `min`, `max`, `default`, `curve` (lin/exp/dB/int), `unit`, `modulatable`.
- **Presets versionnés.** Le champ `version` est obligatoire. Format binaire compact sur la carte, JSON côté hôte (outils de conversion dans `tools/`).
- `fm6` sait importer des sysex DX7 (format documenté publiquement) fournis par l'utilisateur.

## MIDI (`midi/`)

- **Paquets USB-MIDI de 4 octets** : câble = quartet de poids fort, CIN = quartet de poids faible, on ignore le CIN 0. Le MiniLab 3 utilise plusieurs câbles.
- **Flux d'octets DIN** avec running status.
- Note-on avec vélocité 0 = note-off. Pitch bend sur 14 bits. CC 1, 64 (sustain), 74, etc. MPE plus tard.
- Les événements portent un décalage en échantillons dans le bloc quand il est connu.

## Build et tests (macOS)

```sh
just              # la liste des verbes (justfile) ; les builds suivent CMakePresets.json
just setup        # une fois : venv Python, npm, configuration des presets host, debug, asan, arm, wasm
just build        # = cmake --build --preset host -j  (régénère aussi docs/catalog.* et docs/api.md)
just test         # = ctest --preset host
just check        # tout ce qui doit passer avant de rendre la main (host, Debug, ASan, ARM, wasm)
just render acid_bass / just song duel_froid      # rendu WAV dans build-host/renders/ et écoute
just play va      # jouer en temps réel avec le MiniLab
just golden       # enregistrer un changement de son voulu
just web          # prototype navigateur (http://localhost:5173) ; just plugin-install : le plugin CLAP
just clean        # objets, rendus, tracés, caches (just clean-all : supprime aussi les build-*)
./build-host/tests/synth_tests --bench            # ns/voix/échantillon
```

- Hôte : seulement les frameworks Apple (CoreAudio, AudioToolbox, CoreMIDI). Pas de PortAudio ni de JUCE sans me demander.
- **Tests obligatoires pour chaque module** : pas de NaN ni d'Inf, sortie bornée, déterminisme (même entrée → même sortie au bit près), réponse attendue (fréquence de coupure à ±5 %, durées d'enveloppe, hauteur juste, niveau de repliement sous un seuil donné).
- `tools/` : Python 3 avec numpy, scipy et matplotlib pour tracer spectres et réponses (PNG dans `build-host/plots/`).
- Environment setup (once): `python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt` (CMake picks `.venv` automatically); `brew install emscripten` for `web/`; the Arm GNU Toolchain unpacked in `~/opt/arm-gnu-toolchain-*` (found by `cmake/arm-none-eabi.cmake`, as are `/Applications/ArmGNUToolchain` and `$ARM_TOOLCHAIN`), then `just setup` configures every preset.

## Cibles embarquées

- **H745** : CubeMX (projet CMake). Le M7 fait l'audio : SAI + DMA à 48 kHz, en bare metal. Le M4 fera l'USB Host MIDI et l'interface, plus tard, avec une file de messages en mémoire partagée vers le M7.
  - Le tampon DMA va en **SRAM AXI** (section `.RAM_D1`, aligné sur 32 octets), jamais en DTCM : le DMA ne la voit pas sur H7. Après chaque rendu, appeler `SCB_CleanDCache_by_Addr`, ou laisser le D-cache coupé au début.
  - L'état du DSP va en DTCM (`DSP_FAST_DATA`).
  - **Alimentation : garder exactement la configuration SMPS/LDO des exemples ST de la carte.** Un mauvais réglage rend la carte injoignable par le débogueur.
- **Matériel** : 10 F746G-DISCO à l'IUT, une STM32H745I-DISCO, un Arturia MiniLab 3 (USB-C).
- **F746** : le projet existant (dépôt `STM32F746_Disco_Default_Synth`, branche `minilab3`, pas encore testée sur la carte). Son USB Host est déjà corrigé pour le MiniLab 3 (limites d'interfaces, parsing borné, sélection de l'interface MIDIStreaming).
- **Ne jamais modifier le code généré par CubeMX en dehors des blocs `USER CODE`.**
- **Budget H745** : 400 MHz / 48 kHz ≈ 8 300 cycles par échantillon au total (480 MHz exigerait l'alimentation LDO, que la carte n'utilise pas). Objectif : 60 % de charge maximum à la polyphonie maximale. Mesure par `DWT->CYCCNT` autour du rendu, affichée à l'écran.

## Façon de travailler (pour Claude)

- Avant de coder, lire `docs/api.md` (index de l'API généré depuis les headers : fonctions, unités, types) et `docs/catalog.md` (paramètres des moteurs), puis seulement les headers concernés, et suivre les modèles existants. `docs/api.md` et `docs/catalog.*` sont régénérés par chaque build hôte.
- **Petites étapes** : un module, son test, une ligne dans `docs/`. Le build et les tests doivent passer avant de me rendre la main.
- Après chaque modification : `just test`. Avant de rendre la main : `just check` (hôte, Debug, ASan, ARM, wasm). Le projet est sous git : un commit par étape, message qui dit pourquoi.
- ctest inclut `rules` (règles de code vérifiées par `tools/check_rules.py`) et `golden` (empreinte audio de chaque patch et morceau dans `tests/golden.json`). Une refactorisation ne doit changer aucune empreinte ; un changement de son voulu s'enregistre avec `just golden`.
- Pour un nouveau son ou un nouveau moteur : produire un rendu WAV de démonstration dans `build-host/renders/`, et un tracé spectral si c'est utile.
- Ne pas changer l'API de `dsp/synth.h` ni les décisions ci-dessus sans me demander.
- **Licences** : implémenter à partir de la littérature DSP et de la documentation publique. Ne pas copier de code sous GPL (Dexed, msfa, Surge, Vital…). Pas de marques déposées dans les noms de code. Pas d'échantillons issus de produits commerciaux.
- En fin de tâche : cocher la feuille de route ci-dessous si une étape est finie, ajouter une ligne dans `docs/progress.md`, et mettre à jour `docs/bench.md` si les performances ont changé.

## Feuille de route et état

Détail et historique : `docs/progress.md`.

- [x] **M0** : squelette CMake, `fastmath`, tests, `synth_render`, `synth_play` (CoreAudio + CoreMIDI).
- [x] **M1** : `va` (polyBLEP, SVF TPT, ADSR exponentielle, polyphonie, lissage, LFO, supersaw).
- [x] **M2** : ladder ZDF avec drive suréchantillonné, glide, legato ; moteurs `acid`, `mono`, `poly`.
- [ ] **M3** : `fm4` (4 opérateurs, 11 algorithmes, formes d'onde, enveloppe de hauteur).
- [x] **M4** : `fm6` (32 algorithmes, enveloppes rate/level, keyboard scaling, import sysex).
- [~] **M5** : MSEG (`dsp/env/mseg`), modulation par part pour tous les moteurs (8 macros, 2 LFO synchronisables, 2 MSEG, 8 routes, `dsp/mod/partmod`), matrice de `wt` ; restent les presets sur carte.
- [x] Parts multitimbrales (`synth_part_engine`, `synth_part_param`), tempo et horloge MIDI (`dsp/mod/tempo`), delay synchronisé.
- [x] Lissage commun des paramètres (5 ms, tous moteurs), modulation par voix (`dsp/voice/voicemod`, MSEG et LFO par note, 4 routes), microtonalité (tempéraments, Scala), MPE ; filtre en peigne (`dsp/filter/comb`, dans `wt`). Filtres formant et MS-20 (`dsp/filter/formant`, `k35`, slot commun `vfilter`) dans `va` et `wt`, second filtre de `va` en série ou parallèle, suréchantillonnage partagé `dsp/filter/halfband.h`.
- [~] **M6** : effets (chorus, delay, réverb, freeze) faits ; reste la saturation master.
- [~] **M7** : portage H745 ; fait : projet CubeMX, le M7 rend le synthé dans l'interruption audio (`target/h745/app`, `just h745`, `just flash`) ; joue et sonne sur la carte ; sélecteur de presets sur l'écran tactile (M4, boîte aux lettres en SRAM4) ; Ethernet sur le M4 (notes, CC, paramètres, presets, kits d'échantillons ; `docs/link.md` ; essayé sur la carte : les 81 presets joués par le réseau, client Python `tools/board.py`) ; USB MIDI hôte sur le M4 (contrôleur class-compliant, essayé avec un Launch Control ; `board_config.h`) ; potards sur les 8 macros avec reprise douce et vue Contrôles à l'écran (`app/knobs.c`) ; niveau de sortie `BOARD_VOLUME` ; optimisation : plus aucun preset au-delà de 90 % au pire tampon (moyenne 22 %), restent les 3 `sine_duel` à 61 % ; puis la F746.
- Hors feuille de route, faits : moteurs `modal` (dont piano physique), `drums`, `wt` et `coupled` (réseaux d'oscillateurs de Stuart-Landau couplés, échange module-phase, couplage harmonique n:m), `selfosc` (oscillateurs auto-oscillants de Van der Pol / Duffing forcés par la note), `drone` (machine à drones : partiels qui errent, corps, souffle, éclats ; de glacial à chaleureux) et `micro` (sinus de précision : enveloppes à l'échantillon près, hauteur fixe, clics, salves, ratchets, motifs de gate) ; format ouvert OID 0.3.
- [x] Moteur wavetable `wt` (esprit synthé wavetable moderne).
- [x] Sampleur de batterie `sampler` : kits d'échantillons (`dsp/core/kit.h`, `synth_part_kit`), 16 pads (vitesse, début/fin, inverse, boucle, filtre, groupes de coupure), lecture à bande limitée (`dsp/osc/splay`) ; kits depuis des WAV (`host/kitfile`), kits de démo enregistrés depuis `drums`, panneau Kit dans le navigateur, kits reçus par Ethernet sur la carte, paramètre `kit` dans le plugin CLAP (kits de démo dans le bundle).
- [x] Hôtes en plus de macOS : prototype navigateur (`web/`, wasm + AudioWorklet + Svelte) et plugin CLAP (`plugin/`).
- Non testé en conditions réelles : `synth_play` avec le MiniLab 3.
- Séquenceur et routage : projet séparé `~/Code/oahub` (DAW MIDI en Rust, clips et scènes, Launchpad X, page iPad, API Python), qui pilote les cartes par le protocole de `docs/link.md`. Ne pas mettre son code ici.

## Références

- V. Zavalishin, *The Art of VA Filter Design* (filtres TPT/ZDF, ladder).
- Välimäki et al., travaux sur polyBLEP/polyBLAMP (oscillateurs anti-repliement).
- A. Huovilainen, modèle non linéaire du filtre ladder.
- J. Chowning, *The Synthesis of Complex Audio Spectra by Means of Frequency Modulation*.
- Documentation publique du DX7 et du format sysex DX7.
- W. Pirkle, *Designing Software Synthesizer Plugins in C++* (architecture des voix, modulation).
