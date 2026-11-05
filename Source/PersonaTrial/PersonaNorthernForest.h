#pragma once
#include "CoreMinimal.h"
#include "PersonaInteraction.h"
#include "ProceduralMeshComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "PersonaNorthernForest.generated.h"

class APersonaTrialDirector;
class APersonaForestNode;

UCLASS()
class UPersonaForestTerrain : public UProceduralMeshComponent
{
    GENERATED_BODY()
public:
    UPersonaForestTerrain(const FObjectInitializer& ObjectInitializer);
    TArray<FVector> NavVertices;
    TArray<int32> NavIndices;
    virtual bool DoCustomNavigableGeometryExport(FNavigableGeometryExport& Export) const override;
};

UENUM()
enum class ENorthSite : uint8 { None, Orchard, Spring, Scrap, Cloth, Shells };

USTRUCT()
struct FNorthChartTile : public FFastArraySerializerItem
{
    GENERATED_BODY()
    UPROPERTY() FIntPoint Tile = FIntPoint::ZeroValue;
    UPROPERTY() uint64 Seen = 0;
    UPROPERTY() ENorthSite Landmark = ENorthSite::None;
};

USTRUCT()
struct FNorthAtlas : public FFastArraySerializer
{
    GENERATED_BODY()
    UPROPERTY() TArray<FNorthChartTile> Tiles;
    bool NetDeltaSerialize(FNetDeltaSerializeInfo& Info) { return FastArrayDeltaSerialize<FNorthChartTile,FNorthAtlas>(Tiles,Info,*this); }
    void Visit(const FVector& Position, ENorthSite Landmark);
};
template<> struct TStructOpsTypeTraits<FNorthAtlas> : TStructOpsTypeTraitsBase2<FNorthAtlas>
{ enum { WithNetDeltaSerializer=true }; };

USTRUCT()
struct FNorthDrop
{
    GENERATED_BODY()
    UPROPERTY() FTransform Transform;
    UPROPERTY() FPersonaInventoryRecord Item;
};

namespace PersonaNorth
{
    constexpr int32 TileSize=3600;
    FIntPoint TileAt(const FVector& Position);
    FVector Origin(FIntPoint Tile);
    float Height(int32 Seed,double X,double Y);
    FVector Site(int32 Seed,FIntPoint Tile,ENorthSite& Kind);
    const TCHAR* SiteName(ENorthSite Kind);
}

// Deterministic scenery is local on every peer. Only harvest and item ownership are authoritative.
UCLASS()
class APersonaNorthernForest : public AActor
{
    GENERATED_BODY()
public:
    APersonaNorthernForest();
    bool Sync(APersonaTrialDirector& Director, const FVector& Center);
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void RefreshNodes(APersonaTrialDirector& Director);
    void Harvest(APersonaForestNode& Node, APersonaCharacter& Player);
private:
    void LoadTile(APersonaTrialDirector& Director,FIntPoint Tile);
    void UnloadTile(APersonaTrialDirector& Director,FIntPoint Tile);
    void UpdateNode(APersonaTrialDirector& Director,FIntPoint Tile,AActor* Chunk);
    UPROPERTY() TMap<FIntPoint,TObjectPtr<AActor>> Chunks;
    UPROPERTY() TMap<FIntPoint,TObjectPtr<APersonaForestNode>> Nodes;
    UPROPERTY() TMap<FName,TObjectPtr<UStaticMesh>> Kit;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Sky;
    UPROPERTY() TObjectPtr<class UPointLightComponent> Daylight;
    UPROPERTY() TObjectPtr<class UPostProcessComponent> Weather;
    FIntPoint Window=FIntPoint(MAX_int32,MAX_int32);
};

UCLASS()
class APersonaForestNode : public AActor, public IPersonaInteractable
{
    GENERATED_BODY()
public:
    APersonaForestNode();
    void Init(FIntPoint InTile,ENorthSite InKind);
    virtual FText InteractionLabel_Implementation() const override;
    virtual void Interact_Implementation(APersonaCharacter* C) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(Replicated) FIntPoint Tile=FIntPoint::ZeroValue;
    UPROPERTY(ReplicatedUsing=Refresh) ENorthSite Kind=ENorthSite::None;
    UFUNCTION() void Refresh();
private:
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Visual;
};
