#include "PersonaCharacter.h"
#include "PersonaFramework.h"
#include "PersonaAttributes.h"
#include "PersonaInteraction.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/AudioComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputActionValue.h"
#include "Perception/AISense_Hearing.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystemComponent.h"

void UPersonaVoiceTalker::OnTalkingBegin(UAudioComponent* Audio)
{
    PlayingAudio = Audio; bTalking = true;
    if (Audio) Audio->SetVolumeMultiplier(OutputVolume);
    Super::OnTalkingBegin(Audio);
}
void UPersonaVoiceTalker::OnTalkingEnd() { bTalking = false; PlayingAudio = nullptr; Super::OnTalkingEnd(); }
void UPersonaVoiceTalker::SetOutputVolume(float Value)
{
    OutputVolume = Value;
    if (PlayingAudio) PlayingAudio->SetVolumeMultiplier(Value);
}

APersonaCharacter::APersonaCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetNetUpdateFrequency(30);
    GetCapsuleComponent()->InitCapsuleSize(32, 90);
    BaseEyeHeight = 68;
    CrouchedEyeHeight = 45;
    GetCharacterMovement()->SetCrouchedHalfHeight(60);
    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
    GetCharacterMovement()->MaxWalkSpeedCrouched = 140;
    GetCharacterMovement()->JumpZVelocity = 420;
    GetCharacterMovement()->AirControl = 0.2f;
    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
    GetMesh()->SetRelativeLocation(FVector(0, 0, -90));
    GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
    GetMesh()->SetOwnerNoSee(true);
    GetMesh()->SetCastHiddenShadow(true);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetRenderCustomDepth(true);
    GetMesh()->SetCustomDepthStencilValue(1);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(GetCapsuleComponent());
    Camera->SetRelativeLocation(FVector(0, 0, 68));
    Camera->bUsePawnControlRotation = true;
    Camera->FieldOfView = 90;
    FirstPersonArms = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonArms"));
    FirstPersonArms->SetupAttachment(Camera);
    FirstPersonArms->SetRelativeLocation(FVector(30, 0, -168));
    FirstPersonArms->SetRelativeRotation(FRotator(0, -90, 0));
    FirstPersonArms->SetOnlyOwnerSee(true);
    FirstPersonArms->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FirstPersonArms->SetCastShadow(false);
    Interaction = CreateDefaultSubobject<UPersonaInteraction>(TEXT("Interaction"));
    Voice = CreateDefaultSubobject<UPersonaVoiceTalker>(TEXT("Voice"));
    VoiceOrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VoiceOrigin"));
    VoiceOrigin->SetupAttachment(GetMesh(), TEXT("head"));
    NPCAbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("NPCAbilitySystem"));
    NPCAbilitySystem->SetIsReplicated(true);
    NPCAbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
    NPCAttributes = CreateDefaultSubobject<UPersonaAttributes>(TEXT("NPCAttributes"));
}
void APersonaCharacter::BeginPlay()
{
    Super::BeginPlay(); InitializeAbilities();
    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
    GetMesh()->SetOwnerNoSee(!bNPC);
    FirstPersonArms->SetVisibility(!bNPC);
    if (!bNPC) GetWorldTimerManager().SetTimer(VoiceRegistration, this, &ThisClass::RegisterVoice, 0.5f, true);
}
void APersonaCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HasAuthority() && Interaction) Interaction->ReleaseHeld(false);
    GetWorldTimerManager().ClearTimer(VoiceRegistration);
    if (InputSubsystem.IsValid() && IsValid(Mapping)) InputSubsystem->RemoveMappingContext(Mapping);
    Super::EndPlay(Reason);
}
void APersonaCharacter::PossessedBy(AController* NewController) { Super::PossessedBy(NewController); InitializeAbilities(); }
void APersonaCharacter::OnRep_PlayerState() { Super::OnRep_PlayerState(); InitializeAbilities(); }
UAbilitySystemComponent* APersonaCharacter::GetAbilitySystemComponent() const
{
    if (auto PS = GetPlayerState<APersonaPlayerState>()) return PS->AbilitySystem;
    return NPCAbilitySystem;
}
const UPersonaAttributes* APersonaCharacter::GetVitals() const
{
    if (auto PS = GetPlayerState<APersonaPlayerState>()) return PS->Attributes;
    return NPCAttributes;
}
bool APersonaCharacter::IsAlive() const { return GetVitals() && GetVitals()->GetHealth() > 0; }
void APersonaCharacter::InitializeAbilities()
{
    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
        ASC->InitAbilityActorInfo(GetPlayerState() ? static_cast<AActor*>(GetPlayerState()) : this, this);
}
void APersonaCharacter::RegisterVoice()
{
    if (bVoiceRegistered || !GetPlayerState() || !GetPlayerState()->GetUniqueId().IsValid()) return;
    Voice->Settings.ComponentToAttachTo = VoiceOrigin;
    USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(Voice);
    Attenuation->Attenuation.bAttenuate = true;
    Attenuation->Attenuation.bSpatialize = true;
    Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
    Attenuation->Attenuation.AttenuationShapeExtents = FVector(100);
    Attenuation->Attenuation.FalloffDistance = 2400;
    Attenuation->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
    Voice->Settings.AttenuationSettings = Attenuation;
    Voice->RegisterWithPlayerState(GetPlayerState());
    bVoiceRegistered = true;
    GetWorldTimerManager().ClearTimer(VoiceRegistration);
}
void APersonaCharacter::Tick(float Delta)
{
    Super::Tick(Delta);
    FVector CameraLocation = Camera->GetRelativeLocation();
    CameraLocation.Z = FMath::FInterpTo(CameraLocation.Z, bIsCrouched ? CrouchedEyeHeight : 68.f, Delta, 15.f);
    Camera->SetRelativeLocation(CameraLocation);
    if (HasAuthority())
    {
        FootstepTime += Delta; VitalsTime += Delta;
        if (GetVelocity().Size2D() > 30 && !GetCharacterMovement()->IsFalling() && FootstepTime > (bSprinting ? 0.3f : 0.55f))
        {
            FootstepTime = 0;
            UAISense_Hearing::ReportNoiseEvent(this, GetActorLocation(), bIsCrouched ? 0.12f : (bSprinting ? 1.f : 0.45f), this, 1800, TEXT("Footstep"));
        }
        if (!bNPC && VitalsTime >= 0.2f)
        {
            const float Change = bSprinting && GetVelocity().Size2D() > 20 ? -10.f : 8.f;
            auto ASC = GetAbilitySystemComponent();
            ASC->SetNumericAttributeBase(UPersonaAttributes::GetStaminaAttribute(), FMath::Clamp(GetVitals()->GetStamina() + Change * VitalsTime, 0.f, 100.f));
            VitalsTime = 0;
            if (GetVitals()->GetStamina() <= 0 && bSprinting) { bSprinting = false; OnRep_Sprinting(); }
        }
    }
}
float APersonaCharacter::TakeDamage(float Damage, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
    if (!HasAuthority() || !IsAlive() || !FMath::IsFinite(Damage) || Damage <= 0) return 0;
    if (auto GM = GetWorld()->GetAuthGameMode<APersonaGameMode>(); GM && !GM->CanDamage(EventInstigator, GetController())) return 0;
    auto ASC = GetAbilitySystemComponent();
    FGameplayEffectContextHandle Context = ASC->MakeEffectContext(); Context.AddInstigator(Causer, Causer);
    FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UPersonaDamageEffect::StaticClass(), 1, Context);
    Spec.Data->SetSetByCallerMagnitude(FName(TEXT("Damage")), -Damage);
    ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    return Damage;
}
void APersonaCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(APersonaCharacter, bSprinting);
}
void APersonaCharacter::ServerSetSprinting_Implementation(bool Enabled)
{
    bSprinting = Enabled && IsAlive() && GetVitals()->GetStamina() > 5 && !bIsCrouched;
    OnRep_Sprinting();
}
void APersonaCharacter::OnRep_Sprinting() { GetCharacterMovement()->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed; }
void APersonaCharacter::MoveForward(const FInputActionValue& V) { if (Controller && IsAlive()) AddMovementInput(FRotationMatrix(FRotator(0, Controller->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::X), V.Get<float>()); }
void APersonaCharacter::MoveRight(const FInputActionValue& V) { if (Controller && IsAlive()) AddMovementInput(FRotationMatrix(FRotator(0, Controller->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::Y), V.Get<float>()); }
void APersonaCharacter::LookYaw(const FInputActionValue& V) { AddControllerYawInput(V.Get<float>() * MouseSensitivity); }
void APersonaCharacter::LookPitch(const FInputActionValue& V) { AddControllerPitchInput(-V.Get<float>() * MouseSensitivity); }
void APersonaCharacter::SprintStart() { ServerSetSprinting(true); }
void APersonaCharacter::SprintEnd() { ServerSetSprinting(false); }
void APersonaCharacter::CrouchStart() { ServerSetSprinting(false); Crouch(); }
void APersonaCharacter::CrouchEnd() { UnCrouch(); }
void APersonaCharacter::Interact() { Interaction->TryInteract(); }
void APersonaCharacter::Throw() { Interaction->ServerRelease(true); }
void APersonaCharacter::VoiceStart() { if (auto PC = Cast<APlayerController>(Controller)) PC->StartTalking(); }
void APersonaCharacter::VoiceEnd() { if (auto PC = Cast<APlayerController>(Controller)) PC->StopTalking(); }
void APersonaCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    auto EI = CastChecked<UEnhancedInputComponent>(Input);
    auto PC = Cast<APlayerController>(Controller);
    if (!PC || !PC->GetLocalPlayer()) return;
    auto Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
    if (Mapping) Subsystem->RemoveMappingContext(Mapping);
    InputSubsystem = Subsystem;
    Mapping = NewObject<UInputMappingContext>(Subsystem); Actions.Reset();
    auto Action = [&](FName Name, EInputActionValueType Type, FKey Positive, FKey Negative = FKey())
    {
        UInputAction* A = NewObject<UInputAction>(Mapping, Name); A->ValueType = Type; Actions.Add(A);
        Mapping->MapKey(A, Positive);
        if (Negative.IsValid()) Mapping->MapKey(A, Negative).Modifiers.Add(NewObject<UInputModifierNegate>(Mapping));
        return A;
    };
    EI->BindAction(Action(TEXT("MoveForward"), EInputActionValueType::Axis1D, EKeys::W, EKeys::S), ETriggerEvent::Triggered, this, &ThisClass::MoveForward);
    EI->BindAction(Action(TEXT("MoveRight"), EInputActionValueType::Axis1D, EKeys::D, EKeys::A), ETriggerEvent::Triggered, this, &ThisClass::MoveRight);
    EI->BindAction(Action(TEXT("Yaw"), EInputActionValueType::Axis1D, EKeys::MouseX), ETriggerEvent::Triggered, this, &ThisClass::LookYaw);
    EI->BindAction(Action(TEXT("Pitch"), EInputActionValueType::Axis1D, EKeys::MouseY), ETriggerEvent::Triggered, this, &ThisClass::LookPitch);
    auto JumpAction = Action(TEXT("Jump"), EInputActionValueType::Boolean, EKeys::SpaceBar);
    EI->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
    EI->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
    auto Sprint = Action(TEXT("Sprint"), EInputActionValueType::Boolean, EKeys::LeftShift);
    EI->BindAction(Sprint, ETriggerEvent::Started, this, &ThisClass::SprintStart); EI->BindAction(Sprint, ETriggerEvent::Completed, this, &ThisClass::SprintEnd);
    auto CrouchAction = Action(TEXT("Crouch"), EInputActionValueType::Boolean, EKeys::LeftControl);
    EI->BindAction(CrouchAction, ETriggerEvent::Started, this, &ThisClass::CrouchStart); EI->BindAction(CrouchAction, ETriggerEvent::Completed, this, &ThisClass::CrouchEnd);
    EI->BindAction(Action(TEXT("Interact"), EInputActionValueType::Boolean, EKeys::E), ETriggerEvent::Started, this, &ThisClass::Interact);
    EI->BindAction(Action(TEXT("Throw"), EInputActionValueType::Boolean, EKeys::Q), ETriggerEvent::Started, this, &ThisClass::Throw);
    auto Talk = Action(TEXT("Talk"), EInputActionValueType::Boolean, EKeys::V);
    EI->BindAction(Talk, ETriggerEvent::Started, this, &ThisClass::VoiceStart); EI->BindAction(Talk, ETriggerEvent::Completed, this, &ThisClass::VoiceEnd);
    Subsystem->AddMappingContext(Mapping, 0);
}
void UPersonaAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    if (auto C = Cast<APersonaCharacter>(TryGetPawnOwner()))
    {
        Speed = C->GetVelocity().Size2D(); bFalling = C->GetCharacterMovement()->IsFalling(); bCrouched = C->bIsCrouched;
        const FVector Local = C->GetActorRotation().UnrotateVector(C->GetVelocity());
        Direction = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
        AimPitch = FRotator::NormalizeAxis(C->GetBaseAimRotation().Pitch);
    }
}
