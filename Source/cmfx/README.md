# cmfx (vendored subset)

`Common.h` and `Svf.h` are copied unchanged from Brimstone's `Source/fx`
(commit `1bc0b1f`): header-only, standard library only. See Brimstone's
`Source/fx/README.md` for the rules every cmfx file follows.

Plydrm uses:

- `SvfCoef` / `SvfState` (`Svf.h`): the drum voices' filters
- `driveSample` / `DriveType` (`Common.h`): the per-voice drive
- `Smoothed` (`Common.h`): the master gain

To update, copy the files again from Brimstone. If more cmfx effects are
needed, consider making cmfx its own repository and pulling it into both
projects instead of copying.
