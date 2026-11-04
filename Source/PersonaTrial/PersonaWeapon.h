#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "PersonaItemState.h"
#include "PersonaWeapon.generated.h"
class APersonaProp;
class APersonaCharacter;

UCLASS(BlueprintType)
class PERSONATRIAL_API UPersonaFXFrames : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) TArray<TObjectPtr<UStaticMesh>> Frames;
};
UCLASS()
class PERSONATRIAL_API APersonaFX : public AActor
{
    GENERATED_BODY()
public:
    APersonaFX();
    virtual void Tick(float Delta) override;
    UPROPERTY() TObjectPtr<UPersonaFXFrames> Frames;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Mesh;
    float Age=0;
    static void Play(UWorld* World,FName Kind,const FVector& Location,const FRotator& Rotation);
};

UCLASS(ClassGroup=(Persona), meta=(BlueprintSpawnableComponent))
class PERSONATRIAL_API UPersonaWeapon : public UActorComponent
{
    GENERATED_BODY()
public:
    UPersonaWeapon();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Fire();
    void SetAim(bool Value);
    void Load(bool Held);
    void Unload(bool Held);
    void Pump();
    void Inspect();
    void Safety();
    void Cancel();
    APersonaProp* Item() const;
    FVector Grip(bool Left, FVector Base) const;
    FTransform ViewTransform(FTransform Base) const;
    FTransform BallisticTransform() const;
    UFUNCTION(Server, Reliable) void ServerAction(uint8 Command, bool Held, APersonaProp* ExpectedGun);
    UFUNCTION(Server, Reliable) void ServerFire(FVector_NetQuantizeNormal Aim, int32 Sequence, uint8 SoundVariant, APersonaProp* ExpectedGun);
    UFUNCTION(Client, Reliable) void ClientFireState(APersonaProp* Gun, uint32 Revision, int32 Sequence, bool Fired, uint8 SoundVariant, const FString& Reason);
    void ResetSequence() { LocalSequence=LastServerSequence=LastPresentedSequence=BufferedSequence=0; bPumpQueued=false; NextShot=0; }
    UFUNCTION(NetMulticast, Unreliable) void ShotFX(int32 Sequence, uint8 SoundVariant, FVector_NetQuantize Muzzle);
    UFUNCTION(NetMulticast, Unreliable) void ImpactFX(FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal, bool Human, bool Soft = false);
    UFUNCTION(Client, Unreliable) void ClientNearMiss(FVector_NetQuantize Point);
    UFUNCTION(NetMulticast, Unreliable) void MechanicalFX(FName Sound);
    UFUNCTION(NetMulticast, Unreliable) void EjectShell();
    UPROPERTY(Replicated, BlueprintReadOnly) EPersonaWeaponAction Action = EPersonaWeaponAction::Idle;
    UPROPERTY(Replicated) float ActionStart = 0;
    UPROPERTY(Replicated) bool bAiming = false;
    float AimAlpha = 0;
    float Recoil = 0;
    int32 LastSoundVariant=0, NearMissCount=0, BodyImpactCount=0, WallImpactCount=0;
    float InspectUntil = 0;
    FString LastFireReason;
    TArray<FHitResult> LastPelletHits;
    int32 ShotsFired=0;
    FString DeferredAction() const;
    FString Inspection() const;
private:
    UPROPERTY() TArray<TObjectPtr<UObject>> PresentationAssets;
    float RecoilAge=1, AppliedCameraRecoil=0, ClippedPitchRecoil=0, RaiseUntil=0;
    float AppliedYawRecoil=0, ShotKick=0, ShotResidual=0, ShotYaw=0, ShotDuration=.55f, ShotAimScale=1;
    bool bPendingFire=false;
    float RecoilValue(const class UCurveFloat* Curve) const;
    TWeakObjectPtr<APersonaProp> ActiveItem;
    bool bLoadHeld = false, bUnloadHeld = false;
    float ActionEnd = 0, NextShot = 0;
    int32 LocalSequence = 0, LastServerSequence = 0;
    uint8 LastServerSoundVariant=0;
    uint32 StateRevision=0, AppliedStateRevision=0;
    int32 LastPresentedSequence=0, BufferedSequence=0;
    uint8 BufferedSound=0;
    FVector BufferedAim=FVector::ForwardVector;
    bool bPumpQueued=false;
    void Acknowledge(int32 Sequence=0, bool Fired=false, uint8 SoundVariant=0);
    void ExecuteFire(FVector Aim, int32 Sequence, uint8 SoundVariant);
    bool CanAct() const;
    void StartAction(EPersonaWeaponAction Value, float Duration);
    void FinishAction();
    void LocalShot(int32 Sequence, uint8 SoundVariant);
};
