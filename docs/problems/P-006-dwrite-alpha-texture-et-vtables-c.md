# P-006 — DirectWrite en C : `ALIASED_1x1` vide, et les vtables écrites à la main

## Symptôme
Deux blocages successifs pendant T-005, tous deux sur le même angle mort : **DirectWrite n'a pas
d'interface C**, et sa documentation décrit le comportement C++ habituel.

1. `GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1)` renvoie un rectangle **vide** (donc zéro
   glyphe rastérisé) alors que `CreateGlyphRunAnalysis` a réussi. C'est pourtant exactement ce que
   demandent ADR-006 et research/03 §5.3.
2. Crash en écriture à l'adresse `0x24` **dans `dwrite.dll`**, sur un appel qui compile et se lit
   correctement (`IDWriteFont::CreateFontFace`), et un second crash au premier `MapCharacters`.

## Analyse
1. `ALIASED_1x1` n'est rempli que par `DWRITE_RENDERING_MODE_ALIASED`, c'est-à-dire du texte
   **crénelé**, sans anti-aliasing. Tous les modes antialiasés (`NATURAL`, `NATURAL_SYMMETRIC`)
   n'alimentent que `CLEARTYPE_3x1`. Le « grayscale » de DirectWrite n'est pas une texture séparée :
   c'est la texture 3x1 dont on replie les trois échantillons subpixel. Skia et WebRender font ça.
2. Le `dwrite.h` du SDK est **C++ pur** (`interface X : public IUnknown`, `STDMETHOD`) : aucun bloc
   « C style interface » comme en produisent MIDL et `d3d11.h`. Il faut donc redéclarer les vtables,
   et **l'ordre des slots est l'ABI**. Deux erreurs commises :
   - `IDWriteFont` a **dix** méthodes avant `CreateFontFace` (`GetFontFamily`, `GetWeight`,
     `GetStretch`, `GetStyle`, `IsSymbolFont`, `GetFaceNames`, `GetInformationalStrings`,
     `GetSimulations`, `GetMetrics`, `HasCharacter`), pas neuf. Avec neuf slots de bourrage, l'appel
     tombait sur `HasCharacter(UINT32, BOOL*)`, qui écrit à travers son second argument — d'où
     l'écriture à `0x24`.
   - notre `IDWriteTextAnalysisSource::QueryInterface` répondait `S_OK` à **n'importe quel** IID.
     DirectWrite demande `IDWriteTextAnalysisSource1` ; en recevant oui, il appelait un slot situé
     après la fin de notre vtable, donc un saut dans ce qui suit en `.data`.

## Décision
- Rastérisation en `CLEARTYPE_3x1` + `RENDERING_MODE_NATURAL_SYMMETRIC`, moyenne des trois octets
  vers un octet de couverture. L'intention d'ADR-006 (un canal, aucune frange colorée, atlas R8
  inchangé) est respectée ; seul le nom de la constante change. L'ADR n'est pas rouvert, l'écart est
  documenté dans la Livraison de T-005 et ici.
- Les slots non appelés sont déclarés `void *slots_x[N]` avec, en commentaire, la liste exacte des
  méthodes qu'ils recouvrent : c'est le seul moyen de relire un décalage.
- Tout `QueryInterface` que nous implémentons compare les GUID et renvoie `E_NOINTERFACE` par
  défaut. Répondre oui à tout est une bombe à retardement, pas un raccourci.

## Leçon
Quand une vtable est écrite à la main, un crash **dans la DLL** avec un pointeur nul décalé d'un
petit offset veut presque toujours dire « mauvais slot », pas « mauvais argument ». Vérifier le
compte de méthodes de **toute la chaîne d'héritage** avant de suspecter les paramètres, et compter
les `STDMETHOD_(type, nom)` autant que les `STDMETHOD(nom)` : les deux occupent un slot.
