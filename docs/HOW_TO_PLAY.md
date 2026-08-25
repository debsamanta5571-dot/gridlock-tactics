# How to Play Gridlock Tactics

## 1. Winning

Kill the other base. When it hits 0 health, that's the match.

Running out of cards doesn't lose. If both decks are empty and both bases are still up, a 5-round timer starts. After that, higher base health wins. Same health is a draw.

## 2. Decks

Each player has three decks:

- **Main deck:** 40 units and spells
- **Reserve:** up to 5 cards, only playable on that player's turn
- **Territory deck:** 20 lands

Start with 6 in hand. The first player doesn't draw on turn one. Holding more than 10 at the end of a turn means discarding down to 10.

Dead units and discarded cards stay in the discard pile. They don't go back into the deck.

## 3. The board

The map is 8 tiles wide and 12 long. Each side has a **base** at its end of the board, 4 tiles wide and 2 deep. The tiles right in front of it are the **deployment strip**. New units go there unless a card says they can go somewhere else.

Some units cover more than one tile. To move one of those, pick an anchor tile, rotate it if needed, then confirm.

Cards use two words for nearby tiles:

- **Adjacent** is the four tiles that share a side. No diagonals.
- **Surrounding** is all eight neighbors, diagonals included.

## 4. Energy

Most actions cost energy, and leftover energy doesn't carry over. At the end of the current player's turn it all goes away, including energy picked up on the opponent's turn.

Energy comes from **territories**. Some lands just pay when a cost is paid. Others have a **Use land** button, once per turn (section 12).

**Flux** is a second pool. It can pay for spells and abilities, but not for putting units on the board.

Colors matter. Gallantry is green, Ingenuity is orange, Mythology is turquoise. Omni and neutral can cover more than one of those.

## 5. Turn structure

Turns go:

1. **Energy.** Three territories come off the deck. Keep one, or skip.
2. **Main.** Play units, move, cast slow spells, use abilities, use lands.
3. **Spell window.** Only if a spell was queued. Fast spells can answer it.
4. **Attack.** Declare attacks. If any went down, there's a defense window.
5. **Second main.** Same as Main.
6. **Second spell window.** Same as the first.
7. **Bonus attack.** Only units that got a bonus attack or bonus move.
8. End of turn. Regen and damage-over-time happen, then leftover energy is gone.

The spell and defense windows stay closed unless there's something to answer. Didn't queue a spell? No spell window. Didn't declare an attack? No defense window.

**Pass** (or P) means done with the current window. **End main phase** leaves Main. **Commit attacks** locks in the declared attacks.

## 6. Energy phase

A card is drawn at the start of the turn, except on the first turn of the match. Then three lands from the territory deck show up.

Click the one to keep. It goes onto the territory row. The other two shuffle back. **Skip Territory** if none of them are worth taking.

Some lands come in **depleted**, so they can't be used the turn they arrive. They work starting the next turn.

**Groundwork** only happens if the new land has it, and the land placed before that was a basic of the same color.

If a land wants a tile as it comes in, pick that tile before doing anything else.

## 7. Playing units

In Main or Second Main, click a unit in hand (or in the reserve, on that player's own turn). Legal tiles light up, usually on the deployment strip. Click one, pay the cost, and the unit comes in with **deployment fatigue** until the start of its owner's next turn.

While it's fatigued:

- A normal unit can still melee. It can't move, use abilities, or take a ranged shot it chose to take. Defend and Dash are off. A Focus spell from hand can still use this unit as the caster.
- **Haste** can move, and that's it from the list above.
- **Surge** can attack, use abilities, Defend, and Dash, but it can't move.
- **Charge** and buildings skip fatigue.

**Command** on a friendly unit can let small allies come in next to it instead of on the strip, if the card says so.

Deploying uses up undo for that phase.

## 8. Movement

Select a unit and click a reachable empty tile. That's a preview; it doesn't cost anything. Rotate a large unit if needed, then confirm, or right-click to cancel. Confirming costs one move.

If a unit is allowed to move, it can always take at least one step. **Taunt** keeps an enemy from walking off if it shares a side with the taunting unit. **Bleed** hits when that unit chooses to move.

Declaring an attack also spends the unused standard move for the turn. Take the attack back and the move comes back with it.

## 9. Combat

Attacks aren't declared in Main. Wait for the **Attack** phase.

Select a unit and click a legal enemy or the base. Red tiles are the valid ones. After the attacks are in, click **Commit attacks**. The other player can still play fast spells and abilities, then the attacks all resolve.

Melee has to be next to the target. Ranged needs range and line of sight. Hybrid can do either. Armor cuts physical, magic resist cuts magic, pure ignores both.

The defender usually **counterattacks** if it still can. **Shadowstrike** stops that. **First strike** stops it if the hit kills. Crits deal max damage times 1.5, rounded down. Bases don't take crits.

**Defend** is a Main action. It spends the unit's attack for armor until its next turn, and abilities can't be used while defending. **Dash** spends an attack for extra movement.

If the attacker or the target is already gone when that swing would happen, it's skipped. Energy already spent stays spent.

## 10. Spells, abilities, and the queue

Spells and abilities go on a queue. The other player can answer. Then the queue resolves.

Three speeds:

- **Channeled** is slow. Main only. It sits until main is ended.
- **Reflex** is fast. It can be played in response windows and while attacks are being declared.
- **Blazing** happens right away.

After both players pass, the last group of plays happens first. Inside one player's group, they happen in the order they were played.

If the caster or the target is gone when a waiting effect would happen, it does nothing and the energy is gone. **Undo** can take back a play that's still waiting, before things start resolving.

**Focus** spells pick a friendly unit as the caster. Range and line of sight come from that unit. A stunned unit can't be picked. A silenced unit can, but its keywords don't apply to the spell.

**Multicast** pays once and picks several different targets, up to the number on the card.

## 11. Targeting

Legal tiles go blue. Area effects go orange on hover. Click a highlighted tile. If the effect needs a unit there, there has to actually be one.

Standing next to **Taunt** usually means attacks and targeted abilities have to go to that unit first.

**Stealth** can't be picked as a direct target. An area effect can still hit it if the card allows it.

For an ability: select the unit, click the ability, then click a target if it needs one. Some abilities want an empty tile instead of a unit.

## 12. Territories in play

Territories are on the right. Interactive lands have **Use land** buttons.

Each land can be used once per turn, even if it has more than one ability. Pay the cost if there is one. After that it sits until the start of the next turn.

Lands with no use button just pay energy when a cost is paid.

## 13. Status effects

- **Poison** and **Fire** deal damage at the end of that unit's owner's turn, then lose 1 stack.
- **Bleed** deals damage when the unit chooses to move. Stacks still drop by 1 at turn end if it stayed put.
- **Stunned** can't move, attack, use abilities, or cast Focus.
- **Rooted** can't move.
- **Silenced** turns off keywords and passives. A Focus spell from hand can still use the unit as caster.
- **Jammed** can't use activated abilities.
- **Overload** explodes at 3 or more stacks at turn end. The unit takes damage and nearby units get hit too.
- **Shield**, **Barrier**, and **Bonus health** are extra health. Barrier goes away at turn end.
- **Evasive** is a 50% miss chance against attacks while stacks are on the unit.

## 14. Keywords

Ones that come up a lot:

- **Flying** and **Trueshot** change line of sight and cover on attacks.
- **Taunt** is in the targeting section.
- **Haste**, **Surge**, and **Charge** change fatigue.
- **Vigilance** gives extra reactions.
- **Indestructible** can't be damaged or destroyed. Silence turns that off.
- **Immovable** can't be pushed. It can still walk on its own.
- **Stockpile** lets the same card be played more than once from hand, once per turn, full cost each time.

Hover a keyword if it isn't listed here. The game has a glossary.

**Scan** looks at the top of the deck and can discard some of those cards. Finish the scan before doing anything else.

## 15. Controls

Hand is along the bottom. Click a card, then click the board.

Click a friendly unit to select it, then a blue tile to preview a move. Confirm to spend the move, or right-click to cancel.

In the attack phase, a red tile declares an attack on an enemy or the base.

Territories are on the right. Land choices show up at the start of the turn.

**End main phase** leaves Main. **Commit attacks** finishes declaring. **Pass** is for a response window with nothing to play; P works too. **Undo** takes back a waiting play or a declared attack if that's still allowed.

Right-click cancels a move preview, an armed card, or an armed ability. Escape or Backspace opens options, including quit and back to the main menu.

Combat sometimes plays a short animation. Let it finish.
