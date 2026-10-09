#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GratiaExperienceToolsLibrary.generated.h"

class UFont;
class UFontFace;
class UStaticMesh;
class USkeletalMesh;

/** Editor helpers for setup_scene_experience.py that Python cannot reach (private engine fields). */
UCLASS()
class GRATIAVREDITORTOOLS_API UGratiaExperienceToolsLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Makes Font a runtime composite font whose default typeface has Names[i] -> Faces[i]. */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static bool BuildCompositeFont(UFont* Font, const TArray<FName>& Names, const TArray<UFontFace*>& Faces);
    /**
     * Font face asset (PackageName, e.g. /Game/UI/Font/MyFace) holding the .ttf/.otf file's data.
     * The stock importer asks Slate (dialog, font cache flush), which a -nullrhi commandlet has not.
     */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static UFontFace* ImportFontFace(const FString& Filename, const FString& PackageName);
    /**
     * Completes pending shader and asset compilation and streams every texture in, so a scene capture
     * from a commandlet shows final materials (Python cannot wait: the commandlet does not tick).
     */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static void FinishCompilationAndStreaming();
    /**
     * Reduces LOD 0 of Mesh to PercentTriangles of its source (built-in quadric reduction) and rebuilds it;
     * returns the triangle count of the rebuilt LOD 0 (-1 on failure). Works in a commandlet, where the
     * static mesh editor subsystem behind the Python helpers is not available.
     */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static int32 ReduceStaticMeshLOD0(UStaticMesh* Mesh, float PercentTriangles);
    /**
     * Static mesh (PackageName, e.g. /Game/Env/SM_RainStreaks; rebuilt in place when it exists) of Streaks rain
     * streaks for a rain animated by its material: each streak is two crossed quads Width x Length standing on the
     * disc of Radius around the origin (z 0..Length), in material slot "Rain". UV0 spans each quad (x across, y up
     * the streak); UV1 = (phase 0..1, brightness 0.5..1) of the streak. The material lifts a streak by up to
     * FallHeight and lets it fall; the bounds are extended by FallHeight upwards. Clearings (x, y, radius in mesh
     * space) stay dry: around the player's eyes (streaks there are large and costly in a headset) and through the
     * character. Returns null on bad input.
     */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static UStaticMesh* CreateRainStreakMesh(const FString& PackageName, int32 Streaks, float Radius, float Length, float Width,
        float FallHeight, int32 Seed, const TArray<FVector>& Clearings);
    /**
     * Adds or replaces MorphName on LOD 0 of Mesh (in its mesh description, so rebuilds keep it): the opening of a
     * channel whose entrance is the centre of EntranceBones and whose axis points to InwardBone (reference pose).
     * Every vertex near the axis moves away from it by OpeningCm within CoreRadiusCm, fading smoothly to nothing over
     * FalloffCm (kept at least twice OpeningCm so neighbouring vertices never cross: the skin stretches over a wide
     * area instead of tearing), and along the axis from OutsideCm before the entrance to InsideCm in. With Left/Right
     * bones (a slit) the opening is mostly across it: along the slit it is AlongSlit of the full amount. Skin and
     * clothing in the area move together. Returns the number of moved vertices, -1 on bad input.
     */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static int32 CreateChannelOpeningMorph(USkeletalMesh* Mesh, FName MorphName, const TArray<FName>& EntranceBones, FName InwardBone,
        const TArray<FName>& LeftBones, const TArray<FName>& RightBones, float OpeningCm, float CoreRadiusCm, float FalloffCm,
        float OutsideCm, float InsideCm, float AlongSlit);
    /**
     * Splits the triangles of Mesh (LOD 0, mesh description) whose centre lies within RadiusCm of any of Bones
     * (reference pose) into four, and their neighbours along the split edges into two or three, so there are no
     * T-junctions. New vertices take the average position, blended skin weights (every profile) and the average of
     * every morph delta; seam vertex instances stay separate. Runs once per Marker (a vertex attribute left on the
     * description), so a setup script can call it every time. Returns the number of split edges, 0 when already done,
     * -1 on bad input.
     */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static int32 SubdivideMeshAroundBones(USkeletalMesh* Mesh, const TArray<FName>& Bones, float RadiusCm, FName Marker);
    /**
     * Adds or replaces MorphName on LOD 0 of Mesh: a belly swelling in front of the point DepthCm along the channel
     * (entrance at the centre of EntranceBones, axis toward InwardBone, reference pose). Everything in front of that
     * point (the front: the side the centre of FrontBones lies on from BackBones, horizontally) moves forward by up to
     * AmountCm, whatever its depth, fading out across the belly over RadiusCm (kept at least twice AmountCm, so the
     * skin never folds); nothing behind the point moves, nor anything up to FloorCm above the centre of FrontBones (the
     * entrance, the pubic area and the thighs stay; the swelling fades in over 5 cm above that).
     * Returns the number of moved vertices, -1 on bad input.
     */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static int32 CreateChannelBulgeMorph(USkeletalMesh* Mesh, FName MorphName, const TArray<FName>& EntranceBones, FName InwardBone,
        float DepthCm, const TArray<FName>& FrontBones, const TArray<FName>& BackBones, float RadiusCm, float AmountCm, float FloorCm);
    /**
     * Writes how much each of Morphs (one or two, at weight 1) stretches the skin around every vertex into the vertex
     * colours of LOD 0 (the first morph in red, the second in green; blue 0, alpha 1): log2 of the area ratio of the
     * surrounding triangles over log2 FullAreaRatio, 0..1. A material reads it to soften and tint the stretched skin as
     * the morphs open (the mesh's materials must not use vertex colours otherwise). Returns the number of stretched
     * vertex instances, -1 on bad input.
     */
    UFUNCTION(BlueprintCallable, Category = "Gratia|Editor")
    static int32 BakeMorphStretchToVertexColor(USkeletalMesh* Mesh, const TArray<FName>& Morphs, float FullAreaRatio);
};
