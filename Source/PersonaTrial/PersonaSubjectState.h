#pragma once
#include "CoreMinimal.h"
#include "PersonaItemState.h"
#include "PersonaCombat.h"
#include "PersonaSubjectState.generated.h"

USTRUCT()
struct FPersonaSubjectRecord
{
    GENERATED_BODY()
    UPROPERTY() FString Id;
    UPROPERTY() FString Name;
    UPROPERTY() FTransform Transform;
    UPROPERTY() FVector Velocity = FVector::ZeroVector;
    UPROPERTY() FRotator View = FRotator::ZeroRotator;
    UPROPERTY() float Health = 100;
    UPROPERTY() float MaxHealth = 100;
    UPROPERTY() float DamageScale = 1;
    UPROPERTY() bool Dead = false;
    UPROPERTY() bool Spectator = false;
    UPROPERTY() bool Transit = false;
    UPROPERTY() TArray<FTransform> CorpsePose;
    UPROPERTY() FTransform CorpseMesh;
    UPROPERTY() bool CorpseMoving=false;
    UPROPERTY() TArray<FPersonaBodyMotion> BodyMotion;
    UPROPERTY() TArray<FPersonaInventoryRecord> Loot;
    UPROPERTY() float Stamina = 100;
    UPROPERTY() float Hunger = 100;
    UPROPERTY() float Water = 100;
    UPROPERTY() bool DryCollapse = false;
    UPROPERTY() int32 Team = 0;
    UPROPERTY() uint8 Movement = 1;
    UPROPERTY() bool Crouched = false;
    UPROPERTY() bool Sprinting = false;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool NPC = false;
    UPROPERTY() FString Target;
    UPROPERTY() FVector Investigate = FVector::ZeroVector;
    UPROPERTY() FVector Patrol = FVector::ZeroVector;
    UPROPERTY() FVector MoveGoal = FVector::ZeroVector;
    UPROPERTY() bool Moving = false;
    UPROPERTY() float InvestigationRemaining = 0;
    UPROPERTY() float AttackRemaining = 0;
    UPROPERTY() FName LastSense;
    UPROPERTY() int32 TacticalSeed=0;
    UPROPERTY() int32 TacticalRandomSeed=0;
    UPROPERTY() uint8 Tactic=0;
    UPROPERTY() float CommitRemaining=0;
    UPROPERTY() float SprintRemaining=0;
    UPROPERTY() float LegImpairmentRemaining=0;
    UPROPERTY() float ImpairmentImmunityRemaining=0;
    UPROPERTY() TArray<FPersonaInventoryRecord> Inventory;
    UPROPERTY() int32 SelectedSlot = 0;
};
