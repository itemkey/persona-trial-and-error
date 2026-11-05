#include "PersonaBattleArena.h"
#include "PersonaFramework.h"
#include "PersonaCharacter.h"
#include "PersonaAttributes.h"
#include "PersonaLoot.h"
#include "PersonaHub.h"
#include "PersonaSave.h"
#include "PersonaInteraction.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace
{
constexpr float Travel=8,StartZ=1200,Top=2000,Bottom=-300;
FVector LiftAt(int32 Team,float Z) { return FVector(Team==0 ? -3100 : 3100,0,Z); }
const FVector Normals[]={FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),FVector(0,0,1),FVector(0,0,-1)};
}
APersonaBattleArena::APersonaBattleArena()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PrePhysics;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Arena")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Box(TEXT("/Engine/BasicShapes/Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Stone(TEXT("/Game/Persona/Materials/M_Concrete"));
    auto Post=CreateDefaultSubobject<UPostProcessComponent>(TEXT("ArenaExposure"));Post->SetupAttachment(GetRootComponent());Post->bUnbound=true;
    Post->Settings.bOverride_BloomIntensity=true;Post->Settings.BloomIntensity=.12f;
    int32 Index=0;
    auto Block=[&](const FVector& At,const FVector& Size)
    {
        auto C=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Structure%d"),Index++));C->SetupAttachment(GetRootComponent());C->SetStaticMesh(Box.Object);
        C->SetMaterial(0,Stone.Object);C->SetRelativeLocation(At);C->SetRelativeScale3D(Size/100);C->SetCollisionProfileName(TEXT("BlockAll"));Architecture.Add(C);
    };
    // A bridge over the central service pit makes the lower boundary reachable.
    Block(FVector(-2375,0,-120),FVector(3850,8600,200));
    Block(FVector(2375,0,-120),FVector(3850,8600,200));
    Block(FVector(0,-1300,-120),FVector(900,6000,200));
    Block(FVector(0,3800,-120),FVector(900,1000,200));
    Block(FVector(0,2500,-40),FVector(400,1600,40));
    for(int32 Side:{-1,1})
    {
        Block(FVector(0,Side*700,180),FVector(300,1000,400));
        Block(FVector(Side*2300,0,220),FVector(350,1400,480));
        for(int32 Y:{-1,1})
        {
            Block(FVector(Side*1500,Y*1900,100),FVector(650,450,240));
            Block(FVector(Side*850,Y*650,70),FVector(450,450,180));
            Block(FVector(Side*3200,Y*2900,550),FVector(180,180,1140));
        }
        auto L=CreateDefaultSubobject<URectLightComponent>(*FString::Printf(TEXT("CeilingLight%d"),Side+1));L->SetupAttachment(GetRootComponent());
        L->SetMobility(EComponentMobility::Movable);
        L->SetRelativeLocation(FVector(Side*1600,0,1500));L->SetRelativeRotation(FRotator(-90,0,0));L->SetIntensity(90000);L->SetSourceWidth(2800);L->SetSourceHeight(1400);L->SetAttenuationRadius(7500);L->SetLightColor(FLinearColor(.88f,.91f,.95f));
        auto Fill=CreateDefaultSubobject<UDirectionalLightComponent>(*FString::Printf(TEXT("SoftFill%d"),Side+1));Fill->SetupAttachment(GetRootComponent());Fill->SetMobility(EComponentMobility::Movable);
        Fill->SetRelativeRotation(FRotator(-45,Side<0 ? 45 : 225,0));Fill->SetIntensity(.8f);Fill->SetCastShadows(false);Fill->SetLightColor(FLinearColor(.72f,.78f,.85f));
        Block(FVector(Side*1600,0,1450),FVector(2600,90,100));
    }
    for(int32 I=0;I<6;++I)
    {
        auto F=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Boundary%d"),I));F->SetupAttachment(GetRootComponent());F->SetStaticMesh(Plane.Object);
        F->SetRelativeRotation(FRotationMatrix::MakeFromZ(-Normals[I]).Rotator());F->SetCollisionEnabled(ECollisionEnabled::NoCollision);F->SetCastShadow(false);Faces.Add(F);
    }
}
APersonaBattleArena* APersonaBattleArena::Get(UWorld* W) { if(W) for(TActorIterator<APersonaBattleArena> It(W);It;++It) return *It;return nullptr; }
float APersonaBattleArena::ServerTime() const { auto S=GetWorld()->GetGameState();return S ? S->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds(); }
void APersonaBattleArena::BeginPlay()
{
    Super::BeginPlay();auto Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Persona/North/M_Boundary.M_Boundary"));
    for(auto F:Faces) Tints.Add(F->CreateDynamicMaterialInstance(0,Material));
    if(!HasAuthority()) return;
    auto Battle=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();Phase=EPersonaBattlePhase::Waiting;WaitingUntil=ServerTime()+120;
    for(int32 T=0;T<2;++T)
    {
        auto L=GetWorld()->SpawnActor<APersonaHubObject>(APersonaHubObject::StaticClass(),LiftAt(T,StartZ),FRotator::ZeroRotator);
        L->PersistentId=TEXT("BattleLift")+FString::FromInt(T);L->Configure(LoadObject<UPersonaHubAsset>(nullptr,TEXT("/Game/Persona/Hub/DA_HUB_Lift_CTRL.DA_HUB_Lift_CTRL")));L->SetReplicateMovement(false);Platforms.Add(L);
    }
    SpawnCargo();Battle->ArenaStarted();
}
void APersonaBattleArena::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(APersonaBattleArena,Platforms);DOREPLIFETIME(APersonaBattleArena,DescendAt);DOREPLIFETIME(APersonaBattleArena,FightAt);DOREPLIFETIME(APersonaBattleArena,HalfExtent);DOREPLIFETIME(APersonaBattleArena,WaitingUntil);
}
void APersonaBattleArena::EnsureCabins()
{
    if(Platforms.Num()!=2 || !Platforms[0] || !Platforms[1] || Doors.Num()) return;
    auto Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));auto Mat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Persona/Materials/M_Concrete.M_Concrete"));
    for(int32 T=0;T<2;++T) for(int32 I=0;I<6;++I)
    {
        auto D=NewObject<UStaticMeshComponent>(this);D->SetupAttachment(Platforms[T]->GetRootComponent());D->SetStaticMesh(Mesh);D->SetMaterial(0,Mat);D->SetCollisionProfileName(I==5 ? TEXT("NoCollision") : TEXT("BlockAll"));D->RegisterComponent();Doors.Add(D);
    }
}
bool APersonaBattleArena::ClientReady() const
{ return Platforms.Num()==2 && Platforms[0] && Platforms[1] && Platforms[0]->Mesh->GetStaticMesh() && Platforms[1]->Mesh->GetStaticMesh() && Doors.Num()==12; }
bool APersonaBattleArena::IsOnLift(APersonaCharacter* C) const
{
    if(!C) return false;
    for(auto L:Platforms) if(L) { const auto P=C->GetActorLocation()-L->GetActorLocation();if(FMath::Abs(P.X)<260 && FMath::Abs(P.Y)<260 && P.Z>0 && P.Z<220) return true; }
    return false;
}
bool APersonaBattleArena::SpawnPlayer(APersonaPlayerController* PC)
{
    auto Battle=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();auto PS=PC ? PC->GetPlayerState<APersonaPlayerState>() : nullptr;
    if(!PS) return false;const int32 Team=Battle->Ticket.TeamFor(PS->PersistentId);if(Team<0 || Platforms.Num()!=2) return false;
    PS->Team=Team;PC->bArenaReady=false;
    auto Small=Battle->Ticket;
    for(auto& Party:Small.Parties) { Party.Cargo.Reset();for(auto& Subject:Party.Subjects) { Subject.Inventory.Reset();Subject.Loot.Reset();Subject.BodyMotion.Reset();Subject.CorpsePose.Reset(); } }
    PC->ClientBattleTicket(Small,Team);
    if(GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->Rejoin(PC)) return true;
    if(Spawned.Contains(PS->PersistentId)) return false;
    const auto& Members=Battle->Ticket.Parties[Team].Subjects;const int32 Index=Members.IndexOfByPredicate([&](const auto& S){return S.Id==PS->PersistentId;});if(Index<0) return false;
    FTransform At(FRotator(0,Team ? 180 : 0,0),LiftAt(Team,StartZ)+FVector((Index%2)*130-65,(Index/2)*130-65,95));
    auto GM=GetWorld()->GetAuthGameMode<APersonaGameMode>();FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto C=GetWorld()->SpawnActor<APersonaCharacter>(GM->DefaultPawnClass,At,Params);if(!C) return false;PC->Possess(C);
    auto R=Members[Index];R.Team=Team;R.Transform=At;R.View=At.Rotator();R.Velocity=FVector::ZeroVector;R.Transit=true;R.Crouched=false;R.Sprinting=false;R.Movement=MOVE_Walking;
    UPersonaSaveSubsystem::RestoreSubject(C,R);C->bTransit=true;C->OnRep_Transit();
    FMovementBaseInterfaceData Base(Platforms[Team]->Mesh);C->SetBase(&Base);Spawned.Add(R.Id);return true;
}
void APersonaBattleArena::SpawnCargo()
{
    const auto B=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();if(B->Ticket.Parties.Num()!=2) return;
    for(int32 T=0;T<2;++T) for(const auto& C:B->Ticket.Parties[T].Cargo)
        if(auto P=UPersonaLoot::Spawn(GetWorld(),C.Item,C.Relative*Platforms[T]->GetActorTransform()))
        { P->bInTransit=true;P->Mesh->SetSimulatePhysics(false); }
}
void APersonaBattleArena::Tick(float Delta)
{
    Super::Tick(Delta);EnsureCabins();const float Now=ServerTime();auto B=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();
    const bool Moving=Phase==EPersonaBattlePhase::Descending;
    const float Alpha=Moving ? FMath::Clamp((Now-DescendAt)/Travel,0.f,1.f) : Phase==EPersonaBattlePhase::Waiting ? 0.f : 1.f;
    const float Z=StartZ*(1-FMath::SmoothStep(0.f,1.f,Alpha));
    if(Platforms.Num()==2) for(int32 T=0;T<2;++T) if(Platforms[T])
    {
        const auto Before=Platforms[T]->GetActorLocation();Platforms[T]->SetActorLocation(LiftAt(T,Z));Platforms[T]->Mesh->ComponentVelocity=(Platforms[T]->GetActorLocation()-Before)/FMath::Max(Delta,.001f);
    }
    const bool Closed=Phase==EPersonaBattlePhase::Waiting || Moving;
    for(int32 I=0;I<Doors.Num();++I)
    {
        const int32 Side=I%6;const bool Roof=Side==4,Floor=Side==5;auto D=Doors[I];
        D->SetRelativeLocation(Floor ? FVector(0,0,-10) : Roof ? FVector(0,0,300) : Side<2 ? FVector(Side ? -290 : 290,0,150) : FVector(0,Side==2 ? 290 : -290,150));
        D->SetRelativeScale3D(Roof || Floor ? FVector(6,6,.15f) : Side<2 ? FVector(.15f,6,3) : FVector(6,.15f,3));
        D->SetVisibility(Closed || Floor);D->SetCollisionEnabled(Closed && !Floor ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    }
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) It->GetCharacterMovement()->AddTickPrerequisiteActor(this);
    if(HasAuthority() && Phase==EPersonaBattlePhase::Fighting) HalfExtent=4000*FMath::Clamp(1-(Now-FightAt-900)/300,0.f,1.f);
    auto PC=GetWorld()->GetFirstPlayerController();const auto View=PC && PC->GetViewTarget() ? PC->GetViewTarget()->GetActorLocation() : FVector::ZeroVector;
    for(int32 I=0;I<6;++I)
    {
        const float Extent=I<4 ? HalfExtent : (Top-Bottom)*.5f;
        Faces[I]->SetRelativeLocation(I<4 ? Normals[I]*Extent+FVector(0,0,(Top+Bottom)*.5f) : FVector(0,0,I==4 ? Top : Bottom));
        Faces[I]->SetRelativeScale3D(I<4 ? FVector(HalfExtent*2/100,(Top-Bottom)/100,1) : FVector(HalfExtent*2/100,HalfExtent*2/100,1));
        Faces[I]->SetVisibility(Phase==EPersonaBattlePhase::Fighting || Phase==EPersonaBattlePhase::Descending);
        const float Gap=I<4 ? HalfExtent-FMath::Abs(I<2 ? View.X : View.Y) : I==4 ? Top-View.Z : View.Z-Bottom;
        if(Tints.IsValidIndex(I) && Tints[I]) { Tints[I]->SetScalarParameterValue(TEXT("Veil"),.08f+.5f*FMath::Clamp(1-Gap/450,0.f,1.f));Tints[I]->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.2f,.32f,.35f)); }
    }
    if(!HasAuthority()) return;
    if(Phase==EPersonaBattlePhase::Waiting)
    {
        bool AllReady=B->Ticket.Parties.Num()==2;
        for(const auto& P:B->Ticket.Parties) for(const auto& S:P.Subjects)
        { bool Found=false;for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It) if(auto C=Cast<APersonaPlayerController>(It->Get());C && C->GetPlayerState<APersonaPlayerState>()->PersistentId==S.Id && C->bArenaReady) Found=true;AllReady&=Found; }
        if(AllReady) { Phase=EPersonaBattlePhase::Descending;DescendAt=Now+.65f;B->SetPhase(Phase,TEXT("Спуск на поле боя…"));ForceNetUpdate(); }
        else if(Now>WaitingUntil) B->Abort(TEXT("Не все участники подключились. Груз возвращён."));
    }
    if(Phase==EPersonaBattlePhase::Descending)
    {
        for(int32 T=0;T<B->Ticket.Parties.Num();++T) for(const auto& C:B->Ticket.Parties[T].Cargo)
            for(TActorIterator<APersonaProp> It(GetWorld());It;++It) if(It->PersistentItemId==C.Item.Id) It->SetActorTransform(C.Relative*Platforms[T]->GetActorTransform());
        if(Alpha>=1)
        {
            Phase=EPersonaBattlePhase::Fighting;FightAt=Now;B->SetPhase(Phase,TEXT("Бой до гибели команды. Огонь по своим включён."));
            for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) { It->bTransit=false;It->OnRep_Transit();It->ForceNetUpdate(); }
            for(TActorIterator<APersonaProp> It(GetWorld());It;++It) { It->bInTransit=false;It->OnRep_Holder(); }
            ForceNetUpdate();
        }
    }
    if(Phase!=EPersonaBattlePhase::Fighting) return;
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(It->IsAlive() && B->Ticket.Contains(It->PersistentId))
    {
        if(It->bOffline) { if(!DisconnectedAt.Contains(It->PersistentId)) DisconnectedAt.Add(It->PersistentId,Now); }
        else DisconnectedAt.Remove(It->PersistentId);
        const auto P=It->GetActorLocation();const float R=It->GetCapsuleComponent()->GetScaledCapsuleRadius(),H=It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        if(FMath::Abs(P.X)+R>=HalfExtent || FMath::Abs(P.Y)+R>=HalfExtent || P.Z-H<=Bottom || P.Z+H>=Top || (DisconnectedAt.Contains(It->PersistentId) && Now-DisconnectedAt[It->PersistentId]>=90))
            UGameplayStatics::ApplyDamage(*It,It->GetVitals()->GetMaxHealth()+1,nullptr,this,UDamageType::StaticClass());
    }
    if(!bResolutionPending) { bResolutionPending=true;GetWorld()->GetTimerManager().SetTimerForNextTick(this,&ThisClass::ResolveOutcome); }
}
void APersonaBattleArena::ResolveOutcome()
{
    bResolutionPending=false;if(!HasAuthority() || Phase!=EPersonaBattlePhase::Fighting) return;
    auto B=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();
    int32 Alive[2]={0,0};for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(It->IsAlive()) { const int32 T=B->Ticket.TeamFor(It->PersistentId);if(T>=0) ++Alive[T]; }
    if(!Alive[0] || !Alive[1])
    {
        Winner=Alive[0] ? 0 : Alive[1] ? 1 : -1;Phase=EPersonaBattlePhase::Looting;HalfExtent=4000;
        B->SetPhase(Phase,Winner<0 ? TEXT("Ничья. Возвращение секторов…") : TEXT("Победа. Соберите трофеи и вернитесь на свой лифт."));ForceNetUpdate();
        if(Winner<0) B->Settle(Result(-1));
    }
}
FPersonaBattleSettlement APersonaBattleArena::Result(int32 Team) const
{
    FPersonaBattleSettlement R;auto B=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();R.Match=B->Ticket.Match;R.ManifestHash=B->ManifestHash();R.Winner=Team;
    if(Team<0)
    {
        // A draw returns the original lift stakes to their own sectors, even
        // if physics or a participant moved that cargo during the fight.
        for(const auto& P:B->Ticket.Parties) R.DrawCargo.Append(P.Cargo);
        return R;
    }
    if(Team>=0) for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(It->IsAlive() && B->Ticket.TeamFor(It->PersistentId)==Team) R.Survivors.Add(UPersonaSaveSubsystem::CaptureSubject(*It));
    for(TActorIterator<APersonaProp> It(GetWorld());It;++It) if(!It->Holder)
        for(int32 T=0;T<Platforms.Num();++T) if(Team<0 || T==Team)
        {
            const auto At=Platforms[T]->GetActorTransform().InverseTransformPosition(It->Mesh->Bounds.Origin);
            if(FMath::Abs(At.X)>275 || FMath::Abs(At.Y)>275 || At.Z< -25 || At.Z>220) continue;
            FPersonaBattleCargo C;C.Item=UPersonaLoot::Capture(*It);C.Relative=It->GetActorTransform().GetRelativeTransform(Platforms[T]->GetActorTransform());
            if(Team<0) R.DrawCargo.Add(C);else R.Cargo.Add(C);
        }
    return R;
}
void APersonaBattleArena::UseLift(APersonaCharacter* C)
{
    if(!HasAuthority() || !C) return;
    if(Phase!=EPersonaBattlePhase::Looting || Winner<0) { C->Interaction->ClientFeedback(TEXT("Эвакуация откроется после окончания боя."));return; }
    auto B=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();if(B->Ticket.TeamFor(C->PersistentId)!=Winner) return;
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(It->IsAlive() && B->Ticket.TeamFor(It->PersistentId)==Winner)
    {
        const auto P=It->GetActorLocation()-Platforms[Winner]->GetActorLocation();
        if(FMath::Abs(P.X)>260 || FMath::Abs(P.Y)>260 || P.Z<0 || P.Z>220) { C->Interaction->ClientFeedback(TEXT("Все живые победители должны стоять на своём лифте."));return; }
    }
    auto R=Result(Winner);for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) { It->bTransit=true;It->OnRep_Transit();It->Interaction->CancelActions();It->ForceNetUpdate(); }
    Phase=EPersonaBattlePhase::Settling;B->Settle(R);ForceNetUpdate();
}
