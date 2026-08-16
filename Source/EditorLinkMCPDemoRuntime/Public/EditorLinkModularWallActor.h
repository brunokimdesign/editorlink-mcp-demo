#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EditorLinkModularWallActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class USceneComponent;
class UStaticMesh;

UCLASS(Blueprintable)
class EDITORLINKMCPDEMORUNTIME_API AEditorLinkModularWallActor : public AActor
{
	GENERATED_BODY()

public:
	AEditorLinkModularWallActor();

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Modular Wall")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Modular Wall")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> WallInstances;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular Wall")
	TObjectPtr<UStaticMesh> WallMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular Wall", meta = (ClampMin = "1", UIMin = "1"))
	int32 PieceCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular Wall", meta = (ClampMin = "0.0", UIMin = "0.0"))
	double Gap = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular Wall")
	bool bCenterWall = true;
};

