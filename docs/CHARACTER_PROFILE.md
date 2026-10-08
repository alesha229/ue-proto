# Character profiles

UGratiaCharacterProfile is the editable configuration boundary between generic
VR gameplay and an individual character. Profiles live in
/Game/Characters/Profiles. Each profile has the primary asset type
CharacterProfile and its ProfileId is its stable identifier.

## Ownership

The profile owns model-specific references and names:

- Skeletal mesh, PhysicsAsset, optional animation class, idle and diagnostic clips,
  soft and bright reaction clips.
- PerformanceClips: named full-body clips (Name, Clip, bLoop) that the Pose menu item
  and F2 cycle through after Idle/Arms/Head. They play from the start with native
  single-node playback; their own face curves replace the procedural blink, and
  secondary motion stays active. `-GratiaPoseTest=Performance [-GratiaPerformance=N]`
  starts one directly; runtime QA checks that each starts, loops as configured,
  advances and has finite morph curves. Gratia: `Idle ZZZ`, `KM466` (retargeted VaM mocap,
  docs/MOCAP_KM466.md). `Segments` (optional) continue a long take part by part as one
  performance (each part plays once; the whole performance loops; parts share their
  boundary frame); the menu shows `Name n/N`, QA checks the chain.
  `Scene` (optional) carries the rest of the source scene: `Music` (played by the character's
  `UGratiaPerformanceStage` at the performance clock across segments, re-synced after a seek,
  loop or hitch beyond `MusicResyncSeconds`; non-spatial; menu Sound switches it off),
  `PartnerMesh` + `PartnerTransform` + `PartnerPose` (a static partner body: each aim turns a bone
  so its reference child lies along From -> To in the partner's component space, parents first),
  `PartnerHiddenInViewpoint` and `bHasViewpoint`/`Viewpoint` (the partner's eyes; menu **View**
  switches to them; Recenter while lying (HMD < 1.3 m, head tilted) enters it automatically and
  Recenter while standing leaves it: the head goes to the partner's eyes and the body axis
  follows the top of the head). Runtime QA checks partner aims (<= 3 deg), the viewpoint and music
  sync. Gratia: KM466 performances use SKM_Manny_Simple posed like the VaM partner atom
  (`extract_vam_scene_partner.py`, `import_performance_scene.py`, docs/MOCAP_KM466.md).
- StrongReactionClip and MoodReactionClips: a touch faster than
  `ContactSettings.StrongReactionSpeedCmPerSecond` plays StrongReactionClip; otherwise the
  current mood (`Calm`, `Cheerful`, `Reserved`; menu item Mood) selects MoodReactionClips;
  otherwise ReactionClips zone routing. Gratia: ReactStartle (fast), ReactHappy (Cheerful),
  ReactShy (Reserved), ReactPout (zone Hair) — Zenless-Zone-Zero-style clips from
  `author_anime_clips.py`. Runtime QA checks the selection; reaction QA
  `-GratiaReactionMood=N` checks the mood clips in the packaged game.
- ReactionLines: what the character answers a reaction with — a speech bubble (`Text`, shown by
  her head, facing the player, for `ContactSettings.CaptionSeconds`) and a voice (`Sound`, played
  at the touched zone). A line may be limited to a mood (`Mood` 0 calm, 1 cheerful, 2 reserved),
  to contact zones or penetration channels (`Zones`) and to a touch force (`Force`: Gentle, or
  Strong = faster than `StrongReactionSpeedCmPerSecond`). The presenter takes the most specific
  matches (strong touch, then zone, then mood) and picks one at random, never the same line twice in
  a row. Without lines a profile keeps ReactionSounds/DefaultReactionSound (or a short chime) and
  shows no bubble. Gratia: 33 Russian lines and 21 synthesized non-verbal voice clips
  (`generate_reaction_voice.py`, `setup_character_presentation.py`).
- Free-play stances: in a scene the Pose item switches between idle and every PerformanceClip that is
  a looping single clip without music, partner or viewpoint; `Label` is what the menu shows
  (Gratia: `Idle ZZZ` is «игривая»). Diagnostic Arms/Head and full performances stay on F2 in the studio.
- Semantic bone and morph maps. Gameplay asks for Head, LeftHand, BlinkLeft,
  Smile or Surprise; it does not contain exported Gratia bone names.
- Character contact-zone geometry and hold permissions.
- BodySurface capsules may carry `Influences` (bone + share): the capsule is skinned like the
  skin it was fitted to (a butt sphere follows pelvis and thigh). `MeasureSphereSurface` fills
  them from the part's own skin; `MeasureLimbSurface` counts the flesh of limb soft bones
  (thigh jiggle bones) for their limb. Soft-body chains whose rest tip lies inside the body
  colliders (limb chains) ignore the colliders along their own bone.
- SpringChains (hair, clothing decor, ears/tail; menu groups 1/2/4): KawaiiPhysics chains
  from root bones to tip dummies (`TipLengthCm` along `ForwardAxis`), spring/damping/world
  damping, gravity relative to the authored pose, collision with BodySurface capsules within
  the chain's reach (shrunk to the rest pose; only capsules at least `SpringMinColliderRadiusCm`
  thick) and one sphere per hand (`SpringHandRadiusCm`, no finger spheres), trigger grab within
  `SpringGrabRadiusCm` of that sphere. Validation rejects a spring-chain bone that is still a Chaos
  secondary body (`bSafeSimulation`). A chain rooted below another chain's root is excluded
  from it and simulated after it (strands branching off a ponytail).
- Separate sphere/capsule hand collision proxies. The initial profiles cover
  the head, torso/pelvis, upper arms, forearms, hands, thighs, shins and feet.
  These are conservative starting approximations, not certified surface matches.
- Audited secondary-bone groups, physical-drive settings, local spring settings,
  and Low/Medium/High simulation budgets.
- HandPhysics pressure/grab radii, stiffness, damping, aggregate force limits,
  speed/travel limits and grab break distance. MaxSecondaryCollisionSizeCm
  bounds the world collision size checked by runtime QA (including actor scale).
- ClothSettings, SourceClothCages and SourceClothRegions for native soft-surface
  simulation, hand collision/grab limits, expected particle counts and semantic anchors.
- Capability flags that explicitly describe unavailable optional features.
- Forward/up axes and gaze limits.
- Optional model-specific regression counts.

The profile does not own the player's locomotion, VR controller tracking, room
props or a particular level. A scene cube is a world interaction target, not a
character's anatomical contact zone.

Current primary semantic bones are Root, Head, Neck, UpperChest, Chest,
Pelvis, LeftHand, RightHand, LeftForearm, RightForearm, LeftUpperArm,
RightUpperArm, LeftThigh, RightThigh, LeftShin, RightShin, LeftFoot, RightFoot,
LeftToe and RightToe.

The current facial keys are BlinkLeft, BlinkRight, Smile, BrowsUp,
Surprise, MouthOpen, MouthWide, LookLeft, LookRight, LookUp, and
LookDown. Additional model-specific shapes can remain on the mesh without
being exposed to general gameplay.

## Creating the initial assets

Compile the editor module, then execute
GratiaVR/Scripts/create_character_profiles.py with Unreal's Python commandlet
or connected Unreal editor. It creates DA_Gratia, validates it and saves it.
Existing profiles retain artist-edited settings on subsequent runs. Pass
-GratiaRegenerateProfiles only when explicitly regenerating the authored defaults.
It reads the audited secondary definitions from
GratiaSecondaryBones.h; this generated header remains a porting/editor input
and should not be a runtime model-name dependency.

The script also creates DA_Mannequin from an actual full-body Epic mannequin.
If absent from the project, it can seed installed Epic template resources at
their original /Game/Characters/Mannequins paths, preserving their package
references and never overwriting existing assets. The XR mannequin hand meshes
are not treated as a full-body replacement.

The mannequin profile enables contact, gaze and local acknowledgement sound.
It disables facial morph reactions, blink, authored reaction clips and accessory
simulation because those resources are not provided. No Gratia animation is
assigned to its different skeleton.

Results and provenance are written to evidence/04/character_profiles.json.
Successful asset creation is separate from successful runtime replacement
testing; both must be recorded.

## Runtime contract

Use ResolveBone and ResolveMorph; an unresolved optional key returns None.
Gate optional behavior with Capabilities, and check that an asset/key exists
before evaluating it. ValidateProfile(Errors, Warnings) rejects invalid
configured names, clip skeleton mismatches, invalid geometry/settings and
missing required resources for enabled capabilities. A profile with no idle
clip may display its reference pose; that is a warning rather than an invalid
skeletal mesh.

GetQualitySettings returns clamped quality-index settings.
GetSecondaryGroupSettings and FindSecondaryBone expose the profile's
secondary metadata. Groups are 1 hair, 2 cloth/decor, 3 local body and 4
ears/tail. Keep chain anchors and planted body parts animation-controlled.

## Native source cloth — implemented, editor QA PASS

The 5 October 2026 port places TitsPhys, AssPhys and ThighsPhys in one
mesh-owned `GratiaSourceCloth_BodyCages` asset: 3112 source vertices and 206 full
pins. SourceClothCages identifies its name, group 3 and expected particle count;
SourceClothRegions records each source range and a semantic animation anchor.
These are profile data, not runtime character-name lookups.

`port_source_cloth_cages.py` enables ClothSettings and disables safe rigid-body
simulation for group 3. Hair, clothing/decor accessories and ears/tail retain
their existing bone path and Low/Medium/High caps. Native body cloth currently
uses the same cage particle count at all three qualities; it has no particle LOD
budget. `bBodyMotion`, `bPhysicalMotion` and ClothSettings.bEnabled control it.
Do not read the old four-body group-3 cap as a cloth particle budget.

ClothSettings exposes hand/grab radii, break distance, maximum hand travel/speed,
grab stiffness/velocity blending, an emergency particle-offset limit and
SoftPressDepthCm (default 4 cm): how far the cloth hand collider may follow the raw
controller past the proxy-constrained visible hand, so a press can dent soft tissue.
GrabRadiusCm is measured beyond the hand collider surface (HandRadiusCm).
The cloth asset is a `UGratiaSourceClothingAsset`; it restores the stored per-section
render binding whenever Unreal rebuilds the mesh. The port excludes vertices whose
dominant bone is under the profile's Head/LeftFoot/RightFoot semantics. Native
material/pin/pressure settings live on the embedded clothing asset and remain
editable in Unreal's clothing editor. Artist tuning requires new QA evidence.

Only source cages enabled in Blender render are ported. Hair's Tail main/Tail R
SurfaceDeform paths are disabled in the source and remain excluded. The solver
and units differ from Blender, so settings are an adaptation rather than 1:1
simulation. Active cloth vertices blend away ordinary positional corrective
morph offsets; excluded face/hand vertices keep their original skinning/morph
path and morph assets are retained. SurfaceDeform overlaps use the nearest
eligible cage and strongest mask, rather than serial modifiers.

Source-cage import, actual visible deformation, both hands and reset behavior
still require editor/package QA for a specific build. Headset acceptance is
separate. A profile with no source cloth must leave ClothSettings disabled;
absence of cages or particles is diagnosed, not treated as successful simulation.

AnimationClass can select a future character-specific animation Blueprint.
The current native single-node path controls preview clips and procedural
reactions. The generated native profiles leave AnimationClass empty so clip
selection and playback use this native path. An arbitrary animation Blueprint does not automatically implement
that contract; integrate and test its state/event interface explicitly.

UGratiaAnimationProfileLibrary.GetCharacterAnimationSnapshot provides a
Blueprint-readable snapshot containing profile, look target, bounded target
head angles, reaction weight/impulse/serial, mood, quality, idle state and motion
switches. In a normal Animation Blueprint, cast Get Owning Actor to
GratiaPreviewCharacter and obtain this snapshot on Event Blueprint Update
Animation. Copy values into Blueprint variables consumed by the AnimGraph.
The helper is a game-thread API; it is not marked BlueprintThreadSafe.

The native UGratiaAnimInstance still inherits UAnimSingleNodeInstance, which is
not a normal editable AnimGraph host. Selecting an Animation Blueprint class
allows a manually authored graph to run, but this change does not convert the
existing procedural proxy into graphical nodes or build a complete equivalent
Animation Blueprint. Diagnostic clips and reaction blending remain the native
path's responsibility unless the custom graph explicitly implements them.

Secondary group drive strengths, blend weights, spring limits/local axes,
stiffness, damping and head inertia are editable profile values. Native physics
and procedural animation read them through the profile. Physics fault distance
and check interval are profile settings as well.

Planted idle is an opt-in validation contract. Gratia enables it with 0.1 cm
position and 0.1 degree rotation limits for feet/root. The new mannequin profile
does not claim this contract until its idle has been independently checked.

Hard UObject references are intentional in this MVP: loading a profile retains
its resources for cooking and playback. For many large interchangeable
characters, migrate these references to soft references and explicit
asynchronous loading through Asset Manager as a separate measured change.

## Adding another model

1. Prepare the source model via Blender MCP, preserve its source, and export a
   dedicated game rig with documented units and deformation checks.
2. Import to a new asset folder and build a matching PhysicsAsset. Retarget
   animations to that model's skeleton before assigning them.
3. Create a profile, populate semantic mappings and contact geometry, then
   enable only the capabilities supported by that model's resources.
4. Run profile validation and packaged replacement tests. Check rest pose,
   gaze axes, missing optional features, repeated switches, contacts, core
   anchoring and physics disable/reset.
5. Review materials from every side after textures have loaded. Validate
   tracking, input, hand alignment and performance separately in the headset.

Counts such as Gratia's 246 bones, 67 morphs and 176 physics bodies are regression
expectations for DA_Gratia; they are never requirements for another model.

PhysicsAsset shape dimensions must use the bone's local units. FBX bones with
scale 100 require converting centimetre dimensions before storage; otherwise
small hair/cloth colliders become metre-sized. The Gratia repair script updates
only audited secondary bodies, preserves constraints and stores an idempotent
BoneLocalV2 marker. Inspect world bounds when importing another skeleton.
