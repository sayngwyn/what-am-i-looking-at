# Changelog

## 1.0.0

First release, for Skyrim SE/AE and Fallout 4.

- Open the console and left-click an object, bare ground, water or the sky to see what you hit on a card you can move and resize.
- The card marks the exact part you clicked and shows its textures as thumbnails, with each file's name, size, mip count and the archive it came from.
- It also shows the shader, flags, glossiness, vertex data and mesh size, and on Fallout 4 the material file.
- Bare ground shows its texture layers and how much of each shows. Water shows its type, colors and wave textures.
- The arrows in the card's header (or `lookingat next` in the console) step to the next thing along the same line of sight.
- Click a person or creature to see its skin and armor parts the same way.
- A red warning appears when a part has a problem the card can spot, such as a missing texture or a normal map with no mip levels.
- Every pick is saved to `last-pick.json` with everything on the card and more.
- Works on Skyrim SE 1.5.97, AE 1.6.1170 and 1.7.104, and Fallout 4 1.10.163, 1.10.984 and 1.11.240 (one file for all three).
