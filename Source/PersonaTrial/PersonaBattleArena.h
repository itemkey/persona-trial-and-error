#pragma once
#include "CoreMinimal.h"
#include "PersonaBattle.h"
#include "PersonaBattleArena.generated.h"
class APersonaHubObject;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class PERSONATRIAL_API APersonaBattleArena : public APersonaBattleState
{
    GENERATED_BODY()
public:
    APersonaBattleArena();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    bool SpawnPlayer(APersonaPlayerController* PC);
    void UseLift(APersonaCharacter* C);
    bool ClientReady() const;
    bool IsOnLift(APersonaCharacter* C) const;
    bool AllowsDamage() const { return Phase==EPersonaBattlePhase::Fighting; }
    static APersonaBattleArena* Get(UWorld* World);
    UPROPERTY(Replicated) TArray<TObjectPtr<APersonaHubObject>> Platforms;
    UPROPERTY(Replicated) float DescendAt=0;
    UPROPERTY(Replicated) float FightAt=0;
    UPROPERTY(Replicated) float HalfExtent=4000;
    UPROPERTY(Replicated) float WaitingUntil=0;
private:
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Faces;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Tints;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Doors;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Architecture;
    TSet<FString> Spawned;
    TMap<FString,float> DisconnectedAt;
    float ServerTime() const;
    void EnsureCabins();
    void SpawnCargo();
    FPersonaBattleSettlement Result(int32 Team) const;
    void ResolveOutcome();
    bool bResolutionPending=false;
};
