#include "PersonaNorthernForest.h"
#include "PersonaHub.h"
#include "PersonaCharacter.h"
#include "PersonaLoot.h"
#include "PersonaSave.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/PostProcessVolume.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "AI/NavigationSystemHelpers.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace
{
constexpr double Grade=-12600;
constexpr int Divisions=20;
double Phase(int32 Seed) { return double(uint32(Seed)%10007)*.037; }
double Channel(int32 Seed,double Y) { return 9800+1350*FMath::Sin(Y*.00024+Phase(Seed))+580*FMath::Sin(Y*.00057-Phase(Seed)); }
double RiverDistance(int32 Seed,double X,double Y)
{
    const double D=X-Channel(Seed,Y);
    return D-21000*FMath::RoundToDouble(D/21000);
}
uint32 TileSeed(int32 Seed,FIntPoint K)
{
    uint32 H=uint32(Seed)^uint32(K.X)*0x9e3779b9u^uint32(K.Y)*0x85ebca6bu;
    H^=H>>16; H*=0x7feb352du; H^=H>>15;return H;
}
const TCHAR* ItemName(ENorthSite Kind)
{
    switch(Kind)
    {
        case ENorthSite::Orchard:return TEXT("DA_Ration");
        case ENorthSite::Spring:return TEXT("DA_Flask");
        case ENorthSite::Scrap:return TEXT("DA_Scrap");
        case ENorthSite::Cloth:return TEXT("DA_Cloth");
        case ENorthSite::Shells:return TEXT("DA_Ammo12GA");
        default:return TEXT("");
    }
}
UMaterialInterface* Material(const TCHAR* Name)
{
    return LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/Persona/North/M_%s.M_%s"),Name,Name));
}
}
UPersonaForestTerrain::UPersonaForestTerrain(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SetCustomNavigableGeometry(EHasCustomNavigableGeometry::Yes);
}
bool UPersonaForestTerrain::DoCustomNavigableGeometryExport(FNavigableGeometryExport& Export) const
{
    Export.ExportCustomMesh(NavVertices.GetData(),NavVertices.Num(),NavIndices.GetData(),NavIndices.Num(),GetComponentTransform());
    return false;
}
FIntPoint PersonaNorth::TileAt(const FVector& P)
{
    return FIntPoint(FMath::FloorToInt((P.X+TileSize*.5)/TileSize),FMath::FloorToInt((P.Y+TileSize*.5)/TileSize));
}
FVector PersonaNorth::Origin(FIntPoint K) { return FVector(double(K.X)*TileSize-TileSize*.5,double(K.Y)*TileSize-TileSize*.5,Grade); }
float PersonaNorth::Height(int32 Seed,double X,double Y)
{
    const double P=Phase(Seed), R=FMath::Abs(RiverDistance(Seed,X,Y));
    const double Hills=118*FMath::Sin(.00064*X+P)*FMath::Cos(.00048*Y-P)+205*FMath::Sin(.00027*Y+.00015*X+P)+35*FMath::Sin(.0022*X+.0013*Y);
    const double Banks=FMath::SmoothStep(600.,1500.,R);
    const double Bed=-(1-FMath::SmoothStep(160.,535.,R))*(680+1500*FMath::Square(FMath::Sin(Y*.00012+P)));
    return (Hills*Banks+Bed)*FMath::SmoothStep(640.,2200.,FMath::Max(FMath::Abs(X),FMath::Abs(Y)))-8;
}
FVector PersonaNorth::Site(int32 Seed,FIntPoint K,ENorthSite& Kind)
{
    FRandomStream R(TileSeed(Seed,K));Kind=ENorthSite::None;
    const FVector Base=Origin(K);
    FVector P=Base+FVector(R.FRandRange(780,TileSize-780),R.FRandRange(780,TileSize-780),0);
    if(FMath::Abs(P.X)<2500 && FMath::Abs(P.Y-490)<2500) return P;
    const int Roll=R.RandRange(0,9);
    Kind=Roll<3 ? ENorthSite::Orchard : Roll<5 ? ENorthSite::Spring : Roll<7 ? ENorthSite::Scrap : Roll==7 ? ENorthSite::Cloth : Roll==8 ? ENorthSite::Shells : ENorthSite::None;
    if(FMath::Abs(RiverDistance(Seed,P.X,P.Y))<720) Kind=ENorthSite::None;
    P.Z+=Height(Seed,P.X,P.Y);
    return P;
}
const TCHAR* PersonaNorth::SiteName(ENorthSite K)
{
    switch(K)
    {
        case ENorthSite::Orchard:return TEXT("Дикая яблоня");
        case ENorthSite::Spring:return TEXT("Родник");
        case ENorthSite::Scrap:return TEXT("Металл");
        case ENorthSite::Cloth:return TEXT("Ткань");
        case ENorthSite::Shells:return TEXT("Патроны");
        default:return TEXT("");
    }
}
void FNorthAtlas::Visit(const FVector& P,ENorthSite Landmark)
{
    const FIntPoint K=PersonaNorth::TileAt(P);const FVector L=P-PersonaNorth::Origin(K);
    const int X=FMath::Clamp(int(L.X*8/PersonaNorth::TileSize),0,7),Y=FMath::Clamp(int(L.Y*8/PersonaNorth::TileSize),0,7);
    auto* T=Tiles.FindByPredicate([&](const auto& V){return V.Tile==K;});
    if(!T) { T=&Tiles.AddDefaulted_GetRef();T->Tile=K; }
    const uint64 Bit=uint64(1)<<(8*Y+X);
    if((T->Seen&Bit)!=0 && (Landmark==ENorthSite::None || T->Landmark==Landmark)) return;
    T->Seen|=Bit;
    if(Landmark!=ENorthSite::None) T->Landmark=Landmark;
    MarkItemDirty(*T);
}

APersonaNorthernForest::APersonaNorthernForest()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Weather")));
    Sky=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Horizon"));Sky->SetupAttachment(GetRootComponent());
    Sky->SetCollisionEnabled(ECollisionEnabled::NoCollision);Sky->SetCanEverAffectNavigation(false);Sky->SetCastShadow(false);
    Weather=CreateDefaultSubobject<UPostProcessComponent>(TEXT("ForestExposure"));Weather->SetupAttachment(GetRootComponent());
    Weather->bUnbound=true;Weather->Priority=200;Weather->bEnabled=false;Weather->BlendWeight=0;
    auto& S=Weather->Settings;S.bOverride_AutoExposureMethod=true;S.AutoExposureMethod=AEM_Manual;
    S.bOverride_AutoExposureBias=true;S.AutoExposureBias=0;S.bOverride_AutoExposureApplyPhysicalCameraExposure=true;S.AutoExposureApplyPhysicalCameraExposure=false;
    S.bOverride_IndirectLightingIntensity=true;S.IndirectLightingIntensity=0;
    Daylight=CreateDefaultSubobject<UPointLightComponent>(TEXT("Overcast"));Daylight->SetupAttachment(GetRootComponent());
    Daylight->SetRelativeLocation(FVector(-780,520,1750));Daylight->SetIntensity(1200);Daylight->SetAttenuationRadius(6000);Daylight->SetCastShadows(false);
}
bool APersonaNorthernForest::Sync(APersonaTrialDirector& D,const FVector& Center)
{
    SetActorLocation(Center);
    auto PC=GetWorld()->GetFirstPlayerController();
    const bool Below=PC && PC->IsLocalController() && PC->GetViewTarget() && PC->GetViewTarget()->GetActorLocation().Z<-5000;
    Weather->BlendWeight=FMath::FInterpConstantTo(Weather->BlendWeight,Below ? 1.f : 0.f,GetWorld()->GetDeltaSeconds(),.5f);
    Weather->bEnabled=Weather->BlendWeight>0;
    Sky->SetVisibility(Weather->bEnabled);
    if(!Sky->GetStaticMesh())
    {
        Sky->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));Sky->SetRelativeScale3D(FVector(195));Sky->SetMaterial(0,Material(TEXT("fog")));
        for(const TCHAR* Name:{TEXT("Spruce0"),TEXT("Spruce1"),TEXT("Spruce2"),TEXT("Spruce3"),TEXT("Birch"),TEXT("Snag"),TEXT("Orchard"),TEXT("Outcrop0"),TEXT("Outcrop1"),TEXT("Windfall")})
            Kit.Add(FName(Name),LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Game/Persona/North/SM_%s.SM_%s"),Name,Name)));
        Kit.Add(TEXT("Trunk"),LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
        Kit.Add(TEXT("Bridge"),LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Persona/Survival/SM_Bridge.SM_Bridge")));
    }
    const FIntPoint Current=PersonaNorth::TileAt(Center);
    if(Window==Current) return true;
    Window=Current;
    TSet<FIntPoint> Needed;Needed.Add(FIntPoint::ZeroValue);
    for(int X=-2;X<=2;++X) for(int Y=-2;Y<=2;++Y) Needed.Add(Current+FIntPoint(X,Y));
    // All peers have identical solid ground before they acknowledge lift readiness.
    for(const auto& K:Needed) if(!Chunks.Contains(K)) LoadTile(D,K);
    TArray<FIntPoint> Old;Chunks.GetKeys(Old);
    for(const auto& K:Old) if(!Needed.Contains(K)) UnloadTile(D,K);
    for(TActorIterator<APostProcessVolume> It(GetWorld());It;++It) if(It->ActorHasTag(TEXT("NorthExposure"))) It->SetActorLocation(Center);
    if(D.HasAuthority()) for(TActorIterator<ANavMeshBoundsVolume> It(GetWorld());It;++It) if(It->ActorHasTag(TEXT("NorthNav")))
    {
        It->SetActorLocation(Center);
        if(auto Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld())) Nav->OnNavigationBoundsUpdated(*It);
    }
    return Chunks.Contains(Current);
}

void APersonaNorthernForest::LoadTile(APersonaTrialDirector& D,FIntPoint K)
{
    const FVector Base=PersonaNorth::Origin(K);
    FActorSpawnParameters Params;Params.Name=FName(*FString::Printf(TEXT("North_%s_%d_%d"),*D.Run.Id,K.X,K.Y));Params.bDeferConstruction=true;
    auto Chunk=GetWorld()->SpawnActor<AActor>(AActor::StaticClass(),FTransform(Base),Params); if(!Chunk) return;
    Chunk->SetNetAddressable();Chunk->FinishSpawning(FTransform(Base));
    auto Ground=NewObject<UPersonaForestTerrain>(Chunk,TEXT("Terrain"));Ground->SetNetAddressable();Ground->SetMobility(EComponentMobility::Static);
    Ground->SetCanEverAffectNavigation(false);
    Chunk->SetRootComponent(Ground);Ground->SetWorldLocation(Base);Ground->SetCollisionProfileName(TEXT("BlockAll"));Ground->bUseAsyncCooking=false;Ground->RegisterComponent();
    Chunk->AddInstanceComponent(Ground);Chunks.Add(K,Chunk);
    const int Seed=D.Run.Seed;
    TArray<FVector> V,N;TArray<FVector2D> UV;TArray<int32> Tri;
    const double Pitch=double(PersonaNorth::TileSize)/Divisions;
    for(int Y=0;Y<=Divisions;++Y) for(int X=0;X<=Divisions;++X)
    {
        const double WX=Base.X+X*Pitch,WY=Base.Y+Y*Pitch;
        V.Emplace(X*Pitch,Y*Pitch,PersonaNorth::Height(Seed,WX,WY));UV.Emplace(X/4.,Y/4.);
        N.Add(FVector(PersonaNorth::Height(Seed,WX-25,WY)-PersonaNorth::Height(Seed,WX+25,WY),
                      PersonaNorth::Height(Seed,WX,WY-25)-PersonaNorth::Height(Seed,WX,WY+25),50).GetSafeNormal());
        if(X<Divisions && Y<Divisions)
        {
            const int A=Y*(Divisions+1)+X,B=A+Divisions+1;Tri.Append({A,B,A+1,A+1,B,B+1});
        }
    }
    Ground->CreateMeshSection_LinearColor(0,V,Tri,N,UV,{}, {},true);Ground->SetMaterial(0,Material(TEXT("ground")));
    Ground->NavVertices=V;
    Ground->NavIndices=Tri;
    Ground->SetCanEverAffectNavigation(true);
    FNavigationSystem::UpdateComponentData(*Ground);
    // The channel surface is below its banks. Generate water only in tiles crossed by the creek.
    const double MidX=Base.X+PersonaNorth::TileSize*.5,MidY=Base.Y+PersonaNorth::TileSize*.5;
    if(FMath::Abs(RiverDistance(Seed,MidX,MidY))<3500)
    {
        TArray<FVector> Water;TArray<int32> Indices;
        for(int J=0;J<=Divisions;++J)
        {
            const double WY=Base.Y+J*Pitch,ChannelX=MidX-RiverDistance(Seed,MidX,WY);
            Water.Emplace(FMath::Clamp(ChannelX-490-Base.X,0.,double(PersonaNorth::TileSize)),J*Pitch,-210);
            Water.Emplace(FMath::Clamp(ChannelX+490-Base.X,0.,double(PersonaNorth::TileSize)),J*Pitch,-210);
            if(J<Divisions){const int A=J*2;Indices.Append({A,A+2,A+1,A+1,A+2,A+3});}
        }
        Ground->CreateMeshSection_LinearColor(1,Water,Indices,{}, {},{}, {},false);Ground->SetMaterial(1,Material(TEXT("stream")));
    }
    TMap<FName,UInstancedStaticMeshComponent*> Groups;
    auto Plant=[&](FName Name,FVector At,FRotator Rotation,FVector Scale,bool Solid)
    {
        auto Mesh=Kit.FindRef(Name);if(!Mesh) return;
        auto& Group=Groups.FindOrAdd(Name);
        if(!Group)
        {
            Group=NewObject<UInstancedStaticMeshComponent>(Chunk,Name);Group->SetNetAddressable();Group->SetMobility(EComponentMobility::Static);Group->SetupAttachment(Ground);Group->SetStaticMesh(Mesh);
            Group->SetVisibility(Name!=TEXT("Trunk")); Group->SetCollisionProfileName(Solid ? TEXT("BlockAll") : TEXT("NoCollision"));Group->SetCanEverAffectNavigation(Solid);
            if(Name==TEXT("Bridge")) for(int32 I=0;I<Group->GetNumMaterials();++I) Group->SetMaterial(I,Material(TEXT("deadwood")));
            Group->RegisterComponent();Chunk->AddInstanceComponent(Group);
        }
        Group->AddInstance(FTransform(Rotation,At-Base,Scale));
    };
    ENorthSite Kind;const FVector Site=PersonaNorth::Site(Seed,K,Kind);
    if(Kind==ENorthSite::Orchard) Plant(TEXT("Orchard"),Site,FRotator::ZeroRotator,FVector(1),false);
    if(Kind==ENorthSite::Spring) Plant(TEXT("Outcrop1"),Site+FVector(0,90,-15),FRotator(0,27,0),FVector(.8),true);
    FRandomStream R(TileSeed(Seed,K));const int Count=R.RandRange(15,44);
    for(int I=0;I<Count;++I)
    {
        FVector P=Base+FVector(R.FRandRange(30,3570),R.FRandRange(30,3570),0);
        if(P.SizeSquared2D()<FMath::Square(710.) || (Kind!=ENorthSite::None && FVector::DistSquared2D(P,Site)<FMath::Square(270.))) continue;
        P.Z+=PersonaNorth::Height(Seed,P.X,P.Y)-4;
        const double Dist=FMath::Abs(RiverDistance(Seed,P.X,P.Y)); if(Dist<520) continue;
        const int Type=R.RandRange(0,17);
        const FName Name=Dist<730 || Type<3 ? (Type%2 ? TEXT("Outcrop0") : TEXT("Outcrop1")) : Type<5 ? TEXT("Windfall") : Type==5 ? TEXT("Snag") : Type<9 ? TEXT("Birch") : FName(*FString::Printf(TEXT("Spruce%d"),Type%4));
        const float S=R.FRandRange(.65f,1.3f);
        const bool Tree=Type>=5 && Dist>=730;
        Plant(Name,P,FRotator(0,R.FRandRange(-180,180),0),FVector(S),!Tree);
        if(Tree) Plant(TEXT("Trunk"),P+FVector(0,0,195*S),FRotator::ZeroRotator,FVector(.33*S,.33*S,3.9*S),true);
    }
    // Crossings repeat along each meandering river, with several possible routes back.
    for(int J=FMath::CeilToInt(Base.Y/5400.); J*5400.<Base.Y+PersonaNorth::TileSize; ++J)
    {
        const double WY=J*5400.,WX=MidX-RiverDistance(Seed,MidX,WY);
        if(WX>=Base.X && WX<Base.X+PersonaNorth::TileSize)
            Plant(TEXT("Bridge"),FVector(WX,WY,Grade+7),FRotator::ZeroRotator,FVector(.75,1,1),true);
    }
    if(D.HasAuthority())
    {
        UpdateNode(D,K,Chunk);
        for(int I=D.NorthDrops.Num()-1;I>=0;--I) if(PersonaNorth::TileAt(D.NorthDrops[I].Transform.GetLocation())==K)
        {
            const auto& Drop=D.NorthDrops[I];
            if(UPersonaLoot::Spawn(GetWorld(),Drop.Item,Drop.Transform)) D.NorthDrops.RemoveAtSwap(I);
        }
    }
}
void APersonaNorthernForest::UnloadTile(APersonaTrialDirector& D,FIntPoint K)
{
    if(D.HasAuthority())
    {
        for(TActorIterator<APersonaProp> It(GetWorld());It;++It)
            if(!It->Holder && !It->bInTransit && It->GetActorLocation().Z<-5000 && PersonaNorth::TileAt(It->GetActorLocation())==K)
            { auto& R=D.NorthDrops.AddDefaulted_GetRef();R.Item=UPersonaLoot::Capture(*It);R.Transform=It->GetActorTransform();It->Destroy(); }
        if(auto N=Nodes.FindRef(K)) N->Destroy();Nodes.Remove(K);
    }
    if(auto Chunk=Chunks.FindRef(K))
    {
        Chunk->Rename(*MakeUniqueObjectName(Chunk->GetOuter(),Chunk->GetClass(),TEXT("RetiredForestTile")).ToString(),nullptr,REN_DontCreateRedirectors|REN_NonTransactional);
        Chunk->Destroy();
    }
    Chunks.Remove(K);
}
void APersonaNorthernForest::UpdateNode(APersonaTrialDirector& D,FIntPoint K,AActor* Chunk)
{
    if(IsValid(Nodes.FindRef(K))) return;
    if(const float* Respawn=D.NorthHarvest.Find(K);Respawn && (*Respawn<0 || *Respawn>D.Run.Forest.Elapsed)) return;
    ENorthSite Kind;FVector P=PersonaNorth::Site(D.Run.Seed,K,Kind);if(Kind==ENorthSite::None) return;
    P.Z+=Kind==ENorthSite::Orchard ? 111 : 20;P.X+=Kind==ENorthSite::Orchard ? 58 : 0;
    auto Node=GetWorld()->SpawnActor<APersonaForestNode>(P,FRotator::ZeroRotator);if(Node){Node->Init(K,Kind);Nodes.Add(K,Node);}
}
void APersonaNorthernForest::RefreshNodes(APersonaTrialDirector& D)
{
    for(const auto& Pair:Chunks) UpdateNode(D,Pair.Key,Pair.Value);
}
void APersonaNorthernForest::EndPlay(const EEndPlayReason::Type Reason)
{
    for(auto& Pair:Nodes) if(IsValid(Pair.Value)) Pair.Value->Destroy();
    for(auto& Pair:Chunks) if(IsValid(Pair.Value)) Pair.Value->Destroy();
    Super::EndPlay(Reason);
}
APersonaForestNode::APersonaForestNode()
{
    bReplicates=true;Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Find"));SetRootComponent(Visual);
    Visual->SetCollisionProfileName(TEXT("BlockAll"));Visual->SetCanEverAffectNavigation(false);
}
void APersonaForestNode::Init(FIntPoint K,ENorthSite Site) {Tile=K;Kind=Site;Refresh();ForceNetUpdate();}
void APersonaForestNode::Refresh()
{
    const TCHAR* Model=Kind==ENorthSite::Orchard ? TEXT("/Game/Persona/North/SM_Apple") :
        Kind==ENorthSite::Spring ? TEXT("/Game/Persona/Survival/SM_Flask") : TEXT("/Game/Persona/Survival/SM_MedCase");
    Visual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Model));Visual->SetRenderCustomDepth(true);Visual->SetCustomDepthStencilValue(2);
}
void APersonaForestNode::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(APersonaForestNode,Tile);DOREPLIFETIME(APersonaForestNode,Kind);
}
FText APersonaForestNode::InteractionLabel_Implementation() const { return FText::FromString(FString(TEXT("E — "))+PersonaNorth::SiteName(Kind)); }
void APersonaForestNode::Interact_Implementation(APersonaCharacter* C)
{
    if(HasAuthority() && C) if(auto D=APersonaTrialDirector::Get(GetWorld()); D && D->NorthWorld) D->NorthWorld->Harvest(*this,*C);
}
void APersonaNorthernForest::Harvest(APersonaForestNode& Node,APersonaCharacter& C)
{
    auto D=APersonaTrialDirector::Get(GetWorld());
    if(!D || !D->HasAuthority() || Nodes.FindRef(Node.Tile)!=&Node || !C.IsAlive() || C.bTransit || !C.Interaction->CanReachLoot(&Node)) return;
    const auto* Player=D->Run.Forest.Player(C.PersistentId);if(!Player || Player->Outcome!=EForestOutcome::Active) return;
    FPersonaInventoryRecord R;const TCHAR* Name=ItemName(Node.Kind);
    R.Definition=FString::Printf(TEXT("/Game/Persona/Items/%s.%s"),Name,Name);R.Quantity=1;
    auto P=UPersonaLoot::Spawn(GetWorld(),R,C.GetActorTransform());if(!P) return;
    if(!C.Interaction->Store(P)) { P->Destroy();return; }
    const bool Renewable=Node.Kind==ENorthSite::Orchard || Node.Kind==ENorthSite::Spring;
    D->NorthHarvest.Add(Node.Tile,Renewable ? D->Run.Forest.Elapsed+420.f : -1.f);
    D->NorthAtlas.Visit(C.GetActorLocation(),Node.Kind);Nodes.Remove(Node.Tile);Node.Destroy();
    D->GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->SaveWorld();
}
