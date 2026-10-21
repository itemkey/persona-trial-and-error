#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Net/VoiceConfig.h"
#include "Animation/AnimInstance.h"
#include "PersonaCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UPersonaInteraction;
class UAbilitySystemComponent;
class UPersonaAttributes;
struct FInputActionValue;

UCLASS()
class PERSONATRIAL_API UPersonaVoiceTalker : public UVOIPTalker
{
    GENERATED_BODY()
public:
    virtual void OnTalkingBegin(UAudioComponent* Audio) override;
    virtual void OnTalkingEnd() override;
    void SetOutputVolume(float Value);
    UPROPERTY(BlueprintReadOnly) bool bTalking = false;
private:
    UPROPERTY() TObjectPtr<UAudioComponent> PlayingAudio;
    float OutputVolume = 1;
};

UCLASS()
class PERSONATRIAL_API APersonaCharacter : public ACharacter, public IAbilitySystemInterface
{
    GENERATED_BODY()
public:
    APersonaCharacter();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Delta) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual void PossessedBy(AController* NewController) override;
    virtual void OnRep_PlayerState() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual float TakeDamage(float Damage, const FDamageEvent& Event, AController* Instigator, AActor* Causer) override;
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    UFUNCTION(BlueprintPure) const UPersonaAttributes* GetVitals() const;
    UFUNCTION(BlueprintPure) bool IsAlive() const;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMeshComponent> FirstPersonArms;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UPersonaInteraction> Interaction;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UPersonaVoiceTalker> Voice;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USceneComponent> VoiceOrigin;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement") float WalkSpeed = 260;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement") float SprintSpeed = 440;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="View") float MouseSensitivity = 0.75f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI") bool bNPC = false;
    UPROPERTY(ReplicatedUsing=OnRep_Sprinting, BlueprintReadOnly) bool bSprinting = false;
    UFUNCTION(Server, Reliable) void ServerSetSprinting(bool Enabled);
    UFUNCTION() void OnRep_Sprinting();
    void InitializeAbilities();
private:
    UPROPERTY() TObjectPtr<UAbilitySystemComponent> NPCAbilitySystem;
    UPROPERTY() TObjectPtr<UPersonaAttributes> NPCAttributes;
    UPROPERTY() TObjectPtr<UInputMappingContext> Mapping;
    TWeakObjectPtr<class UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
    UPROPERTY() TArray<TObjectPtr<UInputAction>> Actions;
    FTimerHandle VoiceRegistration;
    float FootstepTime = 0;
    float VitalsTime = 0;
    bool bVoiceRegistered = false;
    void RegisterVoice();
    void MoveForward(const FInputActionValue& Value);
    void MoveRight(const FInputActionValue& Value);
    void LookYaw(const FInputActionValue& Value);
    void LookPitch(const FInputActionValue& Value);
    void SprintStart();
    void SprintEnd();
    void CrouchStart();
    void CrouchEnd();
    void Interact();
    void Throw();
    void VoiceStart();
    void VoiceEnd();
};

UCLASS(Transient, Blueprintable)
class PERSONATRIAL_API UPersonaAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    virtual void NativePostEvaluateAnimation() override;
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation") TObjectPtr<class UBlendSpace> Locomotion;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation") TObjectPtr<class UAnimSequence> JumpClip;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation") TObjectPtr<class UAnimSequence> HandsClip;
    UPROPERTY(BlueprintReadOnly) float Speed = 0;
    UPROPERTY(BlueprintReadOnly) float Direction = 0;
    UPROPERTY(BlueprintReadOnly) bool bFalling = false;
    UPROPERTY(BlueprintReadOnly) bool bCrouched = false;
    UPROPERTY(BlueprintReadOnly) float AimPitch = 0;
};
