#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PersonaMovement.generated.h"

// Intent is part of each saved move, so correction/replay uses the same gait.
UCLASS()
class PERSONATRIAL_API UPersonaMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    UPersonaMovement();
    bool bWantsSprint = false;
    bool bWantsAim = false;
    UPROPERTY(EditAnywhere, Category="Movement") float AimSpeed = 300;
    UPROPERTY(EditAnywhere, Category="Movement") float CrouchedAimSpeed = 180;
    virtual float GetMaxSpeed() const override;
    virtual bool ServerCheckClientError(float TimeStamp,float DeltaTime,const FVector& Accel,const FVector& ClientWorldLocation,const FVector& RelativeClientLocation,FMovementBaseInterfaceData* Base,FName Bone,uint8 Mode) override;
    virtual void ServerMoveHandleClientError(float TimeStamp,float DeltaTime,const FVector& Accel,const FVector& RelativeClientLocation,FMovementBaseInterfaceData* Base,FName Bone,uint8 Mode) override;
    virtual void UpdateFromCompressedFlags(uint8 Flags) override;
    virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
    bool CanSprint() const;
    FString TraversalFailure;
    int32 CollisionRevision=0;
    virtual void HandleImpact(const FHitResult& Hit,float TimeSlice=0.f,const FVector& MoveDelta=FVector::ZeroVector) override;
    bool BeginTraversal(const FVector& FloorTarget,UPrimitiveComponent* Support);
    void AbortTraversal();
    virtual void PhysCustom(float DeltaTime,int32 Iterations) override;
private:
    float NPCSpeedLimit=-1;
    TWeakObjectPtr<UPrimitiveComponent> TraversalSupport;
    FTransform TraversalSupportTransform;
};
