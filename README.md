# Gridlock Tactics

This is a work in progress prototype for a multiplayer tactical card game me and my friends are making. Of course they wanted the comp sci major to actually program everything while they think of "ideas" nevertheless its going sort of smoothly even if the UI is kind of a mess.

## Preview

![Gameplay](preview.gif)

## Architecture

The true purpose of this is to eventually integrate it with some sort of real world smart board, where it can register game and smart pieces straight to the software with websockets(change sofware_
Technically its not actually made with c++ unreal objects but it is a separate c++ module loaded into whats effectively a GUI. The C++ standaolone is meant to be able to create small portable servers with micro ccomputers. It can also just work standalone with unreal with a 3d board gui visual. Currrently there is a board and a battle visualization however the art assets are currently all place holders.. Unreal works by effectively sending cmds lines to its own running c++ instance with the 3d board gui/

# Instructions
If you want to actually try this game, (i don't think its fully stable yet, the c++ is fine but the unreal gui still needs a lot of polishing) you will need to read the instructions its fairly complex"

## Open
Add packaged build instructions and c++ server instructuions (don't recommend c++ server its mean to eventually have a web gui to alk to"

#Made with C++ and Unreal 5.8
