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
    virtual void UpdateFromCompressedFlags(uint8 Flags) override;
    virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
    bool CanSprint() const;
};
