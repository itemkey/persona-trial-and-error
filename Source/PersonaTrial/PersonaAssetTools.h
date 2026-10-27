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
    static void EnsureAttachmentSockets(class USkeleton* Skeleton);
    UFUNCTION(BlueprintCallable, Category="Persona|Editor")
    static bool BuildNavigation(class UWorld* World);
    UFUNCTION(BlueprintCallable, Category="Persona|Editor")
    static void ConfigureRecoilCurve(class UCurveFloat* Curve, int32 Profile);
};
