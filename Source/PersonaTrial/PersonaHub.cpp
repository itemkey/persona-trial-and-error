#include "PersonaHub.h"
#include "PersonaFramework.h"
#include "PersonaBattle.h"
#include "PersonaCharacter.h"
#include "PersonaWeapon.h"
#include "PersonaLoot.h"
#include "PersonaAI.h"
#include "PersonaSave.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/ExponentialHeightFog.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"

APersonaHubObject::APersonaHubObject()
{
    bReplicates=true; PrimaryActorTick.bCanEverTick=true; SetReplicateMovement(true);
    Mesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh")); SetRootComponent(Mesh); Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    Loot=CreateDefaultSubobject<UPersonaLoot>(TEXT("Loot"));
    Audio=CreateDefaultSubobject<UAudioComponent>(TEXT("Audio")); Audio->SetupAttachment(Mesh); Audio->bAutoActivate=false;
    Light=CreateDefaultSubobject<UPointLightComponent>(TEXT("Light")); Light->SetupAttachment(Mesh); Light->SetIntensity(0); Light->SetCastShadows(false);
    Display=CreateDefaultSubobject<UTextRenderComponent>(TEXT("Display")); Display->SetupAttachment(Mesh); Display->SetText(FText::GetEmpty()); Display->SetWorldSize(45); Display->SetHorizontalAlignment(EHTA_Center);
    Display->SetTextRenderColor(FColor(12,12,12));
    Water=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Water")); Water->SetupAttachment(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
    Water->SetStaticMesh(Cylinder.Object); Water->SetRelativeLocation(FVector(33,-10,101)); Water->SetRelativeScale3D(FVector(.012,.012,.45));
    Water->SetCollisionEnabled(ECollisionEnabled::NoCollision); Water->SetCastShadow(false); Water->SetVisibility(false);
    Audio->bOverrideAttenuation=true; Audio->AttenuationOverrides.bAttenuate=true; Audio->AttenuationOverrides.bSpatialize=true;
    Audio->AttenuationOverrides.AttenuationShapeExtents=FVector(100); Audio->AttenuationOverrides.FalloffDistance=650;
}
APersonaPlayerBarrier::APersonaPlayerBarrier()
{
    Barrier=CreateDefaultSubobject<UBoxComponent>(TEXT("Barrier")); SetRootComponent(Barrier); Barrier->SetBoxExtent(FVector(900,20,550));
    Barrier->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Barrier->SetCollisionResponseToAllChannels(ECR_Ignore);
    Barrier->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Block); Barrier->SetCanEverAffectNavigation(false);
}
void APersonaHubObject::Configure(UPersonaHubAsset* Asset)
{
    Definition=Asset; if(!Definition) return; Mesh->SetStaticMesh(Definition->Mesh);
    // The director owns the lift transform on every peer, including idle endpoints.
    if(Definition->Kind==EPersonaHubKind::Lift) SetReplicateMovement(false);
    if(Definition->Kind==EPersonaHubKind::Screen)
    {
        // Explicit UE pitch/yaw/roll avoids the Python native-make argument order.
        Display->SetRelativeLocation(bTrialObject ? FVector(0,-12,0) : FVector(-12,0,0));
        Display->SetRelativeRotation(FRotator(0,bTrialObject ? -90.f : 180.f,0));
        Display->SetVerticalAlignment(EVRTA_TextCenter);
        Display->SetWorldSize(360);
    }
    for(auto P:MovingParts) if(P) P->DestroyComponent(); MovingParts.Reset();
    for(int32 I=0; I<Definition->Parts.Num(); ++I)
    {
        const auto& D=Definition->Parts[I]; auto P=NewObject<UStaticMeshComponent>(this,MakeUniqueObjectName(this,UStaticMeshComponent::StaticClass(),TEXT("MovingPart")));
        P->SetupAttachment(Mesh); P->SetStaticMesh(D.Mesh); P->SetRelativeLocation(D.Offset); P->SetCollisionProfileName(TEXT("BlockAll"));
        // The director opens the gate before releasing a wave. Keep its sliding
        // leaves out of Recast so their closed pose cannot sever the exit path.
        // Physical collision still prevents passage until the leaves separate.
        if(Definition->Kind==EPersonaHubKind::Gate) P->SetCanEverAffectNavigation(false);
        P->RegisterComponent(); MovingParts.Add(P);
    }
    PartAlphas.SetNumZeroed(MovingParts.Num()); InitializeCompartments();
}
void APersonaHubObject::BeginPlay() { Super::BeginPlay(); Configure(Definition); if(PersistentId.IsEmpty()) PersistentId=GetName(); }
void APersonaHubObject::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(APersonaHubObject,bOpen); DOREPLIFETIME(APersonaHubObject,bPowered); DOREPLIFETIME(APersonaHubObject,Message); DOREPLIFETIME(APersonaHubObject,Definition); DOREPLIFETIME(APersonaHubObject,PersistentId); }
void APersonaHubObject::Tick(float Delta)
{
    Super::Tick(Delta); if(!Definition) return;
    OpenAlpha=FMath::FInterpConstantTo(OpenAlpha,bOpen ? 1.f : 0.f,Delta,Definition->Kind==EPersonaHubKind::Gate ? .5f : 1.8f);
    if(HasAuthority() && HasCompartments())
    {
        for(auto& Pair:Viewers)
        {
            const int32 Before=Pair.Value.Num();
            Pair.Value.RemoveAll([&](const auto& Viewer){auto C=Viewer.Get(); return !C || !C->IsAlive() || C->bOffline || !C->Controller || !C->Interaction->CanReachLoot(this);});
            if(Before>0 && Pair.Value.IsEmpty()) if(auto Part=Loot->FindPart(Pair.Key)) { Part->Open=false; ForceNetUpdate(); }
        }
        bOpen=Loot->Compartments.ContainsByPredicate([](const auto& Part){return Part.Open;});
    }
    for(int32 I=0; I<MovingParts.Num(); ++I)
    {
        const auto& D=Definition->Parts[I];
        auto Part=Loot->FindPart(PartId(I));
        const bool Open=HasCompartments() ? Part && Part->Open : bOpen;
        PartAlphas[I]=FMath::FInterpConstantTo(PartAlphas[I],Open ? 1.f : 0.f,Delta,Definition->Kind==EPersonaHubKind::Gate ? .5f : 1.8f);
        const float Eased=FMath::SmoothStep(0.f,1.f,PartAlphas[I]);
        MovingParts[I]->SetRelativeLocationAndRotation(D.Offset+D.OpenOffset*Eased,FQuat::Slerp(D.ClosedRotation.Quaternion(),D.OpenRotation.Quaternion(),Eased));
    }
    if(Definition->Kind==EPersonaHubKind::Screen) if(auto D=APersonaTrialDirector::Get(GetWorld()))
    {
        const FString Text=D->ScreenText();
        if(Display->Text.ToString()!=Text)
        {
            Display->SetText(FText::FromString(Text)); Display->SetWorldSize(360);
            const FVector Size=Display->GetTextLocalSize();
            const float Fit=FMath::Min(1.f,FMath::Min(4400.f/FMath::Max(1.f,static_cast<float>(Size.Y)),2200.f/FMath::Max(1.f,static_cast<float>(Size.Z))));
            Display->SetWorldSize(360*Fit);
        }
    }
    Light->SetIntensity(bPowered ? 180.f : 0.f);
    Water->SetVisibility(bPowered && Definition->Kind==EPersonaHubKind::Sink);
    if(bPowered && !Audio->IsPlaying()) { if(!Audio->Sound) Audio->SetSound(LoadObject<USoundBase>(nullptr,Definition->Kind==EPersonaHubKind::Sink ? TEXT("/Game/Persona/Audio/Water.Water") : TEXT("/Game/Persona/Audio/Motor.Motor"))); if(Audio->Sound) Audio->Play(); }
    if(!bPowered && Audio->IsPlaying()) Audio->Stop();
}
FText APersonaHubObject::InteractionLabel_Implementation() const
{ return Definition ? FText::FromString(TEXT("E — ")+Definition->Label.ToString()) : FText::GetEmpty(); }
void APersonaHubObject::Interact_Implementation(APersonaCharacter* C)
{
    if(!HasAuthority() || !C || !C->IsAlive() || !Definition) return;
    switch(Definition->Kind)
    {
    case EPersonaHubKind::Storage: break; // Storage requires a verified part hit.
    case EPersonaHubKind::Terminal: if(auto PC=Cast<APersonaPlayerController>(C->Controller)) PC->ClientTrialTerminal(); break;
    case EPersonaHubKind::Lift: if(!GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->UseLift(C)) if(auto D=APersonaTrialDirector::Get(GetWorld())) D->UseLift(C); break;
    case EPersonaHubKind::Workbench: C->Weapon->Inspect(); break;
    case EPersonaHubKind::Vice: bOpen=!bOpen; break;
    case EPersonaHubKind::Stove: case EPersonaHubKind::Sink:
    {
        FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(Stove),false,C);
        GetWorld()->LineTraceSingleByChannel(Hit,C->GetPawnViewLocation(),C->GetPawnViewLocation()+C->GetBaseAimRotation().Vector()*300,ECC_Visibility,Q);
        if(MovingParts.Contains(Cast<UStaticMeshComponent>(Hit.GetComponent())))
        { if(Definition->Kind==EPersonaHubKind::Sink) InteractPart(C,HitPart(Hit.GetComponent())); else bOpen=!bOpen; }
        else if(Definition->Kind==EPersonaHubKind::Sink && bPowered && C->Water<99.f)
        { C->Water=100; C->ForceNetUpdate(); C->Interaction->ClientFeedback(TEXT("Вы напились")); }
        else bPowered=!bPowered; break;
    }
    case EPersonaHubKind::Switch: bPowered=!bPowered; break;
    default: break;
    }
    ForceNetUpdate();
}
FName APersonaHubObject::PartId(int32 Index) const
{
    if(!Definition || !Definition->Parts.IsValidIndex(Index)) return NAME_None;
    const auto& Part=Definition->Parts[Index];
    return !Part.Id.IsNone() ? Part.Id : Part.Mesh ? Part.Mesh->GetFName() : NAME_None;
}
FName APersonaHubObject::HitPart(const UPrimitiveComponent* Component) const
{
    for(int32 I=0;I<MovingParts.Num();++I) if(MovingParts[I]==Component) return PartId(I);
    // A single-lid chest has one unambiguous destination. Multi-part bodies
    // deliberately resolve to no compartment.
    return Component==Mesh && HasCompartments() && MovingParts.Num()==1 ? PartId(0) : NAME_None;
}
bool APersonaHubObject::HasCompartments() const
{ return Definition && (Definition->Kind==EPersonaHubKind::Storage || Definition->Kind==EPersonaHubKind::Sink); }
void APersonaHubObject::InitializeCompartments()
{
    if(!HasAuthority() || !HasCompartments()) return;
    for(int32 I=0;I<Definition->Parts.Num();++I)
    {
        const FName Id=PartId(I); if(Id.IsNone() || Loot->FindPart(Id)) continue;
        FPersonaCompartment Part; Part.Id=Id; Part.Capacity=Loot->Capacity;
        Part.Label=Definition->Parts[I].Label.IsEmpty() ? FString::Printf(TEXT("%s — отделение %d"),*Definition->Label.ToString(),I+1) : Definition->Parts[I].Label;
        Loot->Compartments.Add(Part);
    }
    if(Loot->HasSharedInventory())
    {
        // Merge old per-door supplies exactly once, preserving item identity and
        // weapon state. Doors keep their independent open/viewer state.
        TSet<FString> Seen; for(const auto& Item:Loot->Items) if(!Item.Id.IsEmpty()) Seen.Add(Item.Id);
        for(auto& Part:Loot->Compartments)
        {
            for(const auto& Item:Part.Items)
                if(Item.Id.IsEmpty() || !Seen.Contains(Item.Id)) { Loot->Items.Add(Item); Seen.Add(Item.Id); }
            Part.Items.Reset();
        }
        ForceNetUpdate(); return;
    }
    // Old tagged saves only contain Items. Move records, never respawn them.
    if(!Loot->Compartments.IsEmpty() && !Loot->Items.IsEmpty())
    { Loot->Compartments[0].Items.Append(MoveTemp(Loot->Items)); Loot->Items.Reset(); }
}
void APersonaHubObject::InteractPart(APersonaCharacter* C,FName Id)
{
    auto Part=Loot->FindPart(Id); if(!HasAuthority() || !C || !C->IsAlive() || !Part) return;
    if(auto Previous=Cast<APersonaHubObject>(C->Interaction->ServerLootTarget.Get())) Previous->ReleaseViewer(C,C->Interaction->ServerLootPart);
    Viewers.FindOrAdd(Id).AddUnique(C); Part->Open=true; bOpen=true;
    C->Interaction->ServerLootTarget=this; C->Interaction->ServerLootPart=Id;
    C->Interaction->ClientOpenLoot(this,Id); ForceNetUpdate();
}
void APersonaHubObject::ReleaseViewer(APersonaCharacter* C,FName Id)
{
    if(!HasAuthority()) return;
    auto& Users=Viewers.FindOrAdd(Id); Users.Remove(C);
    Users.RemoveAll([](const auto& User){return !User.IsValid();});
    if(Users.IsEmpty()) if(auto Part=Loot->FindPart(Id)) Part->Open=false;
    if(C && C->Interaction->ServerLootTarget==this && C->Interaction->ServerLootPart==Id)
    { C->Interaction->ServerLootTarget.Reset(); C->Interaction->ServerLootPart=NAME_None; }
    bOpen=Loot->Compartments.ContainsByPredicate([](const auto& Part){return Part.Open;}); ForceNetUpdate();
}
bool APersonaHubObject::HasViewer(const APersonaCharacter* C,FName Id) const
{ const auto* Users=Viewers.Find(Id); return Users && Users->ContainsByPredicate([&](const auto& User){return User.Get()==C;}); }
APersonaTrialDirector::APersonaTrialDirector()
{
    bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true; SetNetUpdateFrequency(10);
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    PurgeFog=CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("PurgeFog")); PurgeFog->SetupAttachment(GetRootComponent());
    PurgeFog->SetRelativeLocation(FVector(0,0,-12100)); PurgeFog->SetFogDensity(0); PurgeFog->SetFogHeightFalloff(2.f);
    // Match the fixed -7.5 EV scene exposure. A display-referred 0.12 here
    // becomes almost black after exposure and cannot read as filling smoke.
    PurgeFog->SetFogInscatteringColor(FLinearColor(22.f,22.f,22.f)); PurgeFog->SetFogMaxOpacity(.995f);
    PurgeFog->SetVisibility(false);
}
APersonaTrialDirector* APersonaTrialDirector::Get(UWorld* World) { for(TActorIterator<APersonaTrialDirector> It(World);It;++It) return *It; return nullptr; }
void APersonaTrialDirector::BeginPlay()
{
    Super::BeginPlay();
    if(HasAuthority()) ClockEpoch=FDateTime::UtcNow().ToUnixTimestamp()-int64(GetWorld()->GetTimeSeconds());
    if(Trials.IsEmpty() && Definition) Trials.Add(Definition);
    if(auto Forest=LoadObject<UPersonaTrialDefinition>(nullptr,TEXT("/Game/Persona/Trials/DA_SurvivalAgility.DA_SurvivalAgility"))) Trials.AddUnique(Forest);
    if(HasAuthority()) if(auto S=GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>(); S && S->Active) Restore(S->Active->Run);
}
void APersonaTrialDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(APersonaTrialDirector,Run); DOREPLIFETIME(APersonaTrialDirector,Definition); DOREPLIFETIME(APersonaTrialDirector,Trials); DOREPLIFETIME(APersonaTrialDirector,ForestResults); DOREPLIFETIME(APersonaTrialDirector,NorthAtlas); DOREPLIFETIME(APersonaTrialDirector,ForestAvailableAt); DOREPLIFETIME(APersonaTrialDirector,ClockEpoch); DOREPLIFETIME(APersonaTrialDirector,RemainingNPC); DOREPLIFETIME(APersonaTrialDirector,PhaseEnd); DOREPLIFETIME(APersonaTrialDirector,LiftStart); }
bool APersonaTrialDirector::InRun(APersonaCharacter* C) const { return C && Run.Participants.Contains(C->PersistentId); }
bool APersonaTrialDirector::ArenaReady() const { return Arena && Arena->IsLevelLoaded() && Arena->IsLevelVisible(); }
void APersonaTrialDirector::EnsureArena()
{
    if(!Running() || (!LoadedRun.IsEmpty() && LoadedRun!=Run.Id))
    {
        if(Arena) { Arena->SetShouldBeLoaded(false); Arena->SetShouldBeVisible(false); Arena=nullptr; }
        if(IsValid(NorthWorld)) NorthWorld->Destroy();
        NorthWorld=nullptr; bLevelRequested=false; LoadedRun.Reset();
        if(!Running()) return;
    }
    LoadedRun=Run.Id;
    if(!Definition || Definition->TrialId!=Run.TrialId) return;
    if(!Arena && Definition)
    {
        const auto Map=Run.TrialId==PersonaForest::TrialId && Run.Forest.Generation>0 ?
            TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Persona/Maps/L_NorthernForest.L_NorthernForest"))) : Definition->Map;
        // Reuse the authored sublevel so baked interactions include the hub.
        for(auto Level:GetWorld()->GetStreamingLevels()) if(Level && (Level->GetWorldAssetPackageName()==Map.GetLongPackageName() || Level->PackageNameToLoad==Map.GetLongPackageFName())) { Arena=Level; break; }
        if(!Arena) { bool Success=false; Arena=ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(this,Map,FVector::ZeroVector,FRotator::ZeroRotator,Success,Map.GetAssetName()+TEXT("_Instance")); }
        if(Arena) { Arena->SetShouldBeLoaded(true); Arena->SetShouldBeVisible(true); }
    }
    SyncNorth();
    if(ArenaReady() && !bLevelRequested)
    {
        bLevelRequested=true;
        if(auto PC=Cast<APersonaPlayerController>(UGameplayStatics::GetPlayerController(this,0))) PC->ServerArenaReady(true);
    }
}
UPersonaTrialDefinition* APersonaTrialDirector::FindTrial(FName Id) const
{
    for(auto Trial:Trials) if(Trial && Trial->TrialId==Id) return Trial;
    return Definition && Definition->TrialId==Id ? Definition.Get() : nullptr;
}
int32 APersonaTrialDirector::SupplyPlayers() const
{
    if(Running() && Run.Lift!=EPersonaLiftPhase::Hub) return Run.DifficultyPlayers;
    int32 Count=0;
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It)
        if(!It->bNPC && !It->bOffline && It->IsAlive()) ++Count;
    return FMath::Clamp(Count,1,4);
}
int64 APersonaTrialDirector::ForestWait() const
{
    const auto GS=GetWorld()->GetGameState();
    const int64 Now=HasAuthority() || !ClockEpoch || !GS ? FDateTime::UtcNow().ToUnixTimestamp() : ClockEpoch+int64(GS->GetServerWorldTimeSeconds());
    return FMath::Max<int64>(0,ForestAvailableAt-Now);
}
FString APersonaTrialDirector::TrialDetails(FName Id) const
{
    const auto Trial=FindTrial(Id); if(!Trial) return TEXT("Испытание недоступно");
    if(Id==PersonaForest::TrialId)
    {
        auto C=Cast<APersonaCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
        FString Result;
        if(C) if(auto R=ForestResults.FindByPredicate([&](const auto& V){ return V.Id==C->PersistentId; }))
            Result=FString::Printf(TEXT("\n\nПоследний забег — %s (%s). Личный рекорд — %s."),*PersonaForest::Duration(R->Last),R->Outcome==EForestOutcome::Escaped ? TEXT("выход") : TEXT("гибель"),*PersonaForest::Duration(R->Best));
        const int64 Wait=ForestWait();
        if(Wait) Result+=FString::Printf(TEXT("\nНовая вылазка через %s"),*PersonaForest::Duration(Wait));
        return TEXT("Выживание-проворность  ·  Высокая сложность  ·  1–4 участника\n\nВыживайте и добывайте припасы. Флаг перемещает смертельные границы.\n\nВыход — на лифте, вместе с грузом на платформе. При гибели переносимые вещи теряются.")+Result;
    }
    return FString::Printf(TEXT("%s  ·  Высокая сложность  ·  1–4 участника\n\nВыдержите нападение и вернитесь на лифте. Стартовое снабжение предусмотрено.\n\nСохраняется только вывезенная добыча. При гибели переносимые вещи теряются.\n\n%s"),
        *Trial->DisplayName.ToString(),
        Running() ? TEXT("Испытание уже принято.") : TEXT("Принять может любой участник."));
}
void APersonaTrialDirector::AcceptTrial(APersonaPlayerController* PC,FName Id)
{
    if(GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Phase!=EPersonaBattlePhase::Idle) return;
    if(!HasAuthority() || !PC) return;
    auto C=Cast<APersonaCharacter>(PC->GetPawn()); if(!C) return;
    auto Feedback=[&](const TCHAR* Text) { C->Interaction->ClientFeedback(Text); };
    if(!C->IsAlive() || C->bTransit || C->bOffline) { Feedback(TEXT("Испытание недоступно")); return; }
    bool NearTerminal=false;
    for(TActorIterator<APersonaHubObject> It(GetWorld());It;++It)
        if(It->Definition && It->Definition->Kind==EPersonaHubKind::Terminal && C->Interaction->CanReachLoot(*It)) { NearTerminal=true; break; }
    if(!NearTerminal) { Feedback(TEXT("Подойдите к терминалу")); return; }
    auto Trial=FindTrial(Id); if(!Trial) { Feedback(TEXT("Испытание не найдено")); return; }
    if(Running()) { Feedback(TEXT("Испытание уже принято")); return; }
    if(Id==PersonaForest::TrialId && ForestAvailableAt>FDateTime::UtcNow().ToUnixTimestamp()) { Feedback(TEXT("Лес ещё недоступен. Время указано в терминале")); return; }
    Definition=Trial; Run.TrialId=Id; Run.Forest=FForestRun(); Run.Forest.Generation=Id==PersonaForest::TrialId ? 1 : 0;
    Run.Id=FGuid::NewGuid().ToString(); Run.Seed=FMath::Rand(); Run.Phase=EPersonaTrialPhase::Accepted;
    Run.Wave=0; Run.Rewarded=false; Run.Departed=false; Run.CratesFilled=false; Run.Crates.Reset(); Run.Participants.Reset();
    Run.Respect=Run.Penalty=Run.Bonus=0;
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It) if(auto P=Cast<APersonaPlayerController>(It->Get())) P->bArenaReady=false;
    EnsureArena(); ForceNetUpdate(); GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->SaveWorld();
    Feedback(TEXT("Испытание принято. Все участники — на лифт."));
}
void APersonaTrialDirector::UseLift(APersonaCharacter* C)
{
    if(!HasAuthority() || !C || !C->IsAlive() || !Platform) return;
    auto OnBoard=[&](APersonaCharacter* P) { FVector L=P->GetActorLocation()-Platform->GetActorLocation(); return FMath::Abs(L.X)<260 && FMath::Abs(L.Y)<260 && L.Z>0 && L.Z<220; };
    if(!OnBoard(C)) { C->Interaction->ClientFeedback(TEXT("Встаньте на платформу")); return; }
    if(Run.Phase==EPersonaTrialPhase::Accepted && Run.Lift==EPersonaLiftPhase::Hub)
    {
        if(!ArenaReady()) { C->Interaction->ClientFeedback(TEXT("Подготовка испытания…")); return; }
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            auto PC=Cast<APersonaPlayerController>(It->Get()); auto P=PC ? Cast<APersonaCharacter>(PC->GetPawn()) : nullptr;
            if(P && P->IsAlive() && (!OnBoard(P) || !PC->bArenaReady)) { C->Interaction->ClientFeedback(TEXT("Все участники должны находиться на лифте")); return; }
        }
        StartLift(false); if(Run.TrialId!=PersonaForest::TrialId) SetupCrates();
    }
    else if(Run.Phase==EPersonaTrialPhase::Forest && Run.Lift==EPersonaLiftPhase::Arena)
    {
        auto P=Run.Forest.Player(C->PersistentId);
        if(P && P->Outcome==EForestOutcome::Active) { StartLift(true); GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->SaveWorld(); }
    }
    else if(Run.Phase==EPersonaTrialPhase::Extraction && Run.Lift==EPersonaLiftPhase::Arena && !Run.Departed && GetWorld()->GetTimeSeconds()<PhaseEnd) StartLift(true);
    else C->Interaction->ClientFeedback(TEXT("Лифт недоступен"));
}
void APersonaTrialDirector::StartLift(bool Ascend)
{
    Run.Lift=Ascend ? EPersonaLiftPhase::Ascending : EPersonaLiftPhase::Descending; LiftStart=GetWorld()->GetTimeSeconds()+.65f; Run.LiftProgress=0;
    Run.Passengers.Reset(); Run.Cargo.Reset();
    if(!Ascend && Run.Phase==EPersonaTrialPhase::Forest) { ForceNetUpdate(); return; } // Empty return trip.
    if(Ascend) Run.Departed=true; else Run.Participants.Reset();
    const FTransform Base=Platform->GetActorTransform();
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It)
    {
        auto C=*It; if(C->bNPC || C->bOffline || !C->IsAlive()) continue;
        FVector L=Base.InverseTransformPosition(C->GetActorLocation());
        if(FMath::Abs(L.X)>270 || FMath::Abs(L.Y)>270 || L.Z<0 || L.Z>220) continue;
        FPersonaCargoRecord R; R.Id=C->PersistentId; R.Relative=C->GetActorTransform().GetRelativeTransform(Base); Run.Passengers.Add(R);
        if(!Ascend) Run.Participants.Add(C->PersistentId);
        else if(Run.Phase==EPersonaTrialPhase::Forest)
        {
            auto P=Run.Forest.Player(C->PersistentId);
            if(!P || P->Outcome!=EForestOutcome::Active) { Run.Passengers.Pop(); continue; }
            EndForestPlayer(C,*P,EForestOutcome::Escaped);
        }
        C->bTransit=true; C->OnRep_Transit(); C->Interaction->CancelActions(); C->ForceNetUpdate();
        C->GetCharacterMovement()->AddTickPrerequisiteActor(this);
        { FMovementBaseInterfaceData BaseData(Platform->Mesh); C->SetBase(&BaseData); }
    }
    for(TActorIterator<APersonaProp> It(GetWorld());It;++It)
    {
        auto P=*It; if(P->Holder) continue; FVector L=Base.InverseTransformPosition(P->Mesh->Bounds.Origin);
        if(FMath::Abs(L.X)>275 || FMath::Abs(L.Y)>275 || L.Z< -25 || L.Z>220) continue;
        FPersonaCargoRecord R; R.Id=P->PersistentItemId; R.Relative=P->GetActorTransform().GetRelativeTransform(Base); Run.Cargo.Add(R); P->bInTransit=true; P->OnRep_Holder();
    }
    if(!Ascend) Run.DifficultyPlayers=FMath::Clamp(Run.Participants.Num(),1,4);
    ForceNetUpdate();
}
void APersonaTrialDirector::FinishLift()
{
    const bool Up=Run.Lift==EPersonaLiftPhase::Ascending; Run.Lift=Up ? EPersonaLiftPhase::Hub : EPersonaLiftPhase::Arena;
    bool Survivor=false;
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(Run.Passengers.ContainsByPredicate([&](const auto& R){return R.Id==It->PersistentId;})) { Survivor|=It->IsAlive(); It->bTransit=false; It->OnRep_Transit(); It->ForceNetUpdate(); }
    for(TActorIterator<APersonaProp> It(GetWorld());It;++It) if(Run.Cargo.ContainsByPredicate([&](const auto& R){return R.Id==It->PersistentItemId;})) { It->bInTransit=false; It->OnRep_Holder(); It->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector); }
    // Retain the manifest until the next departure so clients can settle at the endpoint.
    if(Run.Phase==EPersonaTrialPhase::Forest)
    {
        if(Up) UnloadForestCargo();
        if(!Run.Forest.Players.ContainsByPredicate([](const auto& P){ return P.Outcome==EForestOutcome::Active; })) { FinishRun(true); return; }
        if(Up) StartLift(false);
    }
    else if(!Up) { if(Run.TrialId==PersonaForest::TrialId) StartForest(); else StartWave(); }
    else if(!Run.Rewarded && Survivor) { Run.Rewarded=true; }
    GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->SaveWorld(); ForceNetUpdate();
}
void APersonaTrialDirector::SetupCrates()
{
    TArray<APersonaHubObject*> Crates;
    for(TActorIterator<APersonaHubObject> It(GetWorld());It;++It) if(It->bTrialObject && It->Definition && It->Definition->Kind==EPersonaHubKind::Storage) Crates.Add(*It);
    Crates.Sort([](const auto& A,const auto& B){return A.PersistentId<B.PersistentId;});
    if(Crates.Num()!=2) return;
    if(Run.Crates.IsEmpty())
    {
        FRandomStream Random(Run.Seed);
        // Four separated quadrants preserve the 12 m container footprints and
        // keep both the lift and the northern spawn gate clear.
        TArray<FVector> Sites={FVector(-2800,-2400,-12600),FVector(2800,2400,-12600),FVector(-2800,2400,-12600),FVector(2800,-2400,-12600)};
        int32 First=Random.RandRange(0,3); int32 Second=(First+1)%4;
        Run.Crates.Add(FTransform(Crates[0]->GetActorRotation(),Sites[First])); Run.Crates.Add(FTransform(Crates[1]->GetActorRotation(),Sites[Second]));
    }
    for(int32 I=0; I<2; ++I)
    {
        Crates[I]->SetActorTransform(Run.Crates[I]);
        if(!Run.CratesFilled)
        {
            Crates[I]->Loot->Clear();
            const int32 Guns=Definition ? Definition->SupplyGuns(Run.DifficultyPlayers) : FMath::Max(2,Run.DifficultyPlayers);
            for(int32 Gun=I;Gun<Guns;Gun+=2) Crates[I]->Loot->Add(TEXT("/Game/Persona/Items/DA_Shotgun.DA_Shotgun"),1);
            const int32 Ammo=Definition ? Definition->SupplyAmmo(Run.DifficultyPlayers) : 48+(Run.DifficultyPlayers-1)*24;
            Crates[I]->Loot->Add(TEXT("/Game/Persona/Items/DA_Ammo12GA.DA_Ammo12GA"),Ammo/2+(I==0 ? Ammo%2 : 0));
        }
    }
    if(!Run.CratesFilled) { Run.Bonus=0; Run.CratesFilled=true; }
}
void APersonaTrialDirector::StartWave()
{
    if(!HasAuthority() || !Definition || !Definition->Waves.IsValidIndex(Run.Wave)) return;
    Run.Phase=EPersonaTrialPhase::Wave; PhaseEnd=0;
    auto GM=GetWorld()->GetAuthGameMode<APersonaGameMode>(); if(!GM || !GM->NPCClass) return;
    Run.PendingNPC=FMath::CeilToInt(Definition->Waves[Run.Wave]*(1+Definition->AdditionalPlayerWaveScale*(Run.DifficultyPlayers-1)));
    Run.SpawnedNPC=0; RemainingNPC=Run.PendingNPC; NextReinforcement=0;
    SpawnReinforcements();
    UpdateGate(); ForceNetUpdate(); GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->SaveWorld();
}
void APersonaTrialDirector::SpawnReinforcements()
{
    if(!HasAuthority() || !Definition || Run.PendingNPC<=0 || GetWorld()->GetTimeSeconds()<NextReinforcement) return;
    auto GM=GetWorld()->GetAuthGameMode<APersonaGameMode>(); if(!GM || !GM->NPCClass) return;
    int32 Alive=0;
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(It->bNPC && It->IsAlive() && It->GetActorLocation().Z<-5000) ++Alive;
    const int32 Count=FMath::Min(Run.PendingNPC,FMath::Max(0,FMath::Min(30,Definition->MaxNPCs)-Alive));
    for(int32 I=0;I<Count;++I)
    {
        FVector Location=NPCSpawns.IsEmpty() ? FVector(2200,-6600,-12500) : NPCSpawns[I%NPCSpawns.Num()];
        Location+=FVector((I%5)*105,(I/5)*110,95);
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
        auto C=GetWorld()->SpawnActor<APersonaCharacter>(GM->NPCClass,Location,FRotator(0,90,0),Params);
        if(C)
        {
            const int32 Index=Run.SpawnedNPC++;
            --Run.PendingNPC; C->Tags.Add(TEXT("ShootingNPC"));
            C->PersistentId=Run.Id+FString::Printf(TEXT("_%d_%d"),Run.Wave,Index);
            C->SetMaxHealth(100); C->SetVitals(100,100,100); C->DamageScale=1;
            if(auto AI=Cast<APersonaAIController>(C->Controller))
            { AI->SetTacticalSeed(Run.Seed+Run.Wave*73856093+Index*19349663); AI->AlertAt(FVector((Index%3-1)*1800,-1600,-12500)); }
        }
    }
    NextReinforcement=GetWorld()->GetTimeSeconds()+Definition->ReinforcementInterval; ForceNetUpdate();
}
void APersonaTrialDirector::UpdateGate()
{
    bool Behind=Run.PendingNPC>0;
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(It->bNPC && It->IsAlive() && It->GetActorLocation().Z< -5000 && It->GetActorLocation().Y< -5650) Behind=true;
    for(TActorIterator<APersonaHubObject> It(GetWorld());It;++It) if(It->Definition && It->Definition->Kind==EPersonaHubKind::Gate) It->bOpen=Behind;
}
void APersonaTrialDirector::FinishRun(bool Success)
{
    TArray<AActor*> Remove; int32 SpawnIndex=0;
    if(IsValid(ForestFlag)) ForestFlag->Destroy();
    if(IsValid(MedicalCase)) MedicalCase->Destroy();
    if(Run.TrialId==PersonaForest::TrialId) ForestAvailableAt=FDateTime::UtcNow().ToUnixTimestamp()+600;
    NorthHarvest.Reset(); NorthDrops.Reset(); NorthAtlas.Tiles.Reset(); NorthAtlas.MarkArrayDirty();
    if(IsValid(NorthWorld)) NorthWorld->Destroy(); NorthWorld=nullptr;
    if(Run.TrialId==PersonaForest::TrialId && Platform) Platform->Mesh->SetCanEverAffectNavigation(true);
    Run.Forest=FForestRun();
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It)
    {
        if(It->bNPC && It->GetActorLocation().Z< -5000) Remove.Add(*It);
        else if(!It->bNPC && !It->bDryCollapse && (!It->IsAlive() || It->bSpectator || It->GetActorLocation().Z< -5000))
        {
            for(auto P:It->Interaction->Slots) if(P) P->Destroy(); It->Interaction->Slots.Init(nullptr,3); It->Interaction->Held=nullptr;
            It->ReviveAt(HubSpawn+FVector(0,SpawnIndex++*100,0));
        }
    }
    for(TActorIterator<APersonaProp> It(GetWorld());It;++It) if(!It->Holder && It->GetActorLocation().Z< -5000) Remove.Add(*It);
    for(auto A:Remove) A->Destroy();
    Run.Phase=EPersonaTrialPhase::Offered; Run.Lift=EPersonaLiftPhase::Hub; Run.Cargo.Reset(); Run.Passengers.Reset(); Run.Participants.Reset(); Run.Crates.Reset(); Run.CratesFilled=false; Run.PendingNPC=0; RemainingNPC=0; PhaseEnd=0;
    if(Platform) Platform->SetActorLocation(LiftTop);
    // Keep the authored streaming entry. Removing it would recreate a dynamic
    // instance next run and lose the baked hub/sublevel lighting relationship.
    if(Arena) { Arena->SetShouldBeVisible(false); Arena->SetShouldBeLoaded(false); Arena=nullptr; } bLevelRequested=false;
    ForceNetUpdate(); GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->SaveWorld();
}
void APersonaTrialDirector::Capture(FPersonaRunRecord& Out) const
{
    Out=Run;
    if(Run.Phase==EPersonaTrialPhase::Forest && !bRestorePending) Out.Forest.Elapsed=GetWorld()->GetTimeSeconds()-ForestClock;
    Out.Remaining=FMath::Max(0.f,PhaseEnd-GetWorld()->GetTimeSeconds());
    Out.ReinforcementRemaining=FMath::Max(0.f,NextReinforcement-GetWorld()->GetTimeSeconds());
    Out.LiftDelayRemaining=FMath::Max(0.f,LiftStart-GetWorld()->GetTimeSeconds());
    if(Platform) for(auto& R:Out.Passengers) for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It)
        if(It->PersistentId==R.Id) { R.Relative=It->GetActorTransform().GetRelativeTransform(Platform->GetActorTransform()); break; }
}
void APersonaTrialDirector::Restore(const FPersonaRunRecord& In)
{ Run=In;
    if(auto S=GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>(); S && S->Active)
    {
        ForestResults=S->Active->ForestResults; ForestAvailableAt=S->Active->ForestAvailableAt;
        NorthHarvest=S->Active->Harvest; NorthDrops=S->Active->ForestDrops;
        NorthAtlas.Tiles.Reset(); NorthAtlas.MarkArrayDirty();
        for(const auto& Entry:S->Active->Atlas)
        {
            auto& T=NorthAtlas.Tiles.AddDefaulted_GetRef(); T.Tile=Entry.Tile; T.Seen=Entry.Seen; T.Landmark=Entry.Landmark; NorthAtlas.MarkItemDirty(T);
        }
    }
    Run.Respect=Run.Penalty=Run.Bonus=0; if(Run.TrialId.IsNone()) Run.TrialId=TEXT("Shooting"); if(auto Trial=FindTrial(Run.TrialId)) Definition=Trial; NextReinforcement=GetWorld()->GetTimeSeconds()+In.ReinforcementRemaining; PhaseEnd=In.Remaining>0 ? GetWorld()->GetTimeSeconds()+In.Remaining : 0; LiftStart=GetWorld()->GetTimeSeconds()-In.LiftProgress*(Definition ? Definition->LiftDuration : 15); bRestorePending=Running(); EnsureArena(); }
FString APersonaTrialDirector::ScreenText() const
{
    float Now=GetWorld()->GetGameState() ? GetWorld()->GetGameState()->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    int32 Seconds=FMath::Max(0,FMath::CeilToInt(PhaseEnd-Now));
    if(Run.TrialId==PersonaForest::TrialId)
    {
        if(Run.Phase==EPersonaTrialPhase::Accepted) return TEXT("ВЫЖИВАНИЕ-ПРОВОРНОСТЬ\nСоберитесь на лифте");
        if(Run.Phase==EPersonaTrialPhase::Forest) return TEXT("ВЫЖИВАНИЕ-ПРОВОРНОСТЬ");
    }
    switch(Run.Phase)
    {
    case EPersonaTrialPhase::Offered: return TEXT("PERSONA\nSelect trial at the terminal");
    case EPersonaTrialPhase::Accepted:return TEXT("SHOOTING\nAll subjects: board the lift\nPrepare for survival");
    case EPersonaTrialPhase::Wave:return FString::Printf(TEXT("SHOOTING - WAVE %d / 4\nKILL ALL TARGETS\nRemaining: %d"),Run.Wave+1,RemainingNPC);
    case EPersonaTrialPhase::Intermission:return FString::Printf(TEXT("Prepare for the next wave\nLoot / reload\n%d"),Seconds);
    case EPersonaTrialPhase::Extraction:return FString::Printf(TEXT("Congratulations, you've passed the test,\nreturn to the hub.\nEVACUATE: %d"),Seconds);
    case EPersonaTrialPhase::Purge:return TEXT("EVACUATION CLOSED\nPURGE IN PROGRESS");
    default:return TEXT("PROCEDURE FAILED");
    }
}
void APersonaTrialDirector::Tick(float Delta)
{
    Super::Tick(Delta); EnsureArena();
    if(!Platform) for(TActorIterator<APersonaHubObject> It(GetWorld());It;++It) if(It->Definition && It->Definition->Kind==EPersonaHubKind::Lift) { Platform=*It; break; }
    const float ServerNow=GetWorld()->GetGameState() ? GetWorld()->GetGameState()->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    const bool Purging=Run.Phase==EPersonaTrialPhase::Purge;
    // UE renders the first registered height fog. Merely increasing a second
    // component's density leaves the purge invisible behind the ambient fog.
    if(PurgeFog->IsVisible()!=Purging)
    {
        for(TActorIterator<AExponentialHeightFog> It(GetWorld());It;++It) It->GetComponent()->SetVisibility(!Purging);
        PurgeFog->SetVisibility(Purging);
    }
    PurgeFog->SetFogDensity(Purging ? FMath::Clamp(1-(PhaseEnd-ServerNow)/FMath::Max(1.f,Definition ? Definition->Purge : 10.f),.01f,1.f)*3.f : 0);
    if(HasAuthority() && bRestorePending)
    {
        if(!ArenaReady()) return;
        bRestorePending=false; PhaseEnd=Run.Remaining>0 ? ServerNow+Run.Remaining : 0;
        LiftStart=ServerNow+Run.LiftDelayRemaining-Run.LiftProgress*(Definition ? Definition->LiftDuration : 15);
        if(Platform && (Run.Lift==EPersonaLiftPhase::Ascending || Run.Lift==EPersonaLiftPhase::Descending))
        {
            const bool Up=Run.Lift==EPersonaLiftPhase::Ascending;
            Platform->SetActorLocation(FMath::Lerp(Up ? LiftBottom : LiftTop,Up ? LiftTop : LiftBottom,FMath::SmoothStep(0.f,1.f,Run.LiftProgress)));
            for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It)
                if(auto R=Run.Passengers.FindByPredicate([&](const auto& P){return P.Id==It->PersistentId;}))
                { It->ApplyLiftPosition((R->Relative*Platform->GetActorTransform()).GetLocation(),true); { FMovementBaseInterfaceData BaseData(Platform->Mesh); It->SetBase(&BaseData); } }
        }
        GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->RestoreHubObjects(); if(Run.Phase==EPersonaTrialPhase::Forest) RestoreForest(); else if(Run.CratesFilled) SetupCrates();
    }
    const bool Moving=Run.Lift==EPersonaLiftPhase::Descending || Run.Lift==EPersonaLiftPhase::Ascending;
    if(Platform)
    {
        const bool Up=Run.Lift==EPersonaLiftPhase::Ascending;
        const float Progress=Moving ? FMath::Clamp((ServerNow-LiftStart)/(Definition ? Definition->LiftDuration : 15),0.f,1.f) : 1.f;
        if(HasAuthority() && Moving) Run.LiftProgress=Progress;
        const FVector Position=Moving ? FMath::Lerp(Up ? LiftBottom : LiftTop,Up ? LiftTop : LiftBottom,FMath::SmoothStep(0.f,1.f,Progress))
            : Run.Lift==EPersonaLiftPhase::Hub ? LiftTop : LiftBottom;
        const FVector Before=Platform->GetActorLocation();
        Platform->SetActorLocation(Position);
        Platform->Mesh->ComponentVelocity=(Position-Before)/FMath::Max(Delta,.001f);
        UpdateCabin(Position,Moving);
        const FTransform Base=Platform->GetActorTransform();
        for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It)
        {
            It->GetCharacterMovement()->AddTickPrerequisiteActor(this);
        }
        if(Moving) for(TActorIterator<APersonaProp> It(GetWorld());It;++It)
            if(auto R=Run.Cargo.FindByPredicate([&](const auto& X){return X.Id==It->PersistentItemId;}))
            { It->bInTransit=true; It->Mesh->SetSimulatePhysics(false); It->SetActorTransform(R->Relative*Base,false,nullptr,ETeleportType::TeleportPhysics); }
        if(Moving && Progress>=1 && HasAuthority()) FinishLift();
    }
    if(!HasAuthority())
    {
        if(!Running() && Arena) { Arena->SetShouldBeVisible(false); Arena->SetShouldBeLoaded(false); Arena=nullptr; bLevelRequested=false; }
        return;
    }
    if(Run.Phase==EPersonaTrialPhase::Forest) { UpdateForest(Delta); return; }
    UpdateElapsed+=Delta; if(UpdateElapsed<.2f || !ArenaReady()) return; UpdateElapsed=0;
    const float Now=GetWorld()->GetTimeSeconds();
    if(Run.Phase==EPersonaTrialPhase::Wave)
    {
        SpawnReinforcements();
        RemainingNPC=Run.PendingNPC; for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(It->bNPC && It->IsAlive() && It->GetActorLocation().Z< -5000)
        {
            ++RemainingNPC;
        }
        UpdateGate();
        if(RemainingNPC==0) { ++Run.Wave; Run.Phase=Definition && Run.Wave<Definition->Waves.Num() ? EPersonaTrialPhase::Intermission : EPersonaTrialPhase::Extraction; PhaseEnd=Now+(Run.Phase==EPersonaTrialPhase::Extraction ? (Definition ? Definition->Extraction : 30) : (Definition ? Definition->Intermission : 15)); }
    }
    if(Run.Phase==EPersonaTrialPhase::Intermission && Now>=PhaseEnd) StartWave();
    if(Run.Phase==EPersonaTrialPhase::Extraction && Now>=PhaseEnd) { Run.Phase=EPersonaTrialPhase::Purge; PhaseEnd=Now+(Definition ? Definition->Purge : 10); }
    if(Run.Phase==EPersonaTrialPhase::Purge)
        for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(!It->bNPC && It->IsAlive() && !It->bTransit && It->GetActorLocation().Z< -5000) UGameplayStatics::ApplyDamage(*It,Now>=PhaseEnd ? 10000.f : 2.5f,nullptr,this,UDamageType::StaticClass());
    if(Run.Phase==EPersonaTrialPhase::Wave || Run.Phase==EPersonaTrialPhase::Intermission || Run.Phase==EPersonaTrialPhase::Extraction || Run.Phase==EPersonaTrialPhase::Purge)
    {
        int32 Alive=0,Lower=0;
        for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(!It->bNPC && It->IsAlive() && InRun(*It)) { ++Alive; if(It->GetActorLocation().Z< -5000 && !It->bTransit) ++Lower; }
        if(!Alive) { Run.Phase=EPersonaTrialPhase::Failed; PhaseEnd=Now+5; }
        else if(Run.Rewarded && Lower==0 && Run.Lift==EPersonaLiftPhase::Hub) FinishRun(true);
    }
    if(Run.Phase==EPersonaTrialPhase::Failed && Now>=PhaseEnd) FinishRun(false);
}
