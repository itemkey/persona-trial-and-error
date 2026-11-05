#include "PersonaSurvival.h"
#include "PersonaBattle.h"
#include "PersonaAI.h"
#include "PersonaAttributes.h"
#include "AbilitySystemComponent.h"
#include "PersonaCharacter.h"
#include "PersonaFramework.h"
#include "PersonaHub.h"
#include "PersonaLoot.h"
#include "PersonaSave.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const FVector FaceNormals[] = {FVector::ForwardVector,-FVector::ForwardVector,FVector::RightVector,-FVector::RightVector,FVector::UpVector,-FVector::UpVector};
float ServerNow(const UWorld* World)
{
    return World->GetGameState() ? World->GetGameState()->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}
bool Playing(const APersonaTrialDirector& D, const APersonaCharacter* C)
{
    if(!C || !C->IsAlive() || C->bOffline || C->bTransit || D.Run.Phase!=EPersonaTrialPhase::Forest) return false;
    const auto P=D.Run.Forest.Player(C->PersistentId);
    return P && P->Outcome==EForestOutcome::Active;
}
void SaveRun(APersonaTrialDirector& D)
{
    D.ForceNetUpdate();
    D.GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->SaveWorld();
}
}

FVector PersonaForest::CarryCenter(const APersonaCharacter& C)
{
    return C.GetActorLocation()+C.GetActorForwardVector()*34+C.GetActorRightVector()*24;
}
bool PersonaForest::TouchesFace(const FVector& Offset,float CapsuleRadius,float HalfHeight)
{
    return FMath::Abs(Offset.X)+CapsuleRadius>=Radius || FMath::Abs(Offset.Y)+CapsuleRadius>=Radius || FMath::Abs(Offset.Z)+HalfHeight>=Radius;
}
FString PersonaForest::Duration(float Seconds)
{
    const int32 N=FMath::Max(0,FMath::FloorToInt(Seconds));
    return FString::Printf(TEXT("%02d:%02d"),N/60,N%60);
}

APersonaForestFlag::APersonaForestFlag()
{
    bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true); SetNetUpdateFrequency(30);
    PrimaryActorTick.bCanEverTick=true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Center")));
    Pole=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Standard")); Pole->SetupAttachment(GetRootComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> FlagAsset(TEXT("/Game/Persona/Survival/SM_Flag"));
    Pole->SetStaticMesh(FlagAsset.Object); Pole->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Pole->SetCollisionResponseToAllChannels(ECR_Ignore); Pole->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Pole->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));

    for(int32 I=0; I<6; ++I)
    {
        auto Face=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Face%d"),I));
        Face->SetupAttachment(GetRootComponent()); Face->SetStaticMesh(Plane.Object);
        Face->SetRelativeLocation(FaceNormals[I]*PersonaForest::Radius);
        Face->SetRelativeRotation(FRotationMatrix::MakeFromZ(-FaceNormals[I]).Rotator());
        Face->SetRelativeScale3D(FVector(PersonaForest::Radius*2/100));
        Face->SetCollisionEnabled(ECollisionEnabled::NoCollision); Face->SetCanEverAffectNavigation(false);
        Face->SetCastShadow(false); Faces.Add(Face);
    }
}
void APersonaForestFlag::BeginPlay()
{
    Super::BeginPlay();
    auto Grid=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Persona/North/M_Boundary.M_Boundary"));
    for(auto Face:Faces) Tints.Add(Face->CreateDynamicMaterialInstance(0,Grid));
}
void APersonaForestFlag::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(APersonaForestFlag,Bearer);
}
void APersonaForestFlag::Tick(float Delta)
{
    Super::Tick(Delta);
    auto PC=GetWorld()->GetFirstPlayerController(); if(!PC || !PC->IsLocalController() || !PC->GetViewTarget()) return;
    if(Bearer)
    {
        if(!HasAuthority() && Bearer->IsLocallyControlled()) SetActorLocation(PersonaForest::CarryCenter(*Bearer));
        Pole->SetRelativeLocationAndRotation(FVector(0,0,46),FRotator(0,Bearer->GetActorRotation().Yaw+90,0));
    }
    else Pole->SetRelativeLocationAndRotation(FVector::ZeroVector,FRotator::ZeroRotator);
    const FVector Offset=PC->GetViewTarget()->GetActorLocation()-GetActorLocation();
    const bool Inside=Offset.GetAbsMax()<PersonaForest::Radius+150;
    for(int32 I=0; I<Faces.Num(); ++I)
    {
        Faces[I]->SetVisibility(Inside);
        const float Gap=PersonaForest::Radius-FVector::DotProduct(Offset,FaceNormals[I]);
        if(Tints[I] && Inside) Tints[I]->SetScalarParameterValue(TEXT("Veil"),.075f+.58f*FMath::Clamp(1.f-Gap/420.f,0.f,1.f));
        if(Tints[I] && Inside) Tints[I]->SetVectorParameterValue(TEXT("Tint"),FMath::Lerp(FLinearColor(.13f,.27f,.28f),FLinearColor(1.8f,.18f,.04f),FMath::Clamp(1.f-Gap/450.f,0.f,1.f)));
    }
}
FText APersonaForestFlag::InteractionLabel_Implementation() const { return FText::FromString(TEXT("E — взять флаг   ·   G — поставить")); }
void APersonaForestFlag::Interact_Implementation(APersonaCharacter* C)
{
    if(HasAuthority()) if(auto D=APersonaTrialDirector::Get(GetWorld())) D->TakeFlag(C);
}
APersonaMedicalCase::APersonaMedicalCase()
{
    bReplicates=true; bAlwaysRelevant=true;
    auto Mesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Case")); SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CaseAsset(TEXT("/Game/Persona/Survival/SM_MedCase"));
    Mesh->SetStaticMesh(CaseAsset.Object); Mesh->SetCollisionProfileName(TEXT("BlockAll")); Mesh->SetCanEverAffectNavigation(false);
}
FText APersonaMedicalCase::InteractionLabel_Implementation() const { return FText::FromString(TEXT("E — взять полевую карту   ·   M — развернуть")); }
void APersonaMedicalCase::Interact_Implementation(APersonaCharacter* C)
{
    if(HasAuthority()) if(auto D=APersonaTrialDirector::Get(GetWorld())) D->TakeMap(C);
}
void APersonaTrialDirector::StartForest()
{
    auto& F=Run.Forest;
    F.Center=LiftBottom+FVector(0,490,90);
    F.CasePosition=LiftBottom+FVector(190,425,0);
    for(const auto& Id:Run.Participants) F.Players.AddDefaulted_GetRef().Id=Id;
    Run.Phase=EPersonaTrialPhase::Forest;
    RestoreForest();
}
void APersonaTrialDirector::RestoreForest()
{
    auto& F=Run.Forest;
    ForestClock=GetWorld()->GetTimeSeconds()-F.Elapsed; SurvivalStep=0;
    if(Platform) Platform->Mesh->SetCanEverAffectNavigation(false);
    if(!IsValid(ForestFlag)) ForestFlag=GetWorld()->SpawnActor<APersonaForestFlag>(F.Center,FRotator::ZeroRotator);
    F.CasePosition=LiftBottom+FVector(190,425,0);
    if(!IsValid(MedicalCase)) MedicalCase=GetWorld()->SpawnActor<APersonaMedicalCase>(F.CasePosition,FRotator::ZeroRotator);

    if(!F.Carrier.IsEmpty()) for(TActorIterator<APersonaCharacter> It(GetWorld()); It; ++It)
        if(It->PersistentId==F.Carrier) { if(ForestFlag) ForestFlag->Bearer=*It; It->Interaction->bCarryingFlag=true; It->Interaction->RefreshEquipped(); break; }
}
void APersonaTrialDirector::TakeFlag(APersonaCharacter* C)
{
    if(!HasAuthority() || !Playing(*this,C) || !Run.Forest.Carrier.IsEmpty() || !IsValid(ForestFlag) || !C->Interaction->CanReachLoot(ForestFlag)) return;
    ForestFlag->Bearer=C; ForestFlag->ForceNetUpdate();
    Run.Forest.LastGround=Run.Forest.Center; Run.Forest.Carrier=C->PersistentId; C->Interaction->CancelActions();
    C->Interaction->bCarryingFlag=true; C->Interaction->RefreshEquipped(); ForceNetUpdate();
}
void APersonaTrialDirector::LeaveFlag(APersonaCharacter* C)
{
    if(!HasAuthority() || !C || Run.Forest.Carrier!=C->PersistentId) return;
    if(ForestFlag) { ForestFlag->Bearer=nullptr; ForestFlag->ForceNetUpdate(); }
    Run.Forest.Carrier.Reset(); C->Interaction->bCarryingFlag=false;
    C->Interaction->RefreshEquipped();
    // Put the staff on the ground on ordinary drops. Retain its position over a chasm.
    FHitResult Ground; FCollisionQueryParams Q(SCENE_QUERY_STAT(ForestFlagDrop),false,C); Q.AddIgnoredActor(ForestFlag);
    const FVector At=Run.Forest.Center;
    if(GetWorld()->LineTraceSingleByChannel(Ground,At+FVector(0,0,30),At-FVector(0,0,250),ECC_WorldStatic,Q))
        Run.Forest.Center.Z=Ground.ImpactPoint.Z+90;
    if(IsValid(ForestFlag)) ForestFlag->SetActorLocation(Run.Forest.Center);
    ForceNetUpdate();
}
void APersonaTrialDirector::TakeMap(APersonaCharacter* C)
{
    if(!HasAuthority() || !Playing(*this,C) || !MedicalCase || !C->Interaction->CanReachLoot(MedicalCase)) return;
    auto P=Run.Forest.Player(C->PersistentId);
    if(!P->HasMap) { P->HasMap=true; SaveRun(*this); }
    C->Interaction->ClientFeedback(TEXT("Полевая карта — M. Лифт отмечен квадратом"));
}
void APersonaTrialDirector::EndForestPlayer(APersonaCharacter* C,FForestPlayer& P,EForestOutcome Outcome)
{
    if(P.Outcome!=EForestOutcome::Active) return;
    P.Outcome=Outcome; P.Seconds=GetWorld()->GetTimeSeconds()-ForestClock;
    if(Run.Forest.Carrier==P.Id) LeaveFlag(C);
    auto R=ForestResults.FindByPredicate([&](const auto& Entry){return Entry.Id==P.Id;});
    if(!R) { R=&ForestResults.AddDefaulted_GetRef(); R->Id=P.Id; }
    R->Outcome=Outcome; R->Last=P.Seconds; R->Best=FMath::Max(R->Best,P.Seconds);
    P.HasMap=false; C->bFieldMapOpen=false;
    C->Interaction->ClientFeedback(FString::Printf(TEXT("%s  ·  %s  ·  Рекорд %s"),Outcome==EForestOutcome::Escaped ? TEXT("Возвращение") : TEXT("Гибель"),*PersonaForest::Duration(P.Seconds),*PersonaForest::Duration(R->Best)));
}
void APersonaTrialDirector::UseRation(APersonaCharacter* C)
{
    if(HasAuthority() && C) C->Interaction->UseConsumable(C->Interaction->Held);
}
void APersonaTrialDirector::SpawnRaid()
{
    auto GM=GetWorld()->GetAuthGameMode<APersonaGameMode>();
    auto Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!GM || !GM->NPCClass || !Nav) return;
    TArray<APersonaCharacter*> Targets;
    int32 Enemies=0;
    for(TActorIterator<APersonaCharacter> It(GetWorld()); It; ++It)
    {
        if(It->bNPC && It->PersistentId.StartsWith(Run.Id) && It->IsAlive()) ++Enemies;
        if(Playing(*this,*It)) Targets.Add(*It);
    }
    if(Targets.IsEmpty()) return;
    auto& F=Run.Forest;
    const int32 Cap=Run.DifficultyPlayers+FMath::Clamp(FMath::FloorToInt(F.Elapsed/360.f),1,5);
    if(Enemies>=Cap) return;
    FRandomStream Random(Run.Seed+Run.SpawnedNPC+FMath::FloorToInt(F.Elapsed));
    auto Target=Targets[Random.RandRange(0,Targets.Num()-1)];
    for(int32 I=0; I<24; ++I)
    {
        const float Angle=Random.FRandRange(0,2*PI);
        const FVector Candidate=F.Center+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Random.FRandRange(1350,1950);
        FNavLocation Floor;
        if(!Nav->ProjectPointToNavigation(Candidate,Floor,FVector(110,110,700))) continue;
        const FVector Feet=Floor.Location;
        bool Seen=false;
        for(auto C:Targets)
        {
            const FVector Delta=Feet+FVector(0,0,120)-C->GetPawnViewLocation();
            if(Delta.SizeSquared()<FMath::Square(750.f)) { Seen=true; break; }
            if(FVector::DotProduct(Delta.GetSafeNormal(),C->GetBaseAimRotation().Vector())>.38f
                && !GetWorld()->LineTraceTestByChannel(C->GetPawnViewLocation(),Feet+FVector(0,0,120),ECC_Visibility,FCollisionQueryParams(SCENE_QUERY_STAT(ForestRaid),false,C)))
            { Seen=true; break; }
        }
        if(Seen) continue;
        FNavLocation Goal;
        if(!Nav->ProjectPointToNavigation(Target->GetNavAgentLocation(),Goal,FVector(450,450,260))) continue;
        const auto Route=Nav->FindPathToLocationSynchronously(this,Feet,Goal.Location,Target);
        if(!Route || !Route->IsValid() || Route->IsPartial()) continue;
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
        auto Enemy=GetWorld()->SpawnActor<APersonaCharacter>(GM->NPCClass,Feet+FVector(0,0,95),FRotator::ZeroRotator,Params);
        if(!Enemy) continue;
        Enemy->PersistentId=Run.Id+FString::Printf(TEXT("_Forest_%d"),Run.SpawnedNPC++);
        if(auto AI=Cast<APersonaAIController>(Enemy->GetController())) { AI->SetTacticalSeed(Random.GetCurrentSeed()); AI->AlertAt(Target->GetActorLocation()); }
        F.NextRaid=F.Elapsed+FMath::Max(18.f,95.f-F.Elapsed/22.f)/FMath::Sqrt(float(Run.DifficultyPlayers));
        return;
    }
}

void APersonaTrialDirector::UpdateForest(float Delta)
{
    auto& F=Run.Forest;
    const float Now=GetWorld()->GetTimeSeconds();
    SurvivalStep+=Delta; SurveyStep+=Delta;
    const bool Survey=SurveyStep>=.16f && F.Players.ContainsByPredicate([](const auto& V){return V.HasMap;});
    if(SurveyStep>=.16f) SurveyStep=0;
    const float Step=SurvivalStep>=1.f ? SurvivalStep : 0.f;
    if(Step>0) { SurvivalStep=0; F.Elapsed=Now-ForestClock; }
    TArray<APersonaCharacter*,TInlineAllocator<4>> Subjects;
    for(TActorIterator<APersonaCharacter> It(GetWorld()); It; ++It)
    {
        if(It->bNPC || !F.Player(It->PersistentId)) continue;
        Subjects.Add(*It);
        if(!F.Carrier.IsEmpty() && F.Carrier==It->PersistentId)
        {
            if(!It->IsAlive() || (It->bOffline && !It->bRestoredBody)) LeaveFlag(*It);
            else if(!It->bOffline)
            {
                // Recover the staff at the bank if its carrier falls below the playable landscape.
                if(It->GetCharacterMovement()->IsMovingOnGround()) F.LastGround=It->GetActorLocation();
                if(It->GetActorLocation().Z<LiftBottom.Z-4200 && F.LastGround!=FVector::ZeroVector)
                { F.Center=F.LastGround; LeaveFlag(*It); }
                else
                {
                    F.Center=PersonaForest::CarryCenter(**It);
                    if(ForestFlag) ForestFlag->SetActorLocation(F.Center);
                }
            }
        }
    }
    bool Changed=false;
    for(auto C:Subjects)
    {
        auto P=F.Player(C->PersistentId); if(P->Outcome!=EForestOutcome::Active || C->bTransit) continue;
        const auto Capsule=C->GetCapsuleComponent();
        bool Lethal=PersonaForest::TouchesFace(C->GetActorLocation()-F.Center,Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight());
        if(Step>0 && C->IsAlive())
        {
            const float Food=FMath::Max(0.f,C->GetVitals()->GetHunger()-Step*100.f/1920.f);
            C->GetAbilitySystemComponent()->SetNumericAttributeBase(UPersonaAttributes::GetHungerAttribute(),Food);
            Lethal|=Food<=0;
        }
        if(Survey && C->IsAlive() && !C->bOffline)
        {
            ENorthSite Landmark=ENorthSite::None;
            ENorthSite Kind;
            const FVector Site=PersonaNorth::Site(Run.Seed,PersonaNorth::TileAt(C->GetActorLocation()),Kind);
            if(F.Generation>0 && FVector::DistSquared2D(Site,C->GetActorLocation())<FMath::Square(430.f)) Landmark=Kind;
            NorthAtlas.Visit(C->GetActorLocation(),Landmark);
        }
        if(Lethal && C->IsAlive()) UGameplayStatics::ApplyDamage(C,C->GetVitals()->GetMaxHealth()+100.f,nullptr,this,UDamageType::StaticClass());
        if(!C->IsAlive()) { EndForestPlayer(C,*P,EForestOutcome::Killed); Changed=true; }
    }
    if(!F.Players.ContainsByPredicate([](const auto& P){return P.Outcome==EForestOutcome::Active;}))
    { if(Run.Lift!=EPersonaLiftPhase::Ascending && Run.Lift!=EPersonaLiftPhase::Descending) FinishRun(true); return; }
    if(Changed) SaveRun(*this);
    if(Step<=0) return;
    if(NorthWorld) NorthWorld->RefreshNodes(*this);
    if(F.Elapsed>=F.NextRaid) { F.NextRaid=F.Elapsed+5; SpawnRaid(); }
    // Retire distant enemies and old bodies on long expeditions.
    for(TActorIterator<APersonaCharacter> It(GetWorld()); It; ++It)
        if(It->bNPC && It->PersistentId.StartsWith(Run.Id) && (FVector::DistSquared2D(It->GetActorLocation(),F.Center)>FMath::Square(5500.f) || (It->bDead && Now-It->DeathState.Start>180.f))) It->Destroy();
}

FString APersonaTrialDirector::ForestStatus(APersonaCharacter* C) const
{
    const auto P=C ? Run.Forest.Player(C->PersistentId) : nullptr;
    if(!P) return FString();
    if(P->Outcome!=EForestOutcome::Active)
        return FString::Printf(TEXT("%s  ·  %s"),P->Outcome==EForestOutcome::Escaped ? TEXT("Вы вернулись в хаб") : TEXT("Вы погибли"),*PersonaForest::Duration(P->Seconds));
    return FString::Printf(TEXT("%s   ·   %s%s"),*PersonaForest::Duration(Run.Forest.Elapsed),
        P->HasMap ? TEXT("M — карта  ·  Выход на лифте") : TEXT("Карта в кейсе у лифта"),
        Run.Forest.Carrier==P->Id ? TEXT("   ·   G — поставить флаг") : TEXT(""));
}


void APersonaTrialDirector::SyncNorth()
{
    if(Run.TrialId!=PersonaForest::TrialId || Run.Forest.Generation!=1 || !Running() || !ArenaReady()) return;
    if(!IsValid(NorthWorld)) NorthWorld=GetWorld()->SpawnActor<APersonaNorthernForest>();
    const FVector Center=Run.Forest.Center.IsNearlyZero() ? LiftBottom : Run.Forest.Center;
    if(NorthWorld) NorthWorld->Sync(*this,Center);
}
void APersonaTrialDirector::UnloadForestCargo()
{
    int32 N=0;
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It)
        if(Run.Passengers.ContainsByPredicate([&](const auto& R){return R.Id==It->PersistentId;}))
        {
            const FVector At=HubSpawn+FVector(0,110*N++,0);
            It->ApplyLiftPosition(At,false);
            if(It->IsAlive()) It->ClientReviveAt(At);
            It->ForceNetUpdate();
        }
    int32 Index=0;
    for(TActorIterator<APersonaProp> It(GetWorld());It;++It)
        if(Run.Cargo.ContainsByPredicate([&](const auto& R){return R.Id==It->PersistentItemId;}))
        {
            const FVector At=HubSpawn+FVector(225+45*(Index%5),45*((Index/5)%5),45+35*(Index/25)); ++Index;
            It->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics); It->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
            It->Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector); It->EnsureSafePlacement();
            // Unloading is an authoritative teleport, not a swept physics step
            // from the lift through the shaft walls. Start the guard at the new pose.
            It->OnRep_Holder(); It->ForceNetUpdate();
        }
    Run.Passengers.Reset(); Run.Cargo.Reset();
}
void APersonaTrialDirector::UpdateCabin(const FVector& Position,bool Moving)
{
    const bool Forest=Run.TrialId==PersonaForest::TrialId;
    const bool Battle=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->ClosingCabin();
    bool Closed=(Moving && Forest) || Battle;
    if(Forest && Run.Lift==EPersonaLiftPhase::Arena && Run.Phase==EPersonaTrialPhase::Forest)
    {
        if(!LocalFlag.IsValid()) for(TActorIterator<APersonaForestFlag> It(GetWorld());It;++It) { LocalFlag=*It; break; }
        Closed=!LocalFlag.IsValid() || (Run.Forest.Generation>0 && !NorthWorld);
    }
    if(!Forest && !Battle && Cabin.IsEmpty()) return;
    if(Cabin.IsEmpty())
    {
        auto Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        auto Steel=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Persona/North/M_rail.M_rail"));
        auto Part=[&](USceneComponent* Parent,FVector Offset,FVector Dimensions)
        {
            auto M=NewObject<UStaticMeshComponent>(this);M->SetupAttachment(Parent);
            M->SetStaticMesh(Cube);M->SetMaterial(0,Steel);M->SetRelativeLocation(Offset);M->SetRelativeScale3D(Dimensions/100.);
            M->SetCollisionProfileName(TEXT("BlockAll"));M->SetCanEverAffectNavigation(false);M->SetCastShadow(false);
            M->RegisterComponent();return M;
        };
        auto Base=Platform->GetRootComponent();
        // Four retracting shutters. No permanent cabin walls are added to the hub.
        for(int S:{-1,1}) Cabin.Add(Part(Base,FVector(S*325,0,145),FVector(14,665,330)));
        for(int S:{-1,1}) Cabin.Add(Part(Base,FVector(0,S*325,145),FVector(665,14,330)));
        Cabin.Add(Part(Base,FVector(0,0,318),FVector(665,665,14)));
        const float Top=LiftTop.Z-8,Bottom=LiftBottom.Z+330,Depth=Top-Bottom;
        const FVector Mid=FVector(LiftTop.X,LiftTop.Y,(Top+Bottom)*.5f);
        for(int S:{-1,1})
        {
            Shaft.Add(Part(GetRootComponent(),GetActorTransform().InverseTransformPosition(Mid+FVector(365*S,0,0)),FVector(20,752,Depth)));
            Shaft.Add(Part(GetRootComponent(),GetActorTransform().InverseTransformPosition(Mid+FVector(0,365*S,0)),FVector(752,20,Depth)));
        }
    }
    DoorOpening=FMath::FInterpConstantTo(DoorOpening,Closed ? 0.f : 1.f,GetWorld()->GetDeltaSeconds(),1.f/.6f);
    for(auto M:Shaft) { M->SetVisibility(Forest && Running()); M->SetCollisionEnabled(Forest && Running() ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision); }
    for(int I=0;I<Cabin.Num();++I)
    {
        auto M=Cabin[I];const bool Visible=DoorOpening<.999f;
        M->SetVisibility(Visible);M->SetCollisionEnabled(Visible ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        if(I<4)
        {
            const float Height=330*(1-DoorOpening);auto P=M->GetRelativeLocation();P.Z=310-Height*.5f;M->SetRelativeLocation(P);
            auto Scale=M->GetRelativeScale3D();Scale.Z=FMath::Max(.001f,Height/100);M->SetRelativeScale3D(Scale);
        }
    }
}
