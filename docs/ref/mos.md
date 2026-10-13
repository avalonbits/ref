# Looking things up in mos

`mos` is the community Agon documentation,
[agon-docs](https://github.com/AgonPlatform/agon-docs), the whole wiki in
one file. A lookup in it knows:

| Ask for | Like |
|---|---|
| VDU sequences, numbers written any way MOS reads them | `ref mos vdu 23 27 3`, `ref mos 23,27,&21` |
| MOS API calls, by name or number | `ref mos mos_fopen`, `ref mos mos 0x0a` |
| RST calls | `ref mos rst 08h`, `ref mos rst 8` |
| C functions | `ref mos f_printf` |
| star commands and their aliases | `ref mos cd`, `ref mos dir` |
| system variables | `ref mos run$path`, `ref mos hotkey$1`, `ref mos sysvar_scrmode` |
| PLOT codes | `ref mos plot 85`, `ref mos vdu 25 85` |
| BBC BASIC keywords | `ref mos gcol` |
| any word of a heading | `ref mos copper` |

## Numbers

A number is read in every form MOS reads one -- `&21`, `0x21`, `21h`,
`33` -- and a leading number is a VDU sequence: `ref mos 23,0,&a0` is
`vdu 23 0 160`.

## Parameters

The parameters a command names before its number can be anything:
`ref mos vdu 23 0 &a0 7; 6` finds buffered command 6, whichever buffer it
names.
