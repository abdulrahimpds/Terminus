# Terminus (formerly HorseMenu)

A mod menu for Red Dead Redemption 2 and Red Dead Online published by Rockstar Games. Strictly for educational purposes.

## How to use
Use a popular injector (Xenos/Extreme Injector/Etc.) and inject into rdr2.exe

(INSERT) Open/Close the menu

## Structure

- `core/` : Essential general features for the base
- `game/` : Game specific implemented things
- `util/` : Loose functions that aren't game specific

## Screenshots / UI Design

![image](https://github.com/user-attachments/assets/e7280722-9ea0-49e5-b6ae-299ebb2a071d)
![image](https://github.com/user-attachments/assets/94ebbbf6-250b-4e3f-8a47-fe95bb6855c8)
![image](https://github.com/user-attachments/assets/05fb0e82-fcbd-46dc-a686-77c145d21bcd)


## Fork and attribution

This project is a fork of YimMenu/HorseMenu: https://github.com/YimMenu/HorseMenu

## Enhancements in this fork

- Spawners
  - complete Ped list organized into logical sections; includes special variants where applicable
  - improved companion flow and settings, including an "Armed" checkbox
  - fixed horse godmode and related persistence edge cases
- Protection
  - hardened task-tree validation: immediate quarantine on whitelist violation; increased scan depth (16 → 64) with defensive quarantine when exceeded
  - safer pointer/range checks and clearer block/quarantine logging to aid incident analysis
- General fixes
  - multiple bug fixes and UI polish; experimental paths remain disabled by default to reduce false positives
