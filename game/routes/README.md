# Routes

Pad routes for unattended game runs (`BB_ROUTE=game/routes/NAME.route tools/run_game.sh ...`),
played by `tools/play_route.py` (format in its docstring). They wait on on-screen text, so they
keep working when loading times or dialogs differ.

- `new_game.route`: title -> New Game -> brightness -> controls -> opening cutscene (skipped) ->
  character creation (name "Hunter") -> contract. Finalising the contract saves; with a save
  present the title menu continues it instead, so use `continue.route` from then on.
- `continue.route`: loads the save into Iosefka's Clinic (the game autosaves there; only the very
  first load after the contract plays the transfusion cutscene first) and walks a few steps.

Record only what a run is for (`BB_CAPTURE_ONLY=function_name`): recording every hooked
function crashes the runtime in the opening cutscene (STATUS.md, "Watch").
