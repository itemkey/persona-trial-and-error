#pragma once
#include "CoreMinimal.h"
#include "PersonaInteraction.h"
#include "PersonaSurvival.generated.h"

namespace PersonaForest
{
    inline const FName TrialId(TEXT("SurvivalAgility"));
    constexpr float Radius = 2100.f;
    bool TouchesFace(const FVector& Offset, float CapsuleRadius, float HalfHeight);
    FString Duration(float Seconds);
    FVector CarryCenter(const APersonaCharacter& C);
}

UENUM()
enum class EForestOutcome : uint8 { Active, Escaped, Killed };

USTRUCT()
struct FForestPlayer
{
    GENERATED_BODY()
    UPROPERTY() FString Id;
    UPROPERTY() float Water = 100;
    UPROPERTY() bool HasMap = false;
    UPROPERTY() EForestOutcome Outcome = EForestOutcome::Active;

    UPROPERTY() float Seconds = 0;
};

USTRUCT()
struct FForestRun
{
    GENERATED_BODY()
    UPROPERTY() int32 Generation = 0; // Zero is the fixed 0.9.0 landscape.
    UPROPERTY() TArray<FForestPlayer> Players;
    UPROPERTY() FVector Center = FVector::ZeroVector;
    UPROPERTY() FString Carrier;
    UPROPERTY() FVector LastGround = FVector::ZeroVector;
    UPROPERTY() FVector CasePosition = FVector::ZeroVector;
    UPROPERTY() float Elapsed = 0;
    UPROPERTY() float NextRaid = 65;
    UPROPERTY() float NextSupplies = 420;
    FForestPlayer* Player(const FString& Id) { return Players.FindByPredicate([&](const auto& P){ return P.Id==Id; }); }
    const FForestPlayer* Player(const FString& Id) const { return Players.FindByPredicate([&](const auto& P){ return P.Id==Id; }); }
};

USTRUCT()
struct FForestResult
{
    GENERATED_BODY()
    UPROPERTY() FString Id;
    UPROPERTY() float Best = 0;
    UPROPERTY() float Last = 0;
    UPROPERTY() EForestOutcome Outcome = EForestOutcome::Active;
};

UCLASS()
class PERSONATRIAL_API APersonaForestFlag : public AActor, public IPersonaInteractable
{
    GENERATED_BODY()
public:
    APersonaForestFlag();
    virtual void Tick(float Delta) override;
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(Replicated) TObjectPtr<APersonaCharacter> Bearer;
    virtual FText InteractionLabel_Implementation() const override;
    virtual void Interact_Implementation(APersonaCharacter* C) override;
private:
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Pole;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Faces;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> Tints;
};

UCLASS()
class PERSONATRIAL_API APersonaMedicalCase : public AActor, public IPersonaInteractable
{
    GENERATED_BODY()
public:
    APersonaMedicalCase();
    virtual FText InteractionLabel_Implementation() const override;
    virtual void Interact_Implementation(APersonaCharacter* C) override;
};

