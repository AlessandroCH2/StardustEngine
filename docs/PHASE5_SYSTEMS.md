# Sistemi della fase 5 (Vulkan)

## Collider e gerarchia

Prima di ogni passo fisico vengono confrontate le matrici world, quindi anche i collider dei figli
seguono le modifiche del padre. Un cambio di scala ricrea la forma conservando le velocità.
I risultati dei rigidbody dinamici vengono applicati dal padre verso i figli. I collider senza
Rigidbody restano corpi statici separati: questa modifica non li unisce in un compound collider.

## Package

Ogni progetto ha `Packages/manifest.json` e cartelle `Packages/<nome>/package.json`.
`com.stardust.core` viene installato automaticamente e contiene il materiale default e la libreria
shader dell'engine; i primitivi e ScriptCore sono forniti dal runtime.
In **Project Settings > Packages > Install Local Package** si importa una cartella con `package.json`.
Nome, versione e dipendenze sono dichiarati dal package. Le dipendenze locali devono essere già
installate alla versione esatta richiesta (oppure `*`). Non è ancora presente un registry remoto.
Il pannello Project ha pulsanti **Assets** e **Packages**; i riferimenti ai package usano
`Packages/com.example.package/Textures/image.png` e rimangono validi nei player esportati.

## Editor Only e script multipli

Qualsiasi segmento `Editor` nel percorso degli script li esclude dall'assembly di gioco.
Gli script di `Assets/**/Editor` e `Packages/**/Editor` vengono compilati in `<Game>.Editor.dll`;
il player non carica né distribuisce questo assembly. Il progetto C# Editor viene generato separatamente.
Per i progetti C# preesistenti personalizzati, aggiungere l'esclusione `Assets/**/Editor/**/*.cs;Packages/**/Editor/**/*.cs`
agli elementi Compile dell'assembly runtime, come nel progetto Sandbox aggiornato.

```csharp
using Stardust;
using Stardust.Editor;
public static class Tools {
    [MenuItem("Tools/Create Marker")]
    static void CreateMarker() { Entity.Create("Marker"); }
}
```

I comandi appaiono nel menu **Scripts** dopo Build Scripts. Possono usare l'API Entity sulla scena
in modifica. Non sono ancora disponibili custom inspector o EditorWindow personalizzabili.
Gli script espongono anche array, List<T> e Dictionary<TKey,TValue>, incluse collezioni annidate,
vettori, colori, enum e riferimenti Entity/Component/script. L'Inspector permette modifica, aggiunta,
rimozione e riordino; i dizionari usano coppie Key/Value e segnalano chiavi duplicate. Le chiavi
supportate sono string, int, bool ed enum. Le classi/struct utente arbitrarie non sono ancora serializzate.
La duplicazione e i prefab rimappano i riferimenti nelle collezioni senza alterare le chiavi testuali.

Il GameObject può avere più script: Add Component > Script o drag & drop aggiunge uno slot senza
sostituire quelli esistenti. Ogni slot ha i propri campi e callback. `GetComponent<MyScript>()` e
`As<MyScript>()` cercano la classe richiesta. Le scene con il vecchio campo `script` sono ancora leggibili;
il nuovo formato usa `scripts`. La duplicazione rimappa i riferimenti interni alla gerarchia duplicata.

## Canvas, sprite e testi

Canvas e RectTransform vengono renderizzati come geometria della scena nell'editor. Il Canvas ha un
bordo bianco; rettangoli, pivot e anchor hanno maniglie dedicate. In vista 2D la UI viene disegnata sopra
geometria e griglia. In Game sono disponibili Overlay, World Space e Screen Space Camera.
I preset di anchor mantengono il rettangolo: Shift cambia anche il pivot, Alt allinea posizione/offset.
Ctrl durante il drag degli anchor aggancia bordi e centro del padre. I RectTransform non usano il
contorno arancione dei mesh. Gli strumenti 3D e le assegnazioni tramite drag non cambiano selezione.

Image supporta texture, sprite custom, materiale e shader custom compatibile con la pipeline UI,
Simple/Sliced/Tiled/Filled, preserve aspect e Pixels Per Unit Multiplier. Il moltiplicatore modifica
la dimensione dei bordi e delle tessere; i riferimenti preesistenti vengono migrati conservando l'aspetto.
Lo Sprite Editor separato offre checkerboard, Fit/1:1, zoom, pan, rettangolo, bordi 9-slice e pivot.
Un clic non modifica il ritaglio; Ctrl+drag definisce una nuova regione. Apply/Revert e le richieste
Save/Discard proteggono le modifiche non salvate. L'Inspector texture offre anche slicing a griglia.

I testi usano font TTF/OTF e atlanti SDF, colori, rich text, wrapping, auto size, allineamenti,
spacing, outline e shadow. La toolbar dell'Inspector inserisce tag nella selezione del testo.
Sono disponibili CanvasGroup, maschere rettangolari e layout orizzontale/verticale/griglia.
Questa API copre i componenti elencati qui sopra, non altri sistemi di testo avanzati.

### Eventi Button e InputField

Button > On Click contiene azioni persistenti multiple. Trascinare un target, scegliere il componente
oppure lo slot script e poi un metodo. Il target vuoto indica il GameObject del pulsante.
I metodi script devono essere pubblici, non statici e restituire void. Gli overload sono distinti.
Sono accettati parametri multipli: int, float/double, bool, string, Vector2/3, Color, enum (valore numerico),
Entity, componenti e script. Le azioni e i riferimenti sopravvivono a salvataggio, prefab e duplicazione.

```csharp
public void CliccaQui(int numero, string stringa) {
    Debug.Log(numero + ": " + stringa);
}
```

Restano disponibili `Button.onClick.AddListener(Action)` e il vecchio callback `OnClick()` quando
non esistono azioni persistenti. InputField offre testo, placeholder, font/colore, password,
multiline, read only, limite caratteri, selezione, clipboard e callback C#:

```csharp
var input = GetComponent<Stardust.UI.InputField>();
input.onValueChanged.AddListener(value => Debug.Log(value));
input.onEndEdit.AddListener(value => Debug.Log("Invio: " + value));
```

## Mesh e materiali

FBX, OBJ e glTF conservano i gruppi di indici/materiali e li renderizzano con slot distinti, anche per
ombre e picking. Nel Mesh Renderer la sezione Materials ha Size ed Element, Lighting espone Cast/Receive Shadows e Geometry contiene il riferimento mesh. Il doppio clic rivela
l'asset nel Project. L'Inspector del materiale ha una preview renderizzata e ruotabile in basso.
Le scene precedenti conservano il materiale incorporato come fallback.

## Prefab e terrain

Le scene conservano il collegamento del prefab, i mapping UUID e la baseline. L'editor prefab isolato
consente Save/Back; le modifiche si propagano alle istanze mantenendo gli override delle proprietà e
gli UUID. Le istanze e i loro figli sono blu nella Hierarchy. Sono disponibili Apply/Revert.

I terrain nuovi sono piani e hanno una palette dichiarata dall'utente: Add/Remove Layer, asset
`.terrainlayer`, texture, tint, tiling e roughness. Sono supportati fino a 16 layer con pittura dei pesi;
la rimozione rimappa e normalizza i pesi. L'editor ha toolbar per sculpt/paint/trees/settings,
palette a miniature, pennelli soft/hard/noise/star e maschere immagine, rotazione, size e opacity.
Ogni terrain ha renderer, erba, alberi e ombre indipendenti. Neighbors offre riferimenti sinistra/destra/
basso/alto, Auto Connect, creazione tile e Stitch Edges. Il pennello attraversa i tile connessi; i layer
sono associati per asset/nome. Auto Connect richiede tile allineati agli assi con dimensione uguale;
Stitch Edges richiede anche risoluzione uguale e media le altezze dei due bordi. I vecchi file SDT1 a quattro canali vengono letti e convertiti
nel formato SDT2 quando salvati. Il generatore procedurale è un componente separato con ruoli di
texture selezionabili. Le ombre mantengono un contributo ambientale configurabile sky/horizon/ground.

## Skybox e camere

Environment > Skybox offre Procedural (default), Solid Colour, Material/Panorama e Six Sided.
I sei lati sono Right +X, Left -X, Top +Y, Bottom -Y, Front +Z e Back -Z. Material usa colore e
albedo equirettangolare; rotazione Y ed esposizione EV sono regolabili. Per pixel shader custom sky
usare Create Custom Sky Shader, compilarlo nell'Inspector e assegnare il risultato a un materiale:
la sua interfaccia è SkyCommon.hlsli, diversa da MeshCommon. L'illuminazione ambientale resta
configurabile separatamente tramite sky/horizon/ground.
La camera di gioco ha projection ortografica, size, near/far e API C# Camera.orthographic e
Camera.orthographicSize. La toolbar 2D dell'editor è indipendente dalla camera del gioco.

## Ordine della Hierarchy

Trascinare nella parte alta/bassa di una riga inserisce prima/dopo, con linea blu; il centro rende
l'oggetto figlio. Il menu destro offre Move Up/Down e First/Last Sibling. L'ordine viene salvato.
Ctrl seleziona più oggetti; Shift estende un intervallo visibile. Ctrl+A seleziona tutti. Ctrl+C/V
copia/incolla le gerarchie con UUID nuovi e rimappatura congiunta dei riferimenti; Ctrl+D duplica,
Delete elimina la selezione. L'Inspector indica quando mostra soltanto l'oggetto attivo.
Project supporta Ctrl/Shift, Ctrl+A, Ctrl+C/V e Ctrl+D, anche per cartelle. Le copie sono create
nella cartella aperta sotto Assets, senza sovrascrivere file esistenti o copiare una cartella dentro sé stessa.
La Hierarchy ha icone per tipo e intestazione della scena con indicatore delle modifiche.

## Input e visibilità editor

In Play gli script ricevono mouse e tastiera soltanto nella Game view; i click iniziati su altri
pannelli non diventano click di gioco trascinando il mouse dentro Game. I manipolatori hanno
priorità sul picking di bone, icone e UI. Gizmos nasconde contorni, icone, bone, colliders e handles;
Handles permette di nascondere soltanto gli strumenti di trasformazione. La griglia resta indipendente.
L'interfaccia unisce font di sistema per cinese/giapponese/coreano; nomi e percorsi asset usano UTF-8.
Sono necessari font di sistema contenenti i glifi richiesti; non è inclusa una famiglia CJK redistribuibile.

## Shader custom

Dal menu contestuale del Project: **Create Custom Shader**. Il file HLSL viene aperto nell'Inspector:
**Save** e **Compile Pixel Shader** producono il file `.spv` adiacente. La compilazione usa DXC del Vulkan SDK.
Nel materiale, scegliere il risultato nel campo **Custom Shader**. I nuovi shader usano l'interfaccia
`MeshCommon.hlsli` del renderer (MeshVSOut, MeshPSOut, DrawData); il template conserva l'ID per il picking.
Sono supportati pixel shader HLSL, con la stessa pipeline geometrica dei materiali built-in.
Per shader con proprietà, stato di rendering e grafi a nodi vedi [Shaders](SHADERS.md).

## Animator e modelli

OBJ, glTF/GLB e FBX possono essere trascinati nella scena o scelti nel Mesh Renderer.
FBX usa [ufbx](https://github.com/ufbx/ufbx) v0.21.5, con conversione a metri e assi Y-up.
I modelli FBX conservano slot materiali e gerarchia dello scheletro. I bone diventano GameObject
con Transform, visibili nella Hierarchy e nella Scene view: possono ricevere oggetti figli e
riferimenti dagli script. Il Mesh Renderer offre Import Bone Hierarchy anche per modelli già presenti.
Le trasformazioni dei bone deformano la mesh con skinning lineare CPU; le animazioni FBX aggiornano
la posa a 30 campioni/secondo. Retargeting e skinning GPU non sono implementati.

Un Animator su un modello FBX riproduce la prima animazione incorporata; State può indicare il nome
di un take. Lo skinning è attualmente valutato sulla CPU a 30 campioni/secondo. Non è ancora una pipeline
di skinning GPU, quindi questa prima versione è destinata a pochi personaggi animati.
`X Bot@Idle.fbx` nel Sandbox contiene **0 mesh e 1 take**: è una risorsa di animazione senza geometria.
L'importatore lo segnala; occorre un modello con skin. L'applicazione di un take da un file separato
a un altro rig non è ancora implementata.

**Create Animation** genera una clip `.anim`, **Create Animator Controller** un `.controller`.
Entrambi sono modificabili nell'Inspector. Esempio di clip:

```json
{"duration":1,"tracks":[{"path":"Child","position":[[0,0,0,0],[1,10,0,0]]}]}
```

I keyframe sono `[tempo,x,y,z]`; i canali sono position, rotation (Euler in gradi), scale.
Il controller usa `defaultState`, `states` (clip e loop) e `transitions` (from, to, parameter, threshold).
`Animator.Play("Run")`, `SetFloat("Speed", 1)`, `SetInteger`, `SetBool`, `SetTrigger` e `ResetTrigger` controllano stati e parametri.
Il doppio clic apre Animation o Animator. Animation offre dope sheet, curve X/Y/Z lineari, keyframe trascinabili, zoom, righello e avanzamento a frame (60 fps). Animator offre grafo con griglia, pan/zoom, Entry/Any State/Exit, stati trascinabili e frecce selezionabili; il menu destro crea stati e transizioni. Il pannello Parameters dichiara Float/Int/Bool/Trigger e permette modifiche live. Le transizioni supportano condizioni multiple (AND), confronti, exit time e consumo dei trigger. La scheda Layers espone attualmente soltanto Base Layer: blend tree, layer aggiuntivi, maschere e blending delle transizioni non sono ancora implementati. Preview Selected apre una copia della scena, con Play/Pause, velocità e scrubbing. Anche l'Inspector
Animator offre Preview in Editor. Le pose di anteprima non modificano la scena salvata né avviano
fisica o script. I controller permettono di provare stati e parametri nell'editor. Il motore interpola position/rotation/scale; blending e blend tree non sono implementati.

## Build e contenuti scaricabili

Le nuove build producono `Data/game.pak` e `Data/packages.pak` insieme a `Data/Managed` e alle librerie runtime.
Il pak principale contiene la chiusura delle dipendenze delle sole scene abilitate (prefab, materiali,
texture, sprite, font, controller/clip, layer terrain, buffer/texture esterni e include shader).
`game.sdproject` non viene distribuito: i dati del player sono CBOR in `__runtime.sdruntime` nel pak.
Questo evita il file progetto JSON sciolto; non è una firma crittografica contro la modifica dei contenuti.

In Build Settings > Optional Content Paks si dichiarano nome e cartella relativa ad Assets. Le risorse
necessarie alle scene abilitate rimangono nel pak principale; il resto della cartella e le dipendenze
sono esportati in `Data/Optional/<nome>.pak`, con percorsi relativi ad Assets conservati. Questi pak
non vengono montati automaticamente. Le dipendenze dei package sono raccolte in packages.pak.
Le risorse caricate tramite nomi costruiti dinamicamente nel codice richiedono un pak esplicito:
il cooker non può dedurre tutti i possibili percorsi generati durante l'esecuzione.
Il player monta automaticamente gli archivi prima di caricare la scena. Gli script sorgente e le cartelle
Editor sono esclusi. Distribuire l'intera cartella della build; è ancora richiesto il runtime .NET 8.

`.pak` è il formato **Stardust SDPAK001**, non il formato binario di Unreal Engine. La prima versione usa
un indice, offset, dimensioni e checksum per file, senza compressione o cifratura. Al mount i file vengono
letti direttamente dall'archivio, senza estrazione o cache su AppData. Il mount legge solo
l'indice: non scorre i payload e non duplica lo spazio occupato dal gioco. FileSystem legge
l'asset richiesto e ne verifica il checksum; l'audio usa letture parziali e seek direttamente
sull'entry per fare streaming senza caricare tutta la musica in memoria. FBX, OBJ/MTL, glTF
con buffer esterni, texture, font e terrain usano la stessa sorgente virtuale. Le letture audio
parziali non verificano il checksum dell'intero file. I payload corrotti sono segnalati alla
lettura completa, anziché rallentare il mount con una verifica preventiva di tutto l'archivio.
Non c'è più il limite totale di 16 GiB o di 1 GiB per entry; l'indice resta limitato a 16 MiB e
100.000 entry. Le API di decodifica dei singoli formati possono avere limiti propri. Il checksum
non sostituisce una firma digitale per autenticare contenuti scaricati.

Per creare un pacchetto di contenuti, organizzare la cartella di origine con percorsi relativi agli Assets
(per esempio `Scenes/DLC.scene`, `Textures/DLC.png`), poi usare **Build Content Pak** nell'Inspector di una
cartella o:

```text
StardustPacker build <cartella-contenuti> <contenuti.pak>
```

```csharp
AssetPacks.Mount("path/to/chapter2.pak");
SceneManager.LoadScene("Scenes/Chapter2.scene");
// Oppure, da un metodo async:
await AssetPacks.DownloadAndMountAsync("https://example.com/chapter2.pak", localFile);
```

Un archivio montato dopo ha precedenza sui precedenti per i successivi caricamenti. Le cache degli asset
vengono invalidate sul thread del renderer. Le nuove scene introdotte dai pak sono caricabili anche se
assenti dalla lista iniziale; le scene esplicitamente disabilitate nella lista restano disabilitate.
Il mount non modifica automaticamente la scena già caricata: ricaricarla per applicare modifiche alla scena.

## Caricamento scene asincrono

`SceneManager.LoadSceneAsync(pathOrIndex)` restituisce un'operazione con progress (0–1), isDone,
error, allowSceneActivation e GetAwaiter. La lettura/validazione avviene in background; il progresso
arriva a 0.9 prima dell'attivazione. Impostando allowSceneActivation=false si può attendere un comando
utente e visualizzare una barra tramite progress. L'attivazione di oggetti, GPU e fisica avviene sul
thread principale; quel passaggio può ancora richiedere un frame lungo. È ammesso un caricamento
attivo per volta. Il fallimento resta osservabile e l'await genera un'eccezione.

```csharp
SceneLoadOperation loading;
public async void CambiaLivello() {
    try {
        loading = SceneManager.LoadSceneAsync("Scenes/Level2.scene");
        await loading;
    } catch (System.Exception e) { Debug.LogError(e.Message); }
}
void OnUpdate(float dt) {
    if (loading != null) Debug.Log(loading.progress);
}
```

## Verifiche

Compilare `StardustRuntimeTests` e avviare l'eseguibile: verifica gerarchie dei collider, forze, serializzazione
dei componenti UI e degli script multipli, rimappatura dei riferimenti, archivi/patche corrotti, package core,
compilazione e caricamento Editor Only, callback C# con argomenti e InputField, interpolazione delle clip, aggiornamento prefab, anchor/pivot, cottura selettiva dei pak, metadata runtime e barriera/errori del caricamento asincrono.
Le verifiche interattive sono state eseguite in progetti separati in `build/FeatureSmoke` e
`build/EditorSystemsRegression`: anteprima Animator senza Play, terrain multipli e pennelli,
gerarchia bone FBX, riordino Hierarchy, camera ortografica, skybox e collezioni nell'Inspector.
I test automatici verificano anche deformazione CPU di un FBX con skin, cucitura dei terrain,
persistenza di 16 layer, liste/dizionari annidati, clipboard multipla, condizioni/trigger/exit time,
entry Unicode e importazione FBX/OBJ/glTF da pak senza estrazione.


## API C# per contenuti runtime

La guida inglese con esempi verificati è [CSHARP_SCRIPTING.md](CSHARP_SCRIPTING.md).
I percorsi espliciti `.scene` non dipendono dalla lista Scenes In Build, purché il file sia
presente nel namespace virtuale o come file locale. Indici e nomi brevi continuano a usare
la lista abilitata. La build continua a includere soltanto le dipendenze delle scene iniziali;
le scene scaricabili vanno distribuite in cartelle/pak opzionali.

`Entity.DontDestroyOnLoad(entity)` conserva tutta la gerarchia del root, inclusi UUID,
istanze C#, stato privato, corpi fisici e audio. Il cambio scena distrugge soltanto gli oggetti
non persistenti e inizializza quelli nuovi; `OnCreate` non si ripete per i superstiti.

`Sprite`, `Prefab`, `TextAsset` e `Asset` sono riferimenti a percorsi. `Assets.Load<T>` crea
un riferimento senza caricare dati. `Image.sprite` accetta `Sprite` e conserva la conversione
implicita da stringa. Il mount successivo aggiorna le cache di rendering, anche dopo un primo
caricamento fallito. `Assets.Exists/GetSize/ReadAllBytes/ReadAllText` cercano nei pak montati
con precedenza dell'ultimo mount e nei file locali. `AssetPacks.Unmount(file)` smonta un archivio;
gli stream audio già aperti mantengono il proprio handle. DownloadAndMountAsync monta il nome
finale del download, così il rename non invalida il percorso fisico indicizzato.

I campi Entity/component/script accettano prefab da Project se il root soddisfa il tipo
richiesto; i riferimenti sono salvati come `{ "asset": "Prefabs/Enemy.prefab" }`. I campi script
sono istanziabili con `Entity.Instantiate(template)` ottenendo lo script della nuova istanza.
I riferimenti asset dei campi script verso cartelle dichiarate opzionali restano soft: non
forzano l'asset nel pak principale. Le dipendenze di rendering effettive restano obbligatorie.
