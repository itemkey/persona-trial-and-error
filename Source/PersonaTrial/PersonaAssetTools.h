#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PersonaAssetTools.generated.h"

UCLASS()
class UPersonaAssetTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Persona|Editor")
    static void ConfigureNPCHitVolumes(class UPersonaCombatProfile* Profile, class USkeletalMesh* Mesh);
    UFUNCTION(BlueprintCallable, Category="Persona|Editor")
    static void EnsureAttachmentSockets(class USkeleton* Skeleton);
    UFUNCTION(BlueprintCallable, Category="Persona|Editor")
    static bool BuildNavigation(class UWorld* World);
    UFUNCTION(BlueprintCallable, Category="Persona|Editor")
    static void ConfigureRecoilCurve(class UCurveFloat* Curve, int32 Profile);
    UFUNCTION(BlueprintCallable, Category="Persona|Editor")
    static void BakeStanceClip(class UAnimSequence* Source,class UAnimSequence* Target,int32 Kind);
    UFUNCTION(BlueprintCallable, Category="Persona|Editor")
    static void BakeCrouchBlend(class UBlendSpace* Target);
    UFUNCTION(BlueprintCallable, Category="Persona|Editor")
    static void ConfigureBodyPhysics(class UPhysicsAsset* Asset);
    UFUNCTION(BlueprintCallable, Category="Persona|Editor")
    static void ConfigureNPCPhysics(class UPhysicsAsset* Asset,class USkeletalMesh* Mesh);
    UFUNCTION(BlueprintCallable, Category="Persona|Editor")
    static void ConfigureNPCBlend(class UBlendSpace* Blend,bool Crouched);
    UFUNCTION(BlueprintCallable, Category="Persona|Editor")
    static void NormalizeNPCClip(class UAnimSequence* Source,class UAnimSequence* Target);
};
