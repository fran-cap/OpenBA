# CITIZENSKINS hand-in (Citizen skin sets)

`ext.citizen_skins`: the citizen skins of every mod that retextures them, shown together. One class A row, **off by
default**, drawing only (nothing is saved in the town, no sim effect). The row is read when a game starts, and what it
installs stays until the game is closed: restart the game after changing it (switching it on also works at the next
game start without a restart; switching it off needs the restart, or a display change, which loads the files again).

## 1. What it does

A citizen's shirt colour is one of 8 cells of a 1024 x 128 texture strip (`Models/Citizen/MaterialInstance/
CitizenTexture` for the men, `CitizenTextureF` for the women). A mod that retextures the citizens ships its own copy of
that file; the last package mounted wins and only its 8 colours are ever drawn.

With the row on, every mounted package's copy is read. The strips become one wider texture side by side (each
different mod's, at most 16 in all: up to 128 colours) and each citizen wears the strip that scores highest for its name. The citizen's saved variant (0..7) is untouched; the
strip is a function of the name and the sets, so a citizen keeps its look in every load and nothing is written to the save. Each set is
identified by its pixels, not by its place in the mount order: every set scores `hash(name, identity)` and the
highest wins (highest random weight). Taking a mod out moves only the citizens who wore its set; putting one in
attracts about 1 in N; a mod that moves up or down the list changes nobody.

Vanilla's own strips are only a fallback: as soon as any mod brings a citizen texture, vanilla's pair is not a set
(`GatherPackages`), so a village with one skin mod on wears only that mod's sets, not that mod and some vanilla. Vanilla's
strips still lend the half a mod lacks. With no mod, vanilla's is the only pair and the row does nothing.

Which strips become sets is decided in one place, `ChooseSets` (`skin_math.cpp`), over the packages `GatherPackages`
passes it (the mods' pairs, in mount order; vanilla's when there is no mod):
- The first of them is set 0 and must have both strips (`GatherPackages` has filled a mod's missing half from
  vanilla's); without both the row does nothing.
- A package that carries only one of the two textures wears the first package's for the other.
- Two packages with the same pixels count once.
- A strip not of the first package's size (vanilla's) is left out (logged).
- At most 16 sets in all, and fewer when the joined image would be wider than 16384 pixels (the wider of the two
  first strips counts). With fewer than two different sets the row does nothing.

A mod can carry more than one set. Besides its own pair (`CitizenTexture` and `CitizenTextureF`: the names retail
reads, so the mod still works as an ordinary skin mod with the row off), a package can ship numbered pairs:
`Models/Citizen/MaterialInstance/CitizenTexture2` with `CitizenTextureF2`, then `3` and so on up to `99`. Each pair
is listed in the mod's resource list like any texture (retail loads them and never uses them), and is one more
candidate set, read in number order after the package's own. A numbered pair with only one of the two wears the
package's own for the other. The sets are filled in mount order, then number order. `ParseStripName`
(`skin_math.cpp`) is the one place that decides what a name is: `CitizenTexture[F][N]_resource.crs`, N from 2 to 99,
no leading zero.

## 2. How

- **Reading.** `IO::FileSystem::_packfiles`: every package entry named by `ParseStripName` is decompressed from the
  package mapping (`CreateOnDemand::Maps`, the block directory's `RedecodeRange`) and its top level decoded
  (`ReadTextureLevel0`: BC1, BC3, BGRA8 or 8-bit grey; any other pixel format is refused, never guessed). The size of
  a strip comes from its file, so it is bounded before anything is allocated: an entry over 16 MB decompressed, a
  strip over 2 million pixels (vanilla's is 128 thousand) and a strip that is not the first package's size are all
  refused, a package's numbered pairs are limited to 16 and the pairs read in all to 64.
- **The texture.** The strips are joined and a full mip chain is made (`BuildChain`, a 2 x 2 box filter, never wider
  than 16384 or over 16 million pixels), BGRA8, and handed to `ImageBuffer::Create` on the already loaded
  `CitizenTexture` / `CitizenTextureF` (a create replaces the earlier texture of the object).
- **The shader.** The character vertex program (`OpaqueMaterial.srsl:NormalSkin_charactervs`, used by the two citizen
  materials only in retail) divides the texture coordinate by 8: an `l(0.125)` immediate in its compiled code, once
  in each of its 4 variants. It becomes `l(1 / (8 N))` (`PatchVertexBlob`, the DXBC checksum made again) and the
  program is created again from its bytes. All of its variants must be exactly that program, or none is patched, and a
  character program that carries the `l(0.125)` but cannot be patched (more than one, a checksum that is not its own)
  stops the whole install: no gender is left with the wide texture and a divide by 8. The
  same bytes feed every backend. The program belongs to the material, so a mesh that a mod draws with the same
  material and that does not get a strip number from the seam below would show the first 1/N of its texture; retail
  draws nothing else with it.
- **The citizen.** `CitizenComp_SetupAgeMesh` writes the variant into the mesh colour's fourth component; with the
  texture installed it writes `set * 8 + variant` instead (`SlotFor`). The identities of the sets are read there
  from atomics, set before the count is.
- **Order.** Everything that can be checked is checked before anything is changed: the strips decoded, the chains
  built, the blobs patched on copies. Then the two textures are made, then each program is written and made again.
  If the device refuses one of those steps, the programs already written get their own bytes back and both textures
  are made again from the strips retail itself draws (`RetailStrips`: per gender the own pair `CitizenTexture` /
  `CitizenTextureF` of the last mounted package that has one, as in retail where the last package wins), so the
  game is left as it was, never half changed; then fewer sets are tried (the first 8, 4 and 2). If a restore itself
  fails, the log says so. A game without the row, or one that could not join the strips, is exactly retail.

## 3. Hooks

- `GameUpdateLoop::Load`, right after `Ext::PathWorkers::SelectForGame()` (loader thread, before any citizen):
  `Ext::CitizenSkins::OnGameStart()`.
- `CitizenComp_SetupAgeMesh`: `Ext::CitizenSkins::SlotFor(name, variant)`. With the row off, or fewer than two sets
  installed, it is one atomic load and returns the variant: exactly retail's value (hook rule 4).
- `Engine::MainLoop::_extVideoLost` / `_extVideoRestored` (`registry_boot.cpp`): a device restart reloads the vanilla
  texture and program from their files; the joined ones are installed again from what was kept, also when the row has
  been switched off since: the citizens already carry `set * 8 + variant` in their mesh colour, and the citizen
  texture's U address mode is CLAMP (`CitizenTexture.rsc`), so retail's program on a reloaded retail strip would show
  every citizen of a later set in the strip's last cell. If that reinstall fails, the textures come back as the
  strips retail draws with WRAP in U, so the cell is the citizen's variant in the first set's look.

No game content ships (hook rule 7): the extension reads the packages the player already has and patches the bytes of
one program in memory; the icon is drawn by `tools/ext_icons/citizenskins.py`.

**Performance (hook rule 6):** not measured with the stress town. At a game's start the strips of every package are
read and joined once (a few hundred kilobytes each, and the texture up to 11 MB per gender); per citizen it is one
atomic load, one hash of the name and one score per set, when the citizen's mesh is set up, not per frame.

## 4. Tests

`tests/unit/ext_citizen_skins_test.cpp`: a BC1, BC3, BGRA8 and grey strip read, an unknown format and a cut file
refused; BC3's colour half is always four colours; a size taken from the file is bounded before decoding (over 2
million pixels, not the first strip's size) and the joined image is never wider than 16384; the chain's size, level
ladder and colours; the cell a slot lands on, through the literal the patch wrote, for every set count; the shader
patch (one literal, 1/(8 N), a valid checksum, nothing else moved) and a program without it, with two, with a broken
checksum left alone; the entry names of the plain and the numbered pairs (and the names that are not one);
`ChooseSets` (equal pixels once, a missing half from the first package, another size left out, the limit of 16 and
the width limit for either gender, no first pair, the order); the pick (stable, spread over 8 and 16 sets, pinned
values, a removed set moves only its own citizens, an added set only attracts its share, the order of the sets
changes nobody, a set's identity comes from its pixels); the registry row. The glue (`citizen_skins.cpp`: package
reading, the install and its undo, the device restart) needs the engine and was looked at in a game, not tested here.

## 5. Not done

- No setting for which mods take part or for a vanilla share: every different mod strip counts and vanilla's is
  used only when no mod has one. A mod that wants vanilla's look in the mix ships it as one of its sets (Vanilla
  Citizen Colors does). The game's own mod list is the choice.
- A package's own mip chain is not used: the chain is made from the top level.
- 16 sets at most (the joined image is never wider than D3D11's 16384): more are left out (the first 16 in mount
  order). A wider layout would need the program to add a row offset too.
- If the device refuses the wide image (a texture limit under 16384: no backend answers its limit through the
  interface the extensions use, so it is found by trying), the first 8, 4 and 2 sets are tried in turn; failing all
  of them the strips retail draws stay.
- A strip's alpha is dropped: `BuildChain` writes 255. The citizens' pixel program is the one `NormalSkin` shares with
  `Normal.ps` (`OpaqueMaterial.srsl`), whose `ps` lights `color.xyz` and writes alpha 0 itself; only its `psalpha`
  variant clips on the texture's alpha, and there a transparent pixel of a strip would be a hole, not another colour.
- A mod's own material drawn with the character vertex program (the program belongs to the material) would sample
  only the first 1/N of its texture: the program divides every texture's U, and only the citizens get the set number
  from the seam. Retail draws nothing else with it; accepted for an off-by-default row.
- Uncompressed, a 16-set texture is about 11 MB per gender in video memory (8 sets: about 5.6 MB).
- Switching the row off while the game runs does not give back the last mod's texture: what is installed stays
  until the game is closed, a display change included (the strips are installed again). Restart the game.
- A second game loaded in one run (quit to the menu, then start or load another): loading may make the character material,
  its programs or a texture again from the packages, which gives the game's own program and strips back while citizens
  keep getting `set * 8 + variant` (a citizen then reads the wrong part of the texture: stripes, a brown head). Every game
  start after the first checks the two programs (an original divide left in one) and the two textures (the joined size),
  makes again whatever is not the installed one, and logs `[CitizenSkins] game start with the strips installed before: ...`.
  The sets are the ones built the first time: changing the mods between two games still needs a restart.
