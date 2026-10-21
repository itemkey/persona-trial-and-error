#include "PersonaInteraction.h"
#include "PersonaCharacter.h"
#include "PersonaFramework.h"
#include "Components/StaticMeshComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

UPersonaInteraction::UPersonaInteraction() { SetIsReplicatedByDefault(true); }
void UPersonaInteraction::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const { Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UPersonaInteraction, Held); }
AActor* UPersonaInteraction::TraceTarget() const
{
    auto C = Cast<APersonaCharacter>(GetOwner()); if (!C) return nullptr;
    FVector Start; FRotator Rotation; C->GetActorEyesViewPoint(Start, Rotation);
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(PersonaInteraction), false, C);
    if (Held) Query.AddIgnoredActor(Held);
    GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Rotation.Vector() * Reach, ECC_Visibility, Query);
    return Hit.GetActor();
}
void UPersonaInteraction::TryInteract() { if (Held) ServerRelease(false); else ServerInteract(TraceTarget()); }
void UPersonaInteraction::ServerInteract_Implementation(AActor* Target)
{
    auto C = Cast<APersonaCharacter>(GetOwner());
    if (!C || !C->IsAlive() || Held || !IsValid(Target) || Target != TraceTarget() || !Target->Implements<UPersonaInteractable>()) return;
    IPersonaInteractable::Execute_Interact(Target, C);
}
void UPersonaInteraction::ServerRelease_Implementation(bool Throw) { ReleaseHeld(Throw); }
void UPersonaInteraction::ReleaseHeld(bool Throw) { if (GetOwner()->HasAuthority() && Held) Held->Release(Throw); }
APersonaProp::APersonaProp()
{
    bReplicates = true; SetReplicateMovement(true); SetNetUpdateFrequency(30);
    PrimaryActorTick.bCanEverTick = true;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh")); SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    Mesh->SetStaticMesh(Cube.Object); Mesh->SetWorldScale3D(FVector(0.35f));
    Mesh->SetSimulatePhysics(true); Mesh->SetCollisionProfileName(TEXT("PersonaInteractable"));
    Mesh->SetIsReplicated(true); Mesh->SetRenderCustomDepth(true); Mesh->SetCustomDepthStencilValue(2);
}
void APersonaProp::Tick(float Delta)
{
    Super::Tick(Delta);
    if (HasAuthority() && Holder)
    {
        if (!Holder->IsAlive() || !Holder->GetController()) { Release(false); return; }
        FVector Start; FRotator Rotation; Holder->GetActorEyesViewPoint(Start, Rotation);
        const FVector Desired = Start + Rotation.Vector() * 130 - FVector(0, 0, 15);
        FHitResult Hit;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(Carry), false, Holder); Params.AddIgnoredActor(this);
        GetWorld()->SweepSingleByChannel(Hit, Start, Desired, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(22), Params);
        SetActorLocation(Hit.bBlockingHit ? Hit.Location : Desired, false, nullptr, ETeleportType::TeleportPhysics);
        SetActorRotation(FRotator(0, Rotation.Yaw, 0));
    }
}
FText APersonaProp::InteractionLabel_Implementation() const { return FText::FromString(Holder ? TEXT("In use") : TEXT("E - carry")); }
void APersonaProp::Interact_Implementation(APersonaCharacter* C)
{
    if (!HasAuthority() || Holder || !C || C->Interaction->Held) return;
    Holder = C; C->Interaction->Held = this; SetOwner(C); OnRep_Holder(); ForceNetUpdate();
}
void APersonaProp::OnRep_Holder()
{
    Mesh->SetSimulatePhysics(Holder == nullptr);
    Mesh->SetCollisionEnabled(Holder ? ECollisionEnabled::QueryOnly : ECollisionEnabled::QueryAndPhysics);
}
void APersonaProp::Release(bool Throw)
{
    if (!HasAuthority() || !Holder) return;
    APersonaCharacter* Previous = Holder; const FVector Direction = Previous->GetBaseAimRotation().Vector();
    Previous->Interaction->Held = nullptr; Holder = nullptr; SetOwner(nullptr); OnRep_Holder();
    if (Throw) Mesh->AddImpulse(Direction * ThrowImpulse, NAME_None, true);
    UAISense_Hearing::ReportNoiseEvent(this, GetActorLocation(), Throw ? 1.f : 0.3f, Previous, 2000, TEXT("Prop"));
    ForceNetUpdate();
}
void APersonaProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const { Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(APersonaProp, Holder); }
APersonaDoor::APersonaDoor()
{
    bReplicates = true;
    Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge")); SetRootComponent(Hinge);
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh")); Mesh->SetupAttachment(Hinge);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    Mesh->SetStaticMesh(Cube.Object); Mesh->SetRelativeLocation(FVector(0, 65, 120)); Mesh->SetRelativeScale3D(FVector(.18f, 1.3f, 2.4f));
    Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
}
FText APersonaDoor::InteractionLabel_Implementation() const { return FText::FromString(bOpen ? TEXT("E - close") : TEXT("E - open")); }
void APersonaDoor::Interact_Implementation(APersonaCharacter* C) { if (HasAuthority()) { bOpen = !bOpen; OnRep_Open(); ForceNetUpdate(); } }
void APersonaDoor::OnRep_Open()
{
    const FRotator Rotation(0, bOpen ? -95 : 0, 0);
    Mesh->SetRelativeRotation(Rotation);
    Mesh->SetRelativeLocation(Rotation.RotateVector(FVector(0,65,120)));
}
void APersonaDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const { Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(APersonaDoor, bOpen); }
APersonaTerminal::APersonaTerminal()
{
    bReplicates = true; Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh")); SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    Mesh->SetStaticMesh(Cube.Object); Mesh->SetWorldScale3D(FVector(.5f, .7f, 1.4f));
}
FText APersonaTerminal::InteractionLabel_Implementation() const { return FText::FromString(bGoToTrial ? TEXT("E - enter test chamber (host)") : TEXT("E - return to hub (host)")); }
void APersonaTerminal::Interact_Implementation(APersonaCharacter* C)
{
    if (auto PC = C ? Cast<APersonaPlayerController>(C->GetController()) : nullptr; PC && PC->IsHost())
        if (auto GM = GetWorld()->GetAuthGameMode<APersonaGameMode>()) GM->Travel(bGoToTrial);
}
