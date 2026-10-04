# Character profiles

UGratiaCharacterProfile is the editable configuration boundary between generic
VR gameplay and an individual character. Profiles live in
/Game/Characters/Profiles. Each profile has the primary asset type
CharacterProfile and its ProfileId is its stable identifier.

## Ownership

The profile owns model-specific references and names:

- Skeletal mesh, PhysicsAsset, optional animation class, idle and diagnostic clips,
  soft and bright reaction clips.
- Semantic bone and morph maps. Gameplay asks for Head, LeftHand, BlinkLeft,
  Smile or Surprise; it does not contain exported Gratia bone names.
- Character contact-zone geometry and hold permissions.
- Separate sphere/capsule hand collision proxies. The initial profiles cover
  the head, torso/pelvis, upper arms, forearms, hands, thighs, shins and feet.
  These are conservative starting approximations, not certified surface matches.
- Audited secondary-bone groups, physical-drive settings, local spring settings,
  and Low/Medium/High simulation budgets.
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

Counts such as Gratia's 246 bones, 59 morphs and 176 physics bodies are regression
expectations for DA_Gratia; they are never requirements for another model.
