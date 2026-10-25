#include "PersonaCharacter.h"
#include "PersonaFramework.h"
#include "PersonaAttributes.h"
#include "PersonaInteraction.h"
#include "PersonaWeapon.h"
#include "PersonaLoot.h"
#include "PersonaAI.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "Engine/DamageEvents.h"
#include "BrainComponent.h"
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
    GetCharacterMovement()->MaxWalkSpeedCrouched = 180;
    GetCharacterMovement()->MaxAcceleration = 1800;
    GetCharacterMovement()->BrakingDecelerationWalking = 1400;
    GetCharacterMovement()->GroundFriction = 5;
    GetCharacterMovement()->JumpZVelocity = 420;
    GetCharacterMovement()->AirControl = 0.2f;
    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
    GetMesh()->SetRelativeLocation(FVector(0, 0, -90));
    GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
    GetMesh()->SetOwnerNoSee(true);
    // The camera-relative arms use a different pose from the full body. Do not
    // project the hidden head/body across them with conventional shadow maps.
    // The visible third-person body still casts shadows for other participants.
    GetMesh()->SetCastHiddenShadow(false);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetRenderCustomDepth(true);
    GetMesh()->SetCustomDepthStencilValue(1);
    GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(GetCapsuleComponent());
    Camera->SetRelativeLocation(FVector(0, 0, 68));
    Camera->bUsePawnControlRotation = true;
    Camera->FieldOfView = 90;
    FirstPersonArms = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonArms"));
    FirstPersonArms->SetupAttachment(Camera);
    FirstPersonArms->SetRelativeLocation(FVector(0, 0, -158));
    FirstPersonArms->SetRelativeRotation(FRotator(0, -90, 0));
    FirstPersonArms->SetOnlyOwnerSee(true);
    FirstPersonArms->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FirstPersonArms->SetCastShadow(false);
    FirstPersonArms->SetBoundsScale(3.f);
    FirstPersonArms->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Interaction = CreateDefaultSubobject<UPersonaInteraction>(TEXT("Interaction"));
    Weapon=CreateDefaultSubobject<UPersonaWeapon>(TEXT("Weapon"));
    Loot=CreateDefaultSubobject<UPersonaLoot>(TEXT("Loot")); Loot->bDeposit=false;
    PhysicalAnimation=CreateDefaultSubobject<UPhysicalAnimationComponent>(TEXT("PhysicalAnimation"));
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
    if (HasAuthority() && bNPC && PersistentId.IsEmpty()) PersistentId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
    GetMesh()->SetOwnerNoSee(!bNPC);
    FirstPersonArms->SetVisibility(false);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    GetMesh()->SetCollisionResponseToAllChannels(ECR_Ignore);
    GetMesh()->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Block);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Ignore);
    if(!bNPC) GetCapsuleComponent()->SetCollisionObjectType(ECC_GameTraceChannel2);
    PhysicalAnimation->SetSkeletalMeshComponent(GetMesh());
    if (!bNPC) GetWorldTimerManager().SetTimer(VoiceRegistration, this, &ThisClass::RegisterVoice, 0.5f, true);
}
void APersonaCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HasAuthority() && Interaction) Interaction->ReleaseAll();
    GetWorldTimerManager().ClearTimer(VoiceRegistration);
    if (InputSubsystem.IsValid() && IsValid(Mapping)) InputSubsystem->RemoveMappingContext(Mapping);
    Super::EndPlay(Reason);
}
void APersonaCharacter::PossessedBy(AController* NewController) { Super::PossessedBy(NewController); InitializeAbilities(); Weapon->ResetSequence(); }
void APersonaCharacter::OnRep_PlayerState() { Super::OnRep_PlayerState(); InitializeAbilities(); }
void APersonaCharacter::ClientResumeMovement_Implementation(bool Crouched, FVector Velocity)
{
    GetCharacterMovement()->bWantsToCrouch = Crouched;
    if (Crouched) Crouch(); else UnCrouch();
    GetCharacterMovement()->Velocity = Velocity;
}
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
bool APersonaCharacter::IsAlive() const { return !bDead && !bSpectator && GetVitals() && GetVitals()->GetHealth() > 0; }
void APersonaCharacter::InitializeAbilities()
{
    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
        ASC->InitAbilityActorInfo(GetPlayerState() ? static_cast<AActor*>(GetPlayerState()) : this, this);
    if (!bNPC && HasActorBegunPlay())
    {
        bVoiceRegistered = false;
        GetWorldTimerManager().SetTimer(VoiceRegistration,this,&ThisClass::RegisterVoice,.5f,true);
    }
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
    FirstPersonArms->SetVisibility(!bNPC && IsAlive() && (Interaction->Held || bShowActionHands));
    if(Interaction->bCharging && (Interaction->ChargedItem!=Interaction->Held || !IsAlive())) Interaction->CancelActions();
    if(Interaction->LootTarget && !Interaction->CanReachLoot(Interaction->LootTarget)) Interaction->CloseLoot();
    if(HasAuthority() && bDead && CorpsePose.IsEmpty()) { DeathAge+=Delta; if(DeathAge>3) FreezeCorpse(); }
    if(!bDead && HitAge>0) { HitAge-=Delta; GetMesh()->SetAllBodiesPhysicsBlendWeight(FMath::Clamp(HitAge/.35f,0.f,1.f)*.45f); if(HitAge<=0) { GetMesh()->SetAllBodiesSimulatePhysics(false); GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly); } }
    // Step phase follows travelled distance and stops in the air.
    const bool Grounded = GetCharacterMovement()->IsMovingOnGround();
    if (Grounded) HandMotionTime = FMath::Fmod(HandMotionTime + Delta * GetVelocity().Size2D() * (bSprinting ? .016f : .021f), 2*PI);
    HandAirOffset = FMath::FInterpTo(HandAirOffset, Grounded ? 0.f : FMath::Clamp(-GetVelocity().Z * .004f,-1.2f,1.2f),Delta,8.f);
    HandMoveAlpha = FMath::FInterpTo(HandMoveAlpha, IsAlive() && !bOffline ? (Grounded ? FMath::Clamp(GetVelocity().Size2D() / WalkSpeed, 0.f, 1.f) : HandMoveAlpha) : 0.f, Delta, 6.f);
    CarryAlpha = FMath::FInterpTo(CarryAlpha, Interaction->Held ? 1.f : 0.f, Delta, 10.f);
    if (HasAuthority() && Interaction->Held)
    {
        const FVector Start = GetPawnViewLocation();
        const FRotator Aim = GetBaseAimRotation();
        FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(CarrySpace), false, this);
        Params.AddIgnoredActor(Interaction->Held);
        const FVector End = Start + Aim.RotateVector(FVector(65,0,-23));
        GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(23), Params);
        CarryDistance = Hit.bBlockingHit ? FMath::Clamp(65.f * Hit.Time, 28.f, 65.f) : 65.f;
    }
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
        if (!bNPC && !bOffline && IsAlive() && VitalsTime >= 0.2f)
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
    if (auto GM = GetWorld()->GetAuthGameMode<APersonaGameMode>())
    {
        if (!GM->CanDamage(EventInstigator, GetController())) return 0;
        if (bOffline && !GM->bFriendlyFire && EventInstigator)
            if (auto Attacker = EventInstigator->GetPlayerState<APersonaPlayerState>(); Attacker && Attacker->Team == SavedTeam) return 0;
    }
    auto ASC = GetAbilitySystemComponent();
    FGameplayEffectContextHandle Context = ASC->MakeEffectContext(); Context.AddInstigator(Causer, Causer);
    FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UPersonaDamageEffect::StaticClass(), 1, Context);
    Spec.Data->SetSetByCallerMagnitude(FName(TEXT("Damage")), -Damage);
    ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    FName Bone=TEXT("spine_02"); FVector Direction=Causer ? (GetActorLocation()-Causer->GetActorLocation()).GetSafeNormal() : GetActorForwardVector();
    if(Event.IsOfType(FPointDamageEvent::ClassID)) { const auto& Point=static_cast<const FPointDamageEvent&>(Event); Bone=Point.HitInfo.BoneName; Direction=Point.ShotDirection; }
    if(GetVitals()->GetHealth()<=0)
    {
        bDead=true;
        for(auto P:Interaction->Slots) if(P) { Loot->Items.Add(UPersonaLoot::Capture(P)); P->Destroy(); }
        Interaction->Slots.Init(nullptr,3); Interaction->Held=nullptr;
        if(bNPC) Loot->Add(TEXT("/Game/Persona/Items/DA_Ammo12GA.DA_Ammo12GA"),6);
        OnRep_Dead(); ForceNetUpdate();
    }
    HitReaction(Bone,Direction,FMath::Clamp(Damage*3,40.f,350.f));
    if(auto AI=Cast<APersonaAIController>(Controller); AI && !bDead) { AI->Target=EventInstigator ? EventInstigator->GetPawn() : nullptr; AI->StaggerUntil=GetWorld()->GetTimeSeconds()+.35f; AI->StopMovement(); }
    return Damage;
}
void APersonaCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(APersonaCharacter, bSprinting);
    DOREPLIFETIME(APersonaCharacter, bOffline); DOREPLIFETIME(APersonaCharacter, PersistentId);
    DOREPLIFETIME(APersonaCharacter, SavedTeam); DOREPLIFETIME(APersonaCharacter, CarryDistance);
    DOREPLIFETIME(APersonaCharacter, OfflineView);
    DOREPLIFETIME(APersonaCharacter,bDead); DOREPLIFETIME(APersonaCharacter,bSpectator); DOREPLIFETIME(APersonaCharacter,bTransit);
    DOREPLIFETIME(APersonaCharacter,DamageScale); DOREPLIFETIME(APersonaCharacter,CorpsePose); DOREPLIFETIME(APersonaCharacter,CorpseMeshTransform);
}
FRotator APersonaCharacter::GetBaseAimRotation() const { return bOffline ? OfflineView : Super::GetBaseAimRotation(); }
FTransform APersonaCharacter::GetCarryTransform(bool FirstPerson) const
{
    const auto Item = Interaction->Held;
    FTransform Local = Item ? Item->CarryLocal() : FTransform(FVector(65,0,-23));
    if (FirstPerson) return Local * Camera->GetComponentTransform();
    FRotator Aim = GetBaseAimRotation(); Aim.Pitch = FMath::ClampAngle(Aim.Pitch,-55.f,25.f);
    // Collision retracts the world representation. The owner's camera-space
    // viewmodel never consumes delayed replicated obstruction distances.
    FVector Offset=Local.GetLocation(); Offset.X -= FMath::Max(0.f,65.f-CarryDistance);
    Local.SetLocation(Offset);
    return Local * FTransform(Aim,GetPawnViewLocation());
}
void APersonaCharacter::SetVitals(float Health, float Stamina, float Hunger)
{
    auto ASC = GetAbilitySystemComponent();
    ASC->SetNumericAttributeBase(UPersonaAttributes::GetHealthAttribute(), Health);
    ASC->SetNumericAttributeBase(UPersonaAttributes::GetStaminaAttribute(), Stamina);
    ASC->SetNumericAttributeBase(UPersonaAttributes::GetHungerAttribute(), Hunger);
}
void APersonaCharacter::BecomeOffline()
{
    if (!HasAuthority()) return;
    const auto V = GetVitals(); const float H = V->GetHealth(), S = V->GetStamina(), F = V->GetHunger();
    OfflineView = GetBaseAimRotation();
    if (auto PS = GetPlayerState<APersonaPlayerState>()) { SavedTeam = PS->Team; SavedReady = PS->bReady; SavedPlayerName = PS->GetPlayerName(); }
    if (Controller) Controller->UnPossess();
    SetPlayerState(nullptr); InitializeAbilities(); SetVitals(H,S,F);
    bOffline = true; bRestoredBody = false; bSprinting = false; OnRep_Sprinting(); SetOwner(nullptr);
    GetCharacterMovement()->StopMovementImmediately();
    Voice->SetOutputVolume(0); ForceNetUpdate();
}
void APersonaCharacter::ServerSetSprinting_Implementation(bool Enabled)
{
    bSprinting = Enabled && IsAlive() && GetVitals()->GetStamina() > 5 && !bIsCrouched;
    OnRep_Sprinting();
}
void APersonaCharacter::OnRep_Sprinting() { GetCharacterMovement()->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed; }
void APersonaCharacter::MoveForward(const FInputActionValue& V) { if (Controller && IsAlive() && !bTransit) AddMovementInput(FRotationMatrix(FRotator(0, Controller->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::X), V.Get<float>()); }
void APersonaCharacter::MoveRight(const FInputActionValue& V) { if (Controller && IsAlive() && !bTransit) AddMovementInput(FRotationMatrix(FRotator(0, Controller->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::Y), V.Get<float>()); }
void APersonaCharacter::LookYaw(const FInputActionValue& V) { AddControllerYawInput(V.Get<float>() * MouseSensitivity); }
void APersonaCharacter::LookPitch(const FInputActionValue& V) { AddControllerPitchInput(-V.Get<float>() * MouseSensitivity); }
void APersonaCharacter::SprintStart() { ServerSetSprinting(true); }
void APersonaCharacter::SprintEnd() { ServerSetSprinting(false); }
void APersonaCharacter::CrouchStart() { if(!IsAlive() || bTransit) return; ServerSetSprinting(false); Crouch(); }
void APersonaCharacter::CrouchEnd() { UnCrouch(); }
void APersonaCharacter::Interact() { Interaction->TryInteract(); }
void APersonaCharacter::Throw() { Interaction->ServerRelease(true); }
void APersonaCharacter::Drop() { Interaction->BeginDrop(); }
void APersonaCharacter::DropEnd() { Interaction->EndDrop(); }
void APersonaCharacter::SlotOne() { Interaction->CancelActions(); Interaction->ServerSelectSlot(0); }
void APersonaCharacter::SlotTwo() { Interaction->CancelActions(); Interaction->ServerSelectSlot(1); }
void APersonaCharacter::SlotThree() { Interaction->CancelActions(); Interaction->ServerSelectSlot(2); }
void APersonaCharacter::Fire() { if(!IsAlive()) { if(auto PC=Cast<APersonaPlayerController>(Controller)) PC->CycleSpectator(); } else Weapon->Fire(); }
void APersonaCharacter::AimStart() { Weapon->SetAim(true); }
void APersonaCharacter::AimEnd() { Weapon->SetAim(false); }
void APersonaCharacter::ReloadStart() { Weapon->Load(true); }
void APersonaCharacter::ReloadEnd() { Weapon->Load(false); }
void APersonaCharacter::Pump() { Weapon->Pump(); }
void APersonaCharacter::Inspect() { Weapon->Inspect(); }
void APersonaCharacter::Safety() { Weapon->Safety(); }
void APersonaCharacter::UnloadStart() { Weapon->Unload(true); }
void APersonaCharacter::UnloadEnd() { Weapon->Unload(false); }
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
    auto DropAction=Action(TEXT("Drop"),EInputActionValueType::Boolean,EKeys::G);
    EI->BindAction(DropAction,ETriggerEvent::Started,this,&ThisClass::Drop); EI->BindAction(DropAction,ETriggerEvent::Completed,this,&ThisClass::DropEnd);
    EI->BindAction(Action(TEXT("Fire"),EInputActionValueType::Boolean,EKeys::LeftMouseButton),ETriggerEvent::Started,this,&ThisClass::Fire);
    auto AimAction=Action(TEXT("Aim"),EInputActionValueType::Boolean,EKeys::RightMouseButton);
    EI->BindAction(AimAction,ETriggerEvent::Started,this,&ThisClass::AimStart); EI->BindAction(AimAction,ETriggerEvent::Completed,this,&ThisClass::AimEnd);
    auto Reload=Action(TEXT("Reload"),EInputActionValueType::Boolean,EKeys::R);
    EI->BindAction(Reload,ETriggerEvent::Started,this,&ThisClass::ReloadStart); EI->BindAction(Reload,ETriggerEvent::Completed,this,&ThisClass::ReloadEnd);
    EI->BindAction(Action(TEXT("Pump"),EInputActionValueType::Boolean,EKeys::F),ETriggerEvent::Started,this,&ThisClass::Pump);
    EI->BindAction(Action(TEXT("Inspect"),EInputActionValueType::Boolean,EKeys::T),ETriggerEvent::Started,this,&ThisClass::Inspect);
    EI->BindAction(Action(TEXT("Safety"),EInputActionValueType::Boolean,EKeys::B),ETriggerEvent::Started,this,&ThisClass::Safety);
    auto UnloadAction=Action(TEXT("Unload"),EInputActionValueType::Boolean,EKeys::U);
    EI->BindAction(UnloadAction,ETriggerEvent::Started,this,&ThisClass::UnloadStart); EI->BindAction(UnloadAction,ETriggerEvent::Completed,this,&ThisClass::UnloadEnd);
    EI->BindAction(Action(TEXT("SlotOne"), EInputActionValueType::Boolean, EKeys::One), ETriggerEvent::Started, this, &ThisClass::SlotOne);
    EI->BindAction(Action(TEXT("SlotTwo"), EInputActionValueType::Boolean, EKeys::Two), ETriggerEvent::Started, this, &ThisClass::SlotTwo);
    EI->BindAction(Action(TEXT("SlotThree"), EInputActionValueType::Boolean, EKeys::Three), ETriggerEvent::Started, this, &ThisClass::SlotThree);
    auto Talk = Action(TEXT("Talk"), EInputActionValueType::Boolean, EKeys::V);
    EI->BindAction(Talk, ETriggerEvent::Started, this, &ThisClass::VoiceStart); EI->BindAction(Talk, ETriggerEvent::Completed, this, &ThisClass::VoiceEnd);
    Subsystem->AddMappingContext(Mapping, 0);
}
void APersonaCharacter::SetMaxHealth(float Value)
{ GetAbilitySystemComponent()->SetNumericAttributeBase(UPersonaAttributes::GetMaxHealthAttribute(),FMath::Max(1.f,Value)); }
FText APersonaCharacter::InteractionLabel_Implementation() const { return bDead ? FText::FromString(TEXT("E — осмотреть тело")) : FText::GetEmpty(); }
void APersonaCharacter::Interact_Implementation(APersonaCharacter* C) { if(HasAuthority() && bDead && C) C->Interaction->ClientOpenLoot(this); }
void APersonaCharacter::OnRep_Dead()
{
    if(!bDead)
    {
        GetMesh()->SetSimulatePhysics(false); GetMesh()->SetAllBodiesSimulatePhysics(false);
        GetMesh()->AttachToComponent(GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);
        GetMesh()->SetRelativeTransform(GetClass()->GetDefaultObject<APersonaCharacter>()->GetMesh()->GetRelativeTransform());
        GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly); GetMesh()->SetCollisionResponseToAllChannels(ECR_Ignore);
        GetMesh()->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Block);
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        OnRep_Transit(); return;
    }
    if(HasAuthority() || IsLocallyControlled()) Weapon->Cancel(); Interaction->bCharging=false; bSprinting=false;
    GetCharacterMovement()->StopMovementImmediately(); GetCharacterMovement()->DisableMovement();
    if(auto AI=Cast<APersonaAIController>(Controller)) { AI->StopMovement(); if(AI->BrainComponent) AI->BrainComponent->StopLogic(TEXT("Dead")); }
    FirstPersonArms->SetVisibility(false); GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetCollisionProfileName(TEXT("Ragdoll")); GetMesh()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    GetMesh()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block); GetMesh()->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Block);
    GetMesh()->SetSimulatePhysics(true); GetMesh()->WakeAllRigidBodies(); DeathAge=0;
    if(!CorpsePose.IsEmpty()) OnRep_Corpse();
}
void APersonaCharacter::OnRep_Transit()
{
    if(bTransit || bSpectator || bDead) { GetCharacterMovement()->StopMovementImmediately(); GetCharacterMovement()->DisableMovement(); }
    else GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    if(bSpectator) { GetMesh()->SetVisibility(false); GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
    else { GetMesh()->SetVisibility(true); if(!bDead) GetCapsuleComponent()->SetCollisionEnabled(bTransit ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics); }
}
void APersonaCharacter::HitReaction_Implementation(FName Bone,FVector_NetQuantizeNormal Direction,float Strength)
{
    if(bDead && !CorpsePose.IsEmpty()) return;
    if(!bDead)
    {
        FPhysicalAnimationData Data; Data.OrientationStrength=1200; Data.AngularVelocityStrength=120; Data.PositionStrength=600; Data.VelocityStrength=60;
        PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(TEXT("spine_01"),Data,true);
        GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        GetMesh()->SetAllBodiesBelowSimulatePhysics(TEXT("spine_01"),true,true); HitAge=.35f;
        GetMesh()->SetAllBodiesPhysicsBlendWeight(.45f);
    }
    GetMesh()->AddImpulse(Direction*Strength,Bone,true);
}
void APersonaCharacter::FreezeCorpse()
{
    if(!HasAuthority() || !bDead || !CorpsePose.IsEmpty()) return;
    CorpseMeshTransform=GetMesh()->GetComponentTransform(); CorpsePose=GetMesh()->GetBoneSpaceTransforms();
    const FVector BodyPosition=GetMesh()->GetBoneLocation(TEXT("pelvis"));
    SetActorLocation(BodyPosition); OnRep_Corpse(); ForceNetUpdate();
}
void APersonaCharacter::OnRep_Corpse()
{
    if(CorpsePose.IsEmpty()) return;
    GetMesh()->SetSimulatePhysics(false); GetMesh()->SetAllBodiesSimulatePhysics(false);
    GetMesh()->AttachToComponent(GetRootComponent(),FAttachmentTransformRules::KeepWorldTransform);
    GetMesh()->SetWorldTransform(CorpseMeshTransform); GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}
void APersonaCharacter::ReviveAt(const FVector& Location)
{
    bDead=bSpectator=bTransit=false; CorpsePose.Reset(); Loot->Items.Reset(); DeathAge=0;
    GetMesh()->SetSimulatePhysics(false); GetMesh()->SetAllBodiesSimulatePhysics(false);
    GetMesh()->AttachToComponent(GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);
    GetMesh()->SetRelativeLocationAndRotation(FVector(0,0,-90),FRotator(0,-90,0));
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    OnRep_Dead(); SetActorLocation(Location,false,nullptr,ETeleportType::TeleportPhysics); SetMaxHealth(100); SetVitals(100,100,100); ForceNetUpdate();
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
