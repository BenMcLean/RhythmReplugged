# Replugged Mode Specification

## Purpose

Replugged Mode is a multi-instrument gameplay mode inspired by *Rock Band Unplugged* on the PSP.

The player claims multiple instrument lanes but can actively play only one lane at a time. The mode is built around periodically "locking" lanes so the player can leave them temporarily and switch to other lanes that need attention.

This document defines the intended gameplay rules for Replugged Mode independently of any specific implementation.

## Scope

This specification covers:

- lane claim assumptions
- initial lane selection
- locked and unlocked lane behavior
- how progress toward a lock is earned
- how and when a lock actually begins
- lock duration
- empty measure behavior
- unlock staggering
- keep-busy behavior
- mute behavior
- UI expectations for the lock bar and lane visuals

This specification does not define:

- scoring
- star power mechanics
- solo-specific lane-collapse rules
- multiplayer runtime input handling

## Terms

### Claimed lane

An instrument lane locally claimed by the player for this song.

### Active lane

The one claimed lane the player is currently playing.

### Measure

A section of time bounded by two consecutive measure lines from the chart.

### Empty measure

A measure in a lane that contains no note starts in that lane.

### Building

The state where an unlocked lane is accumulating correct-measure progress toward its next lock.

### Ready

The state where a lane has earned a possible future lock, but continued correct play is still required before that lock becomes guaranteed.

### Committed

The state where the future lock is now guaranteed, but the locked section has not started yet.

### Locked

The state where the lane is temporarily exempt from play and its locked section is already in effect.

### Safely in the future

A future measure start after the section currently being played.

Practical meaning:

- A lane must never lock the section currently being played.
- A newly earned lock should normally target the next measure.

This is intentionally a gameplay rule, not a renderer-specific timing constant.

## Availability

Replugged Mode is available only when the local player has at least two claimed playable instruments after any reservation filtering.

## Initial Conditions

At song start:

- all claimed lanes begin locked except the initial active lane
- the initial active lane begins unlocked

The initial active lane is chosen using this priority:

1. If one claimed lane has the earliest first note, choose that lane.
2. If multiple claimed lanes share the earliest first note and one of them is guitar, choose guitar.
3. Otherwise choose the rightmost lane among the tied lanes.

## Lane States

Each claimed lane behaves as if it has the following conceptual states:

- `Locked`
- `Unlocked`
- `Building`
- `Ready`
- `Committed`

`Unlocked`, `Building`, and `Ready` are playable states. `Building` means the lane is accumulating valid progress toward its next lock. `Ready` means a future lock is being attempted but is still fragile. `Committed` is a pre-locked transition state: the future lock is guaranteed, but the locked section has not started yet.

## Core Locking Rules

### General principle

A lane is locked to let the player leave it and switch to another lane that needs attention.

Therefore:

- a lane must never become locked for the section currently being played
- a lane should normally lock the immediate next eligible measure
- a lane should not lock during a measure where it is the only lane in the song with notes in that same measure

### Measures required to earn a lock

It takes 2 full correct measures to earn a lock.

A full correct measure means:

- play successfully from one measure line to the next
- all required notes in that measure must be hit
- partial play that begins in the middle of a measure does not count as one of the required lock measures

### When success is known

Lock progress is based on full measures, but success does not need to be deferred until the boundary itself.

If the player hits the last required note of the second required measure, then the game already knows that lane has successfully completed the required work needed to begin attempting a future lock.

That lane may therefore enter `Ready` as soon as the final required note is hit, even if the actual lock will only begin at a later measure boundary.

### Ready still requires correct play

A lane in `Ready` is still playable and still fragile.

Missing a note while `Ready` has the same effect as missing a note while `Building`:

- the pending lock is lost
- the lane returns to ordinary unlocked play
- progress toward the next lock resets

### Committed no longer requires further success

A lane in `Committed` has already resolved the ambiguity of whether the lock will happen.

This occurs when the player has continued through the `Ready` section and has hit the last required note before the future locked section begins.

At that moment:

- the future locked section is guaranteed
- the lock bar may turn green immediately
- notes in the future locked section may disappear immediately
- the lane is still not yet `Locked` until the locked section actually starts
- further player input no longer affects whether that pending lock will occur

### Missing notes

If the player misses a note while the lane is unlocked, building, or ready:

- lock progress resets
- any pending ready-state lock is cancelled

This rule does not apply while the lane is already locked.

## Choosing the Next Locked Measure

Once a lane has entered `Ready`, the game chooses the earliest future measure that satisfies all of the following:

1. The measure begins safely in the future.
2. The measure is not the section currently being played.
3. At least one other lane has notes in that same measure.

This means:

- the next measure is preferred
- if the next measure has no other-lane notes, choose a later measure
- the search continues measure by measure until an eligible measure is found

If the player later reaches the end of the playable `Ready` section without making a mistake, the lane enters `Committed` and that already-chosen future locked section becomes definite.

### Only-thing-happening rule

A lane must not lock during a measure where no other lane has any notes in that same measure.

Reason:

- if the current lane is the only thing happening musically, locking it would leave the player with nothing to do

This rule is measure-based. It is not based on lookahead, prompt logic, or whether another lane feels soon enough to switch to.

## Empty Measures

Empty measures are always treated as locked measures.

Implications:

- they should not require play
- their notes should not need to be "earned" for a lock
- they should not break lock flow
- they should visually behave as locked content

## Lock Start Behavior

When the chosen future locked measure begins:

- the lane becomes `Locked`
- the lock applies only to the locked section and later locked sections within its duration
- notes in the currently played section must never disappear retroactively

The section currently being played must remain readable and playable through its end, even if the final note of that section is what completed the work needed to earn the lock.

## Lock Duration

Lock duration is based on claimed instrument count.

Formula:

`2 measures required to earn the lock * number of claimed instruments`

Examples:

- 2 claimed instruments: 4 locked measures
- 3 claimed instruments: 6 locked measures
- 4 claimed instruments: 8 locked measures

## Unlock Behavior

When a lock expires:

- the lane becomes unlocked again
- this change applies only to future content
- it must not occur mid-measure in a way that makes already-playing notes suddenly appear or disappear

## Unlock Staggering

Newly unlocked lanes should be staggered so perfect play is achievable.

Operational intent:

- multiple lanes should not newly unlock on the same measure when the game can avoid it
- the staggering target is 2 measures apart

Rationale:

- 2 measures to earn another lock

It is still possible for multiple lanes to be simultaneously unlocked because of mistakes. What must be avoided is multiple lanes being newly scheduled to come unlocked on the same measure during otherwise correct play.

## Keep Busy Rule

If all lanes are locked, the mode should not leave the player with nothing to do for an excessively long span.

Therefore:

- if all lanes are locked, remaining lock times may be shortened so that the next lane needing attention comes unlocked sooner
- this shortening must only affect future sections
- it must never unlock a lane mid-measure
- it must never create an immediate unfair miss trap

If the current lane would otherwise lock away the only interesting material in the chart before another lane becomes relevant, it should remain effectively available until another lane actually has notes in the same measure as a future lock target.

## Mute Behavior

Muting in Replugged remains note-based.

Rules:

- missing a note while unlocked mutes that lane
- hitting a note while unlocked immediately unmutes that lane again
- a lane does not need to wait until it becomes locked to become audible again after a mistake
- a locked lane remains audible while locked

## Visual Behavior

### Locked sections

While a lane is locked:

- timing bars continue scrolling
- note gems that start inside locked sections do not appear
- locked content is hidden per section, not as a whole-lane on/off toggle

### Ready-state ambiguity

During `Ready`, the future remains ambiguous because the player can still lose the pending lock by missing a note.

Therefore, in `Ready`:

- the actual future notes should still appear
- the background note-lane colors should stop at the boundary where the future lock would begin if the player succeeds
- this expresses that the future locked section is being attempted, but is not yet guaranteed

If the player makes a mistake during `Ready`:

- the future notes for the hoped-for locked section remain visible
- the lane colors extend again to ordinary unlocked presentation
- the game may later choose a different future lock boundary if the player recovers and earns another lock

### Committed-state future hiding

During `Committed`, the ambiguity has been resolved.

Therefore, in `Committed`:

- the lock bar may already be green even though the locked section has not started
- the notes in the future locked section may disappear
- the background note-lane colors remain absent for that future locked section

This is the moment when the player should visually understand that the future locked section is definitely happening.

### Lane colors

The background colors for the individual note columns must disappear only for locked sections.

They must not:

- disappear for the entire lane vertically
- disappear for unlocked sections
- reappear inside a section that is supposed to remain locked

### Future visibility

Unlocked future notes must remain visible.

The player must be able to see upcoming unlocked content before it becomes playable.

During `Ready`, the future lock target is still uncertain, so future note gems remain visible even beyond the hoped-for lock boundary.

During `Committed`, the future lock target is certain, so future note gems in that locked section may disappear before the locked section actually starts.

## Lock Bar Behavior

The per-lane progress bar does double duty as both lock-build and lock-warning feedback.

### Yellow build

- the bar fills yellow while building toward a lock
- it reflects the percentage of elapsed time within the full required build window

The required build window is:

- start: the start of the first of the 2 required correct note-bearing measures
- end: the last required note hit in the second required correct note-bearing measure

Therefore:

- the yellow fill is not a simple per-measure counter
- it should begin filling during the first required measure
- it should continue filling smoothly across the full two-measure build window
- it reaches full yellow when the last required note for earning `Ready` is hit

### Full yellow ready state

- once the lane has earned a future lock, the bar becomes full yellow
- it stays full yellow while the future lock is still contingent on continued correct play

### Green committed state

- once the last required note of the `Ready` section is hit, the bar turns green
- this green means the future lock is guaranteed even though the locked section has not started yet

### Green locked state

- when the future locked section starts, the lane is now actually locked
- it stays full green through most of the locked duration
- it does not start draining immediately

### Green warning drain

- the green bar begins draining only in the final 2 measures before unlock
- this drain is a warning that playable content is approaching again

## Summary of Intended Flow

The intended lock flow for a lane is:

1. `Unlocked`
2. `Building` after valid correct-measure play begins
3. `Ready` once the second required measure has been secured by hitting its final required note
4. `Committed` once the last required note before the future locked section has been hit without a mistake
5. `Locked` when the earliest eligible future measure begins
6. `Unlocked` again when the lock duration expires

At any point before `Committed`, a miss resets the lane back to ordinary unlocked play and clears its progress.

## Implementation Notes

This section is informative, not normative.

The hardest edge in the rules is not sustain handling; it is timing the handoff between:

- the moment the game knows a lock has been earned
- the measure where that lock is actually allowed to start

The design intent is:

- earn `Ready` as soon as the second required measure has been secured by its final required note
- remain visually and mechanically contingent during the `Ready` section
- promote to `Committed` when the final required note before the future locked section is hit
- apply the actual `Locked` state at the earliest future measure that is both fair and useful

This is why `Ready`, `Committed`, and `Locked` must be treated as distinct gameplay concepts.

## Known Future Design Questions

This specification intentionally leaves some future topics open:

- how to treat extremely long sustains that span multiple measures or sections
- how scoring interacts with lock progress
- how solos override or replace ordinary lane visibility
- how star power can force or extend locks
- how multiplayer runtime lane ownership affects active prompting and lock targeting
