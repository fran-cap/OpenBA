# FAMILYTREE -- `ext.family_tree` (class C, default OFF)

Block FAMILYTREE, 2026-10-28. A record of who is related to whom in the town, and a page that shows it.

## 1. Retail

Retail keeps only the living links of a citizen (`CitizenComponent`):

| field | what it holds |
|---|---|
| `spouse` (+0x250) | the partner, set on both citizens |
| `_parent` (+0x258) | one parent |
| `_firstChild` / `_nextSibling` / `_prevSibling` (+0x260..+0x270) | the ring of that parent's children |

Nothing links a child to both parents. The links are cut as the town lives (`citizen_onremove.cpp`,
`citizencomponent.cpp`):

- A citizen who marries leaves their parent's ring (`spouse->SetParent(nullptr)`).
- When a citizen dies, a surviving spouse inherits the ring; otherwise each child who is still under the adult age dies
  with them and each adult is cut loose.
- A dead citizen's component is removed; nothing about them remains.

So the game cannot say who a married citizen's parents were, or who a dead grandparent was.

## 2. The extension

The extension writes the history down as it happens (`family_tree_town.cpp`). Standard C++ for the table, the
relations and the wire format (`family_tree.h`, `family_tree.cpp`).

**Seams** (one test of `g_on` each, off: nothing runs):

| where | what is written |
|---|---|
| `CitizenComponent::Initialize`, at its end | a person. With a parent and an age of 0: a birth, mother and father taken from the parent and the parent's spouse. Otherwise an arrival (a founder, an immigrant, an immigrant's child) with its age |
| `CitizenComponent::Update`'s marriage scan; the immigration states' couples | a marriage |
| `CitizenComponent::HandleLifeEvent`, on entry | the cause a death will carry: hunger (0), cold (1), old age (2), disease (3), lost their parents (4), childbirth (6) |
| `CitizenComponent::OnRemove` (reached through `MarkForRemoval`, so only for deliberate removals) | the death: the date, the age in years (`(int)(age * 5)`, as the citizen panel shows) and the cause, else "other" |
| `GameUpdateLoop::Update` -> `BeginFrame` | reads the row; after a load binds the tree to the citizens; every 90 frames a sweep |

**The sweep** catches what the seams do not see. It gives a citizen the tree has not met a person (an arrival dated now,
with the parents the live links still name), records spouse links that are not in the tree, and fills in parents that
are unknown. Turning the row on in a town that already exists therefore starts a tree from the living: everyone is an
arrival dated at that moment, with the parents and marriages the live links show.

**Jobs** are kept by the same sweep. Each sweep gives every citizen the tree knows the game time since the last sweep
(read from the calendar: `year * 12` plus the part of the year gone, in months), so a paused game adds nothing and a fast
game adds more. The time goes to the citizen's title, chosen as the citizen panel chooses it: a citizen under the adult
age is a child (`ProfessionList::childProfession`), one at a school is a student (`studentProfession`), and everyone else
has their profession, which is Laborer when none was given. Nothing is hooked where the game assigns a job (it is a plain
field written in about six places); a job held for less than a sweep (90 frames) may not be seen. The clock is not
credited across a jump of more than two months (a load) or before the first sweep after a load.

A job is stored by the key of its string table entry (`Profession::stringName`, for example `Farmer`), so a save does not
depend on the order of the profession list or on a mod's professions being present. The tree keeps one table of the keys
it has seen (at most 255) and a person's jobs point into it. Time is counted in tenths of a game month, at most 65,535
units per job (over 500 years); a part of a unit waits in memory until the next sweep for the same job, and is rounded
when the job changes. A person keeps at most 12 jobs: a thirteenth takes the place of the one with the least time and
adds its time to its own. The page shows the key as the profession's text from its string table (`JobText`), and the key
itself when the game has no such profession.

**Dates** are month stamps: `year * 12 + month`, the year from the calendar and the month as the event log computes it
(`yearTime / 3600 * 12`). The Family page shows a stamp as "Year N, <month>", the month text read from the game's
`seasons` string table (`Dialog/StringTable.rsc:seasons`, keys `Month0` to `Month11`, the table the status bar and the
event log read). A mod that renames the months therefore renames them on this page too. If the table or an entry is
missing, the page falls back to "month N".

## 3. The save

Class C: the tree travels in the extension's own record in the save tail (`state_codecs.cpp`, format version 2). The
payload is `nextId`, the count, and per person: id, name, gender, born-here flag, joined date and age, died date and age,
cause, mother, father, the entity-list index of the living citizen when saved (`live`, a hint), the marriages, and (from
version 2) the jobs: a count, then per job the index into the name table and the units (2 bytes). After the people comes
the table of job names. A job costs 3 bytes; a person with five jobs adds 16 bytes to a record of about 70. Version 1 saves
load with no jobs and are written as version 2 from then on; a save from a newer build is refused as before.

After a load `BeginFrame` binds each living person to a citizen: first by the saved entity index, accepted only when the
name and the gender agree; then, for the rest, by name and gender when exactly one person and one citizen are left with
them. A living person no citizen matches is recorded as gone (died, cause "other", age 0). The decoder refuses a payload
that is cut, has trailing bytes, an id that does not ascend, a cause that disagrees with the died date, or a count past
what the payload holds; a parent or spouse id that is not in the table is dropped.

## 4. The page

The Family page of the extension window (`toolbar_page.cpp`, `FamilyTreePage.rsc`): two lists. The first is the town's
people, the living by name (the dead after them when "Show people who have died" is checked). The second is the family of
the person picked: their own line, born or arrived, died (date, age, cause), jobs (the four with the most time, as "Farmer 12 y, Miner 4 y",
with "and N more" after them; the page looks again every 180 frames while open), descendants (only for a person with children: "3 children, 11 in all, 7 living"; `Tree::DescendantsOf` counts everyone below the person once, the dead included in the total, and a person who descends from the picked person through both parents counts once), mother, father, the four grandparents, wives
or husbands in marriage order, sons and daughters, brothers and sisters (half when only one parent is shared). A related
person's name is a button that picks them. A repeated name carries its number (Anna, Anna (2)).

**The search box** sits on its own line above the people list ("Find:", a count of the people the list shows ("42 people", set each time the list is built) and the game's own edit field, `UI::EditElement`, as the new game dialog's
name; `EditDescription` is a type the embedded `.rsc` reader knows since this box, schema from `EditDescription::Serialize`:
font, text, maximum length, password flag, filter). Its border has no color of its own, so the page gives it a light fill (ARGB 0x38ffffff) when it opens. Clicking it takes the keyboard (the game's key bindings stop while it is
active, the latch in `EditElement::Update`); Enter, Escape or a click elsewhere gives it back. The page's tick reads the
text every frame; when it changes the people list is rebuilt with the people whose name, with its number ("Anna (2)"), has
every word typed (`NameMatches`: upper and lower case alike, a part of a word is enough, any order). The box starts empty
each time the window opens. The family list under it keeps the picked person even when the list no longer shows them.

**The window's size.** The extension window is one tab control, as tall as its tallest page, and the other pages are a hint,
a 350 px list and one line (`RestrictedFoodsPage.rsc`). The Family page stands on the same footprint: its two lists are 120
and 150 px, the rest is one-line rows and the search line, and in game (2026-10-30, a screenshot with lists of 90 and
100 px) the page left about 140 px of the window free, which these heights and the search line use. Raising a list's height
past what the window has makes the window taller on every tab, so the heights are in `FamilyTreePage.rsc` with a comment
saying so.

A pick from a button inside a list only sets a pending pick; the page's tick applies it, because a control must not destroy
the cells of its own event. The tick also rebuilds the lists when the tree has grown or someone has died.

### 4a. The drawn tree

The tree has a window of its own (`tree_window.cpp`, `Dialog/FamilyTreeWindow.rsc`), opened by the Family page's "Show family tree" button or by the citizen panel's Family button, so the page keeps its room for the lists. The window draws the picked person's tree as four rows of boxes (an hourglass): the grandparents (the
father's two parents over the father, the mother's over the mother), the parents, the person with the brothers and
sisters before them and the wives or husbands after, and the children. Lines join the rows: a stem from a couple's middle
(or from a single parent) to a bar over the children. Clicking a box picks that person (the Family page follows) and centers the tree on them; a pick made on the page or from a citizen redraws the tree. "Details" in the window shows the Family page. The window stays where the player left it during the session; it is not in the save. A row
nobody is in is left out, so a person without known parents has no rows above.

The picture is made of tiles 26 px wide. A box is three tiles wide and its center is a tile's center, so a vertical line
from the middle of a box is the line of one tile; boxes in a row are four tiles apart. The arithmetic is
`tree_layout.cpp` (no UI; unit-tested): it returns each box's row and center tile, the one tile between a couple (a short
line, with a stem down when they have children), and the arms (up, right, down, left) of every line tile between two rows.
The view (`tree_view.cpp`) turns that into rows of cells: a box is `ribbonFamilyBox` (a button over a
thin border, tinted for the picked person and the dead), a run of empty tiles is one space, a line tile is one of the
sprites `TreeLink1` to `TreeLink15` (named for the arm bits, 1 up, 2 right, 4 down, 8 left), `TreeGap` and `TreeGapStem`
(`tools/ext_tree_tiles.py` draws them; programmer art, replace the PNGs for real art).

The tree area is 572 px wide, the width of the extension window's lists (about seven boxes of three 26 px tiles; it was 780 px, ten boxes). When the tree is wider than the window, `<` and `>` buttons above it slide it two boxes at a time; a box shows only whole, and
the buttons appear only when something is out of view. Shown at most: 12 brothers and sisters, 3 wives or husbands, 12
children (the lists below hold everyone). The tree is rebuilt in the page's tick (a control must not destroy its own cells),
like a pick in a list.

### 4b. The Family button on the citizen panel

The citizen panel (the dialog that opens on a click on a citizen) gets a "Family" button to the left of Follow
(`citizen_button.cpp`). Pressing it opens the tree window (or brings it to the front) with the tree centered on that citizen, and
picks them on the Family page. Four seam lines in `citizenui.cpp` (`OnCreated`, `OnUpdateLayout`,
`OnElementEvent`, `OnClosed`), each a test of `g_on`.

The button is made from the Follow button's own description (so it looks like Follow, the retail one or a mod's; its label and tooltip are ours; `FamilyTreePage.rsc:buttonFamilyCitizen` is the fallback) and added to the element that holds the Follow button,
so a mod that rearranges the panel keeps the button next to Follow. That element must be a plain group; if it is anything
else the button is not added and `standalone_error.log` says so (`[FamilyTree] the citizen panel's Follow button is not
in a group`). The button's right pad is set from Follow's size at each layout. The button's element id is 0x229, after
the panel's own 0x226 to 0x228. A citizen the tree has no person for (the row was turned on after the citizen was made,
before the first sweep) does nothing when pressed.

### 4c. The village family tree picture

The Family page has two buttons, "Save village family tree picture" and "Save as SVG". The first writes the whole town's
family tree as a PNG, `Screenshots\FamilyTree<N>.png` in the save directory (the folder the town screenshot uses; `<N>` is
the first number not taken); the second writes the same picture as an SVG, `FamilyTree<N>.svg`, which opens in any
browser, scales without blurring and prints. A status line under them says what was saved. The check box "Only the living
and their ancestors" leaves out the dead who are nobody's ancestor (the husbands and wives of the living stay). More than
6000 people fall back to that on their own, and are cut at 6000 if still too many; the picture's subtitle says so.

**The PNG.** `family_raster.cpp` (standard C++, unit-tested with a fake font) draws the layout below into a pixel buffer, at
the SVG's size (one unit is one pixel), and the game side encodes it with SDL (`TownScreenshot::SavePngInScreenshots`). The
text is the platform's font rasterizer (`ext/resources/font_raster.h`, the one `ext.sharp_ui` uses): Segoe UI, then Arial,
then the game's own face on Windows; the game's face (`FrancophilSans.ttf` beside the exe) first elsewhere, because the posix
rasterizer looks up one face file per process. A name with a character the face lacks shows it as "?". A picture wider than
16,384 pixels or larger than 16 million pixels in all (64 MB of pixels; the encoder copies them once more) is drawn at a
smaller scale that fits, and the status line says the percentage. When the scale is so small that the names cannot be drawn
(a box's name would be under a 5-pixel cell), the PNG would be coloured boxes only, so the button saves the SVG instead and
the status line says why (`PictureRaster::namesDrawn`). The same fallback applies when no font can be had at all and when
the pixels or their encoding run out of memory (`std::bad_alloc` is caught in `SaveImage`). The PNG is drawn and encoded on
the main thread, so a very big town pauses the game for a few seconds.

The SVG and the PNG share one set of lines (`BuildPictureStrokes`: the marriages, each family's line, and the dots where the
children meet their boxes) and the same words (`PictureTitle`, `PictureName` and the other `Picture*` functions), so they
cannot drift apart; a unit test checks every line of the shared set is in the SVG's text.

`family_picture.cpp` (standard C++, unit-tested) lays it out. Rows are generations: a founder is generation 0, a child is
one below its parents, and the two people of a marriage share a row, so an immigrant stands in the row of the person they
marry. Spouses are side by side; a person with several marriages is a chain of them. Each cluster of spouses hangs under
the first family the walk reaches that has a child in it, and is centered over its own children; two lineages that marry
meet at the marriage's cluster, and the second family's line to it is simply longer. Each family has one color for its
whole line, from the parents' marriage line down to its bus and the drops to the children; colors go round a palette of
twelve in the order the families stand on the page, so neighbors differ, and where lines cross the colors show which
belongs to which. A marriage is a short dark line between the boxes (an arc over the boxes between them when a person's
marriages are not side by side). Women's boxes are rose and men's blue; the dead are grey with a dashed border. Each
box gives the name, born or arrived (with the age on arrival), and for the dead the year and age at death.

### 4c-2. Outlining a person's descendants in the picture

The check box "Outline the picked person's descendants" (on the line of "Only the living and their ancestors") makes both
picture formats draw the box of the person picked on the Family page, and of everyone who descends from them (through a
mother or a father, `Tree::DescendantIds`), with a 3.4 px orange outline (`#e07b00`, `PictureHighlightColor`) in place of the
usual 1.6 px gender color; the fill still says woman, man or has died. The line under the title says "<name> and the
descendants are outlined in orange". With no one picked the box is ignored. A descendant who is not in the picture (the
living-only choice, or a town too big to draw) is simply not outlined.

### 4c-3. Show in town

A "Show in town" button beside the name in the family list's title moves the camera to the picked person and opens their panel: it calls the game's
own `PlayerToolManager::CenterOnEntity` (the call the citizen panel's home and work buttons make) with no dialog, so the
camera snaps to the citizen and the edit tool selects them. The button is shown only when the picked person is a citizen
living in the town now (`CanShowInTown`: bound to a live citizen with an entity); the page's tick shows or hides it as the
pick changes.

### 4c-4. The main job under each name

A check box beside the two save buttons, "Show each person's main job", (checked when the page opens; uncheck it for the plain boxes) adds a line to every box in the saved picture
(PNG and SVG): the job the person worked longest and for how long ("Farmer 12 y"), in the game's own profession name (the
same `JobText` the Jobs row uses), under the dates (under the arrival line for the living, under the death line for the
dead). `BuildPictureLayout(tree, livingOnly, &PictureJobs)` fills `PictureBox::job` and makes the boxes
`kPictureBoxHeightWithJob` (72) high instead of 58, so the rows and every line follow `PictureLayout::boxHeight`; a person
with no recorded job time has no line. The boxes grow for everyone when the box is checked, so the rows stay even.

### 4d. The town summary

The Family page's "Town summary" button (next to "Show family tree", set apart by a gap so the two read as separate functions) opens a window of its own (`summary_window.cpp`,
`Dialog/FamilyTreeSummary.rsc`; the game's own title bar, not modal, no pause, draggable, 572 px wide like the tree window)
with a scrolling list of rows. Nothing is stored for it: `family_summary.cpp` works every number out of the table, because
births, arrivals and deaths all carry a month stamp. The window looks again every 120 frames and rebuilds when the number
of people, the number alive or the year changes. The first rows are the town itself: a "Town" heading, the town's name
(the save name the status bar shows) and the date ("Year 12, Early Spring": the year and the season word the Event Log
prints); the date row's text is updated in place when the month moves on, without rebuilding the list. The rows, in order:

- **Town:** people recorded (living and died), where from (born here, arrived), the number of generations, and the average age
  at death with how many ages are recorded, and for those born here. A death that was only presumed (a person the record lost
  track of: cause "other" with an age of 0) counts as a death but not in the ages.
- **Deaths by cause:** each cause that has deaths, most first, with its share ("Old age: 12 (30%)").
- **By generation:** a person with no known parent is generation 1, anyone else is one more than the later of their parents
  (found by walking up the parents, not by id order); people and living in each.
- **Year by year:** the most recent year first: born, arrived, died, and living at the end of the year ("living" for the
  current year). At most the last 1,000 years; people from before them count as living at the start.

The rows reuse the Family page's relation cell with its person button hidden; a heading row has an empty note.

## 5. Limits

- The tree starts when the row is turned on; earlier history is not known.
- Jobs are counted from when the tree first sees a citizen; a citizen who was in the town before the row was turned on
  has no earlier jobs. The time between the last sweep and a death is not counted.
- The picture shows each person's main job only while the "Show each person's main job" check box is on (it starts on). The outline is
  the only other mark a person can have besides their gender and death.
- A citizen removed with no life event (a fire, a tornado, a mod) is recorded as dying of "other".
- A death cause is the last death event the citizen was given before removal; an event that did not kill (the game checks
  hunger, cold, disease, old age each tick and acts only when the citizen dies) is stored all the same, so a citizen who
  is later removed another way can carry an earlier cause.
- The father of a child is the mother's spouse at the birth. Retail has no other record.
- The whole-town picture has one row per generation within a band, with a marriage cluster (the people joined by marriages)
  in one row. The lineages with no known parents (the roots) stand side by side; a root that would pass
  `kPictureBandWidth` (4800 units) starts a new band below the last, one empty row between, so a long-lived town is a block
  and not a strip (`PictureLayout::rows` counts the rows drawn, `gens` the generations). A family that joins lineages
  of two bands is drawn as one long line across them (rare: a marriage between two lineages). When
  marriages loop back on a family (a grandson who marries his grandmother, as happens in a town that starts from two
  people), one parent link of the loop cannot be kept, and the people involved may be drawn above a parent, so their line
  comes up from below. The rows always settle, and families outside the loop are not affected.
- The drawn tree shows the picked person only (no whole-town tree); children hang from the person and their first wife or husband, whatever their other parent was; half brothers and sisters hang from the same parents as the full ones. The lists below are exact.

## 6. Tests

`tests/unit/ext_family_tree_test.cpp`: the registry row and its codec; the relations (children, siblings, spouses,
living); the table's rules (marriage, death, unknown parents, name length, repeated names); the wire format (round trip,
the live hint, every refusal); the jobs (units, the part of a unit that waits, the limit of 12 jobs and of 255 names,
the words for a span of time, the round trip, a version 1 payload, and a job that points past the names); the descendant counts (children, everyone below, the living, a person counted once, the dead and unknown ids); the town summary (the counts, the generations, the years, the rows' words and order, the limit of years); the descendants as ids and the outline of the picture (which people, the color used for nothing else, the line under the title). `ext_rsc_text_test.cpp` parses the page; `ext_toolbar_test.cpp` counts it. The seams, the
binding and the page need a running town and are not covered by the unit tests. Tried in game by the author of the request: the page, its records, the drawn tree and the citizen panel's button (2026-10-29); the search box, Show in town, the PNG and SVG save with the descendants outline and the job line, the Town summary rows with the name and date, and the record surviving a save and reload (2026-10-31).
