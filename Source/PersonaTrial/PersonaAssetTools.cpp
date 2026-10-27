#include "PersonaAssetTools.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "EngineUtils.h"
#include "Curves/CurveFloat.h"

void UPersonaAssetTools::EnsureAttachmentSockets(USkeleton* Skeleton)
{
#if WITH_EDITOR
    if (!Skeleton) return;
    const TPair<FName,FName> Sockets[] = {
        {TEXT("Grip_R"),TEXT("hand_r")}, {TEXT("Grip_L"),TEXT("hand_l")}, {TEXT("Voice_Head"),TEXT("head")}
    };
    for (const auto& Entry : Sockets)
    {
        if (Skeleton->FindSocket(Entry.Key)) continue;
        Skeleton->Modify();
        auto Socket=NewObject<USkeletalMeshSocket>(Skeleton,NAME_None,RF_Transactional);
        Socket->SocketName=Entry.Key; Socket->BoneName=Entry.Value;
        Skeleton->Sockets.Add(Socket);
        Skeleton->MarkPackageDirty();
    }
#endif
}
void UPersonaAssetTools::ConfigureRecoilCurve(UCurveFloat* Curve,int32 Profile)
{
#if WITH_EDITOR
    if(!Curve) return;
    Curve->Modify(); Curve->FloatCurve.Reset();
    const float Times[3][5]={{0,.02f,.065f,.14f,.28f},{0,.03f,.10f,.22f,.32f},{0,.03f,.075f,.16f,.30f}};
    const float Values[3][5]={{0,1,.7f,.2f,0},{0,1,.55f,.05f,0},{0,1,.65f,.3f,0}};
    Profile=FMath::Clamp(Profile,0,2);
    for(int32 I=0;I<5;++I) Curve->FloatCurve.SetKeyInterpMode(Curve->FloatCurve.AddKey(Times[Profile][I],Values[Profile][I]),RCIM_Cubic);
    Curve->FloatCurve.AutoSetTangents(); Curve->MarkPackageDirty();
#endif
}
bool UPersonaAssetTools::BuildNavigation(UWorld* World)
{
#if WITH_EDITOR
    auto Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!Nav) return false;
    FlushAsyncLoading();
    // Commandlets do not advance the editor tick which normally releases this
    // lock after all map assets finish loading.
    Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock,
        UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
    Nav->InitializeLevelCollisions();
    for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It) Nav->OnNavigationBoundsUpdated(*It);
    Nav->Build();
    auto Data=Nav->GetDefaultNavDataInstance(FNavigationSystem::Create);
    if (Data) { Data->EnsureBuildCompletion(); Data->MarkPackageDirty(); }
    FNavLocation Point;
    bool Valid=false;
    for(TActorIterator<ANavMeshBoundsVolume> It(World);It;++It)
        Valid|=Nav->ProjectPointToNavigation(It->GetActorLocation()+FVector(400,0,0),Point,FVector(1500,1500,2000));
    UE_LOG(LogTemp,Display,TEXT("PERSONA NAVIGATION: world=%s projected=%d"),*GetNameSafe(World),Valid);
    return Valid;
#else
    return false;
#endif
}
