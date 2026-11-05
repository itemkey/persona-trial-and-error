#include "PersonaAssetTools.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "EngineUtils.h"
#include "Curves/CurveFloat.h"
#include "PersonaInteraction.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#if WITH_EDITOR
#include "CanvasTypes.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ThumbnailRendering/StaticMeshThumbnailRenderer.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"
#endif

UTexture2D* UPersonaAssetTools::BakeItemIcon(UPersonaItemDefinition* Item,const FString& PackagePath)
{
#if WITH_EDITOR
    if(!Item || !Item->Mesh) return nullptr;
    FAssetCompilingManager::Get().FinishAllCompilation();
    if(GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    FlushRenderingCommands();
    auto Target=NewObject<UTextureRenderTarget2D>();Target->InitCustomFormat(128,128,PF_B8G8R8A8,true);Target->UpdateResourceImmediate(true);
    auto Resource=Target->GameThread_GetRenderTargetResource();
    FCanvas Canvas(Resource,nullptr,FGameTime::GetTimeSinceAppStart(),GMaxRHIFeatureLevel);Canvas.Clear(FLinearColor(.025f,.028f,.027f));
    auto Renderer=NewObject<UStaticMeshThumbnailRenderer>();
    Renderer->Draw(Item->Mesh,0,0,128,128,Resource,&Canvas,false);
    Canvas.Flush_GameThread();FlushRenderingCommands();
    TArray<FColor> Colors;if(!Resource->ReadPixels(Colors) || Colors.Num()!=128*128) return nullptr;
    for(auto& Color:Colors) Color=(FLinearColor::FromSRGBColor(Color)*3.f).ToFColorSRGB();
    TArray<uint8> Pixels;Pixels.SetNumUninitialized(Colors.Num()*4);FMemory::Memcpy(Pixels.GetData(),Colors.GetData(),Pixels.Num());
    for(int I=3;I<Pixels.Num();I+=4) Pixels[I]=255;
    bool Varied=false;for(int I=4;I<Pixels.Num();I+=4) if(Pixels[I]!=Pixels[0] || Pixels[I+1]!=Pixels[1] || Pixels[I+2]!=Pixels[2]) { Varied=true;break; }
    if(!Varied) { UE_LOG(LogTemp,Error,TEXT("Empty item thumbnail: %s"),*Item->GetName());return nullptr; }
    const FString Name=FPackageName::GetLongPackageAssetName(PackagePath);
    auto Package=CreatePackage(*PackagePath);
    auto Icon=FindObject<UTexture2D>(Package,*Name);
    if(!Icon) { Icon=NewObject<UTexture2D>(Package,*Name,RF_Public|RF_Standalone);FAssetRegistryModule::AssetCreated(Icon); }
    Icon->Source.Init(128,128,1,1,TSF_BGRA8,Pixels.GetData());
    Icon->CompressionSettings=TC_EditorIcon;Icon->LODGroup=TEXTUREGROUP_UI;Icon->MipGenSettings=TMGS_NoMipmaps;Icon->SRGB=true;
    Icon->PostEditChange();Icon->MarkPackageDirty();Item->Icon=Icon;Item->MarkPackageDirty();return Icon;
#else
    return nullptr;
#endif
}

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
    const float Times[3][5]={{0,.025f,.09f,.25f,.55f},{0,.025f,.12f,.3f,.55f},{0,.025f,.09f,.25f,.55f}};
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
