#include "EditorLinkMCPDemoCommands.h"

#include "Algo/Count.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/ActorComponent.h"
#include "ContentBrowserModule.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/Selection.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/PlatformFileManager.h"
#include "HighResScreenshot.h"
#include "IContentBrowserSingleton.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace
{
	constexpr int32 DefaultLimit = 100;
	constexpr int32 MaxLimit = 500;

	TSharedPtr<FJsonValue> JsonString(const FString& Value)
	{
		return MakeShared<FJsonValueString>(Value);
	}

	TSharedPtr<FJsonValue> JsonObjectValue(const TSharedPtr<FJsonObject>& Value)
	{
		return MakeShared<FJsonValueObject>(Value);
	}

	int32 GetLimit(const TSharedPtr<FJsonObject>& Params, int32 DefaultValue = DefaultLimit)
	{
		double Value = DefaultValue;
		Params->TryGetNumberField(TEXT("limit"), Value);
		return FMath::Clamp(static_cast<int32>(Value), 1, MaxLimit);
	}

	bool RequiredString(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, FString& Out, FString& Error)
	{
		if (!Params->TryGetStringField(Field, Out) || Out.TrimStartAndEnd().IsEmpty())
		{
			Error = FString::Printf(TEXT("The '%s' parameter is required."), Field);
			return false;
		}
		Out = Out.TrimStartAndEnd();
		return true;
	}

	bool GetBool(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, bool DefaultValue)
	{
		bool Value = DefaultValue;
		Params->TryGetBoolField(Field, Value);
		return Value;
	}

	FVector ReadVector(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, const FVector& DefaultValue)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Params->TryGetObjectField(Field, Object) || !Object || !Object->IsValid())
		{
			return DefaultValue;
		}
		double X = DefaultValue.X;
		double Y = DefaultValue.Y;
		double Z = DefaultValue.Z;
		(*Object)->TryGetNumberField(TEXT("x"), X);
		(*Object)->TryGetNumberField(TEXT("y"), Y);
		(*Object)->TryGetNumberField(TEXT("z"), Z);
		return FVector(X, Y, Z);
	}

	FRotator ReadRotator(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, const FRotator& DefaultValue)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Params->TryGetObjectField(Field, Object) || !Object || !Object->IsValid())
		{
			return DefaultValue;
		}
		double Pitch = DefaultValue.Pitch;
		double Yaw = DefaultValue.Yaw;
		double Roll = DefaultValue.Roll;
		(*Object)->TryGetNumberField(TEXT("pitch"), Pitch);
		(*Object)->TryGetNumberField(TEXT("yaw"), Yaw);
		(*Object)->TryGetNumberField(TEXT("roll"), Roll);
		return FRotator(Pitch, Yaw, Roll);
	}

	TSharedPtr<FJsonObject> VectorObject(const FVector& Value)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("x"), Value.X);
		Result->SetNumberField(TEXT("y"), Value.Y);
		Result->SetNumberField(TEXT("z"), Value.Z);
		return Result;
	}

	TSharedPtr<FJsonObject> RotatorObject(const FRotator& Value)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("pitch"), Value.Pitch);
		Result->SetNumberField(TEXT("yaw"), Value.Yaw);
		Result->SetNumberField(TEXT("roll"), Value.Roll);
		return Result;
	}

	FString NormalizeObjectPath(const FString& Path)
	{
		if (!Path.StartsWith(TEXT("/")) || Path.Contains(TEXT(".")))
		{
			return Path;
		}
		return Path + TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
	}

	UObject* LoadEditorObject(const FString& Path)
	{
		return StaticLoadObject(UObject::StaticClass(), nullptr, *NormalizeObjectPath(Path));
	}

	UBlueprint* LoadBlueprint(const FString& Path)
	{
		return Cast<UBlueprint>(LoadEditorObject(Path));
	}

	USkeleton* LoadSkeleton(const FString& Path)
	{
		return Cast<USkeleton>(LoadEditorObject(Path));
	}

	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	AActor* FindActor(const FString& Identifier)
	{
		UWorld* World = EditorWorld();
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && (Actor->GetPathName().Equals(Identifier, ESearchCase::IgnoreCase) ||
				Actor->GetActorLabel().Equals(Identifier, ESearchCase::IgnoreCase) ||
				Actor->GetName().Equals(Identifier, ESearchCase::IgnoreCase)))
			{
				return Actor;
			}
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> PropertyObject(FProperty* Property, const void* Container)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), Property->GetName());
		Result->SetStringField(TEXT("display_name"), Property->GetDisplayNameText().ToString());
		Result->SetStringField(TEXT("type"), Property->GetCPPType());
		Result->SetBoolField(TEXT("editable"), Property->HasAnyPropertyFlags(CPF_Edit));
		FString Exported;
		const void* ValueAddress = Property->ContainerPtrToValuePtr<void>(Container);
		Property->ExportTextItem_Direct(Exported, ValueAddress, nullptr, nullptr, PPF_None);
		Result->SetStringField(TEXT("value"), Exported.Left(4096));
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> InspectProperties(UObject* Object, int32 Limit)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		if (!Object)
		{
			return Values;
		}
		for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It && Values.Num() < Limit; ++It)
		{
			FProperty* Property = *It;
			if (Property && !Property->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient))
			{
				Values.Add(JsonObjectValue(PropertyObject(Property, Object)));
			}
		}
		return Values;
	}

	TSharedPtr<FJsonObject> ActorObject(AActor* Actor, bool bIncludeProperties, int32 PropertyLimit)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("label"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("name"), Actor->GetName());
		Result->SetStringField(TEXT("path"), Actor->GetPathName());
		Result->SetStringField(TEXT("class"), Actor->GetClass()->GetPathName());
		Result->SetObjectField(TEXT("location"), VectorObject(Actor->GetActorLocation()));
		Result->SetObjectField(TEXT("rotation"), RotatorObject(Actor->GetActorRotation()));
		Result->SetObjectField(TEXT("scale"), VectorObject(Actor->GetActorScale3D()));
		Result->SetBoolField(TEXT("hidden"), Actor->IsHiddenEd());

		TArray<TSharedPtr<FJsonValue>> Components;
		TInlineComponentArray<UActorComponent*> ActorComponents(Actor);
		for (UActorComponent* Component : ActorComponents)
		{
			if (!Component)
			{
				continue;
			}
			TSharedPtr<FJsonObject> ComponentObject = MakeShared<FJsonObject>();
			ComponentObject->SetStringField(TEXT("name"), Component->GetName());
			ComponentObject->SetStringField(TEXT("path"), Component->GetPathName());
			ComponentObject->SetStringField(TEXT("class"), Component->GetClass()->GetPathName());
			ComponentObject->SetBoolField(TEXT("registered"), Component->IsRegistered());
			Components.Add(JsonObjectValue(ComponentObject));
		}
		Result->SetArrayField(TEXT("components"), Components);
		if (bIncludeProperties)
		{
			Result->SetArrayField(TEXT("properties"), InspectProperties(Actor, PropertyLimit));
		}
		return Result;
	}

	UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& GraphName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
			{
				return Graph;
			}
		}
		return nullptr;
	}

	UEdGraphNode* FindNode(UEdGraph* Graph, const FString& GuidString)
	{
		FGuid Guid;
		if (!Graph || !FGuid::Parse(GuidString, Guid))
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == Guid)
			{
				return Node;
			}
		}
		return nullptr;
	}

	UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction && Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> PinObject(UEdGraphPin* Pin)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), Pin->PinName.ToString());
		Result->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		Result->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
		Result->SetStringField(TEXT("subcategory"), Pin->PinType.PinSubCategory.ToString());
		Result->SetStringField(TEXT("default_value"), Pin->DefaultValue);
		Result->SetBoolField(TEXT("hidden"), Pin->bHidden);
		TArray<TSharedPtr<FJsonValue>> Links;
		for (UEdGraphPin* Linked : Pin->LinkedTo)
		{
			if (!Linked || !Linked->GetOwningNode())
			{
				continue;
			}
			TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
			Link->SetStringField(TEXT("node_guid"), Linked->GetOwningNode()->NodeGuid.ToString());
			Link->SetStringField(TEXT("pin"), Linked->PinName.ToString());
			Links.Add(JsonObjectValue(Link));
		}
		Result->SetArrayField(TEXT("links"), Links);
		return Result;
	}

	TSharedPtr<FJsonObject> GraphObject(UEdGraph* Graph, int32 NodeLimit)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), Graph->GetName());
		Result->SetStringField(TEXT("class"), Graph->GetClass()->GetPathName());
		Result->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		TArray<TSharedPtr<FJsonValue>> Nodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || Nodes.Num() >= NodeLimit)
			{
				continue;
			}
			TSharedPtr<FJsonObject> NodeObject = MakeShared<FJsonObject>();
			NodeObject->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
			NodeObject->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
			NodeObject->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
			NodeObject->SetNumberField(TEXT("x"), Node->NodePosX);
			NodeObject->SetNumberField(TEXT("y"), Node->NodePosY);
			const int32 VisiblePins = Algo::CountIf(Node->Pins, [](const UEdGraphPin* Pin) { return Pin && !Pin->bHidden; });
			int32 LongestValue = Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Len();
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !Pin->bHidden) LongestValue = FMath::Max(LongestValue, Pin->DefaultValue.Len());
			}
			const int32 EstimatedWidth = Node->NodeWidth > 0 ? Node->NodeWidth : FMath::Clamp(220 + FMath::Max(0, LongestValue - 12) * 5, 240, 520);
			const int32 EstimatedHeight = Node->NodeHeight > 0 ? Node->NodeHeight : FMath::Clamp(70 + VisiblePins * 16, 96, 420);
			NodeObject->SetNumberField(TEXT("estimated_width"), EstimatedWidth);
			NodeObject->SetNumberField(TEXT("estimated_height"), EstimatedHeight);
			TArray<TSharedPtr<FJsonValue>> Pins;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin)
				{
					Pins.Add(JsonObjectValue(PinObject(Pin)));
				}
			}
			NodeObject->SetArrayField(TEXT("pins"), Pins);
			Nodes.Add(JsonObjectValue(NodeObject));
		}
		Result->SetArrayField(TEXT("nodes"), Nodes);
		Result->SetBoolField(TEXT("truncated"), Graph->Nodes.Num() > Nodes.Num());
		return Result;
	}

	UEdGraphNode* CreateGraphNode(UEdGraph* Graph, UClass* NodeClass, int32 X, int32 Y)
	{
		if (!Graph || !NodeClass || !NodeClass->IsChildOf(UEdGraphNode::StaticClass()))
		{
			return nullptr;
		}
		Graph->Modify();
		UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional);
		Graph->AddNode(Node, true, false);
		Node->CreateNewGuid();
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		return Node;
	}

	int32 EstimatedNodeWidth(const UEdGraphNode* Node)
	{
		if (!Node) return 240;
		if (Node->NodeWidth > 0) return Node->NodeWidth;
		int32 LongestValue = Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Len();
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && !Pin->bHidden) LongestValue = FMath::Max(LongestValue, Pin->DefaultValue.Len());
		}
		return FMath::Clamp(220 + FMath::Max(0, LongestValue - 12) * 5, 240, 520);
	}

	int32 EstimatedNodeHeight(const UEdGraphNode* Node)
	{
		if (!Node) return 96;
		if (Node->NodeHeight > 0) return Node->NodeHeight;
		const int32 VisiblePins = Algo::CountIf(Node->Pins, [](const UEdGraphPin* Pin) { return Pin && !Pin->bHidden; });
		return FMath::Clamp(70 + VisiblePins * 16, 96, 420);
	}

	bool NodeAreasOverlap(const UEdGraphNode* A, int32 AX, int32 AY, const UEdGraphNode* B, int32 HorizontalGap, int32 VerticalGap)
	{
		const int32 AW = EstimatedNodeWidth(A);
		const int32 AH = EstimatedNodeHeight(A);
		const int32 BW = EstimatedNodeWidth(B);
		const int32 BH = EstimatedNodeHeight(B);
		return AX < B->NodePosX + BW + HorizontalGap && AX + AW + HorizontalGap > B->NodePosX &&
			AY < B->NodePosY + BH + VerticalGap && AY + AH + VerticalGap > B->NodePosY;
	}

	bool PlaceNodeWithoutOverlap(UEdGraph* Graph, UEdGraphNode* Node, int32 DesiredX, int32 DesiredY, int32 HorizontalGap, int32 VerticalGap)
	{
		if (!Graph || !Node) return false;
		int32 CandidateX = FMath::GridSnap(DesiredX, 16);
		const int32 CandidateY = FMath::GridSnap(DesiredY, 16);
		bool bMoved = false;
		for (int32 Attempt = 0; Attempt < 256; ++Attempt)
		{
			const UEdGraphNode* Collision = nullptr;
			for (const UEdGraphNode* Existing : Graph->Nodes)
			{
				if (Existing && Existing != Node && NodeAreasOverlap(Node, CandidateX, CandidateY, Existing, HorizontalGap, VerticalGap))
				{
					Collision = Existing;
					break;
				}
			}
			if (!Collision)
			{
				Node->NodePosX = CandidateX;
				Node->NodePosY = CandidateY;
				return bMoved;
			}
			CandidateX = FMath::GridSnap(Collision->NodePosX + EstimatedNodeWidth(Collision) + HorizontalGap, 16);
			bMoved = true;
		}
		Node->NodePosX = CandidateX;
		Node->NodePosY = CandidateY;
		return bMoved;
	}

	TSharedPtr<FJsonObject> BlueprintSummary(UBlueprint* Blueprint, int32 NodeLimit)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), Blueprint->GetName());
		Result->SetStringField(TEXT("path"), Blueprint->GetPathName());
		Result->SetStringField(TEXT("class"), Blueprint->GetClass()->GetPathName());
		Result->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : FString());
		Result->SetStringField(TEXT("status"), StaticEnum<EBlueprintStatus>()->GetNameStringByValue(Blueprint->Status));
		Result->SetBoolField(TEXT("dirty"), Blueprint->GetOutermost()->IsDirty());

		TArray<TSharedPtr<FJsonValue>> Variables;
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Variable.VarName.ToString());
			Item->SetStringField(TEXT("category"), Variable.VarType.PinCategory.ToString());
			Item->SetStringField(TEXT("subcategory"), Variable.VarType.PinSubCategory.ToString());
			Variables.Add(JsonObjectValue(Item));
		}
		Result->SetArrayField(TEXT("variables"), Variables);

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		TArray<TSharedPtr<FJsonValue>> GraphValues;
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph)
			{
				GraphValues.Add(JsonObjectValue(GraphObject(Graph, NodeLimit)));
			}
		}
		Result->SetArrayField(TEXT("graphs"), GraphValues);

		TArray<TSharedPtr<FJsonValue>> Components;
		if (Blueprint->SimpleConstructionScript)
		{
			for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (!Node)
				{
					continue;
				}
				TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("variable_name"), Node->GetVariableName().ToString());
				Item->SetStringField(TEXT("component_class"), Node->ComponentClass ? Node->ComponentClass->GetPathName() : FString());
				Components.Add(JsonObjectValue(Item));
			}
		}
		Result->SetArrayField(TEXT("components"), Components);
		return Result;
	}

	FEdGraphPinType PinTypeFromName(const FString& TypeName, const FString& ObjectClassPath)
	{
		FEdGraphPinType PinType;
		if (TypeName.Equals(TEXT("bool"), ESearchCase::IgnoreCase)) PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		else if (TypeName.Equals(TEXT("byte"), ESearchCase::IgnoreCase)) PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		else if (TypeName.Equals(TEXT("int"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("integer"), ESearchCase::IgnoreCase)) PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		else if (TypeName.Equals(TEXT("int64"), ESearchCase::IgnoreCase)) PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		else if (TypeName.Equals(TEXT("float"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("double"), ESearchCase::IgnoreCase)) PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		else if (TypeName.Equals(TEXT("name"), ESearchCase::IgnoreCase)) PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		else if (TypeName.Equals(TEXT("text"), ESearchCase::IgnoreCase)) PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
		else if (TypeName.Equals(TEXT("string"), ESearchCase::IgnoreCase)) PinType.PinCategory = UEdGraphSchema_K2::PC_String;
		else if (TypeName.Equals(TEXT("object"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			PinType.PinSubCategoryObject = LoadObject<UClass>(nullptr, *ObjectClassPath);
		}
		else if (TypeName.Equals(TEXT("class"), ESearchCase::IgnoreCase))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
			PinType.PinSubCategoryObject = LoadObject<UClass>(nullptr, *ObjectClassPath);
		}
		else
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_String;
		}
		return PinType;
	}

	TSharedPtr<FJsonObject> SkeletonSummary(USkeleton* Skeleton)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), Skeleton->GetName());
		Result->SetStringField(TEXT("path"), Skeleton->GetPathName());
		Result->SetBoolField(TEXT("dirty"), Skeleton->GetOutermost()->IsDirty());

		const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
		TArray<TSharedPtr<FJsonValue>> Bones;
		for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
		{
			TSharedPtr<FJsonObject> Bone = MakeShared<FJsonObject>();
			Bone->SetStringField(TEXT("name"), Reference.GetBoneName(Index).ToString());
			const int32 ParentIndex = Reference.GetParentIndex(Index);
			Bone->SetStringField(TEXT("parent"), ParentIndex != INDEX_NONE ? Reference.GetBoneName(ParentIndex).ToString() : FString());
			Bones.Add(JsonObjectValue(Bone));
		}
		Result->SetArrayField(TEXT("bones"), Bones);

		TArray<TSharedPtr<FJsonValue>> Sockets;
		for (USkeletalMeshSocket* Socket : Skeleton->Sockets)
		{
			if (!Socket)
			{
				continue;
			}
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Socket->SocketName.ToString());
			Item->SetStringField(TEXT("bone"), Socket->BoneName.ToString());
			Item->SetObjectField(TEXT("location"), VectorObject(Socket->RelativeLocation));
			Item->SetObjectField(TEXT("rotation"), RotatorObject(Socket->RelativeRotation));
			Item->SetObjectField(TEXT("scale"), VectorObject(Socket->RelativeScale));
			Sockets.Add(JsonObjectValue(Item));
		}
		Result->SetArrayField(TEXT("sockets"), Sockets);

		TArray<TSharedPtr<FJsonValue>> SlotGroups;
		for (const FAnimSlotGroup& Group : Skeleton->GetSlotGroups())
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("group"), Group.GroupName.ToString());
			TArray<TSharedPtr<FJsonValue>> Slots;
			for (const FName Slot : Group.SlotNames)
			{
				Slots.Add(JsonString(Slot.ToString()));
			}
			Item->SetArrayField(TEXT("slots"), Slots);
			SlotGroups.Add(JsonObjectValue(Item));
		}
		Result->SetArrayField(TEXT("slot_groups"), SlotGroups);
		return Result;
	}
}

TSharedPtr<FJsonObject> FEditorLinkMCPDemoCommands::MakeSuccess(const TSharedPtr<FJsonObject>& Data)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("data"), Data.IsValid() ? Data : MakeShared<FJsonObject>());
	return Result;
}

TSharedPtr<FJsonObject> FEditorLinkMCPDemoCommands::MakeError(const FString& Error, const FString& Code)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), Error);
	Result->SetStringField(TEXT("code"), Code);
	return Result;
}

TSharedPtr<FJsonObject> FEditorLinkMCPDemoCommands::Execute(const FString& Command, const TSharedPtr<FJsonObject>& Params)
{
	if (Command == TEXT("ping"))
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("server"), TEXT("EditorLink MCP Demo"));
		Data->SetStringField(TEXT("version"), TEXT("0.1.4-demo"));
		Data->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
		Data->SetBoolField(TEXT("editor_ready"), EditorWorld() != nullptr);
		return MakeSuccess(Data);
	}

	if (Command == TEXT("list_capabilities"))
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("edition"), TEXT("Demo"));
		Data->SetStringField(TEXT("transport"), TEXT("MCP STDIO to authenticated loopback bridge"));
		TArray<TSharedPtr<FJsonValue>> Groups;
		for (const TCHAR* Group : { TEXT("project"), TEXT("assets"), TEXT("actors"), TEXT("blueprints"), TEXT("animation"), TEXT("skeleton"), TEXT("verification") })
		{
			Groups.Add(JsonString(Group));
		}
		Data->SetArrayField(TEXT("tool_groups"), Groups);
		Data->SetBoolField(TEXT("raw_python_exposed"), false);
		Data->SetBoolField(TEXT("save_is_explicit"), true);
		Data->SetBoolField(TEXT("undo_supported"), true);
		return MakeSuccess(Data);
	}

	if (Command == TEXT("get_editor_state"))
	{
		UWorld* World = EditorWorld();
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("project_name"), FApp::GetProjectName());
		Data->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
		Data->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
		Data->SetStringField(TEXT("world"), World ? World->GetPathName() : FString());
		Data->SetBoolField(TEXT("playing"), GEditor && GEditor->PlayWorld != nullptr);

		TArray<TSharedPtr<FJsonValue>> Actors;
		if (GEditor)
		{
			for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
			{
				if (AActor* Actor = Cast<AActor>(*It))
				{
					Actors.Add(JsonObjectValue(ActorObject(Actor, false, 0)));
				}
			}
		}
		Data->SetArrayField(TEXT("selected_actors"), Actors);

		TArray<FAssetData> SelectedAssets;
		FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		ContentBrowser.Get().GetSelectedAssets(SelectedAssets);
		TArray<TSharedPtr<FJsonValue>> Assets;
		for (const FAssetData& Asset : SelectedAssets)
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Asset.AssetName.ToString());
			Item->SetStringField(TEXT("object_path"), Asset.GetSoftObjectPath().ToString());
			Item->SetStringField(TEXT("class"), Asset.AssetClassPath.ToString());
			Assets.Add(JsonObjectValue(Item));
		}
		Data->SetArrayField(TEXT("selected_assets"), Assets);
		return MakeSuccess(Data);
	}

	if (Command == TEXT("search_assets"))
	{
		FString Query;
		Params->TryGetStringField(TEXT("query"), Query);
		FString ClassFilter;
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		const int32 Limit = GetLimit(Params);
		FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> AllAssets;
		RegistryModule.Get().GetAllAssets(AllAssets, true);
		TArray<TSharedPtr<FJsonValue>> Results;
		for (const FAssetData& Asset : AllAssets)
		{
			if (Results.Num() >= Limit)
			{
				break;
			}
			const FString Searchable = Asset.AssetName.ToString() + TEXT(" ") + Asset.PackageName.ToString();
			if ((!Query.IsEmpty() && !Searchable.Contains(Query, ESearchCase::IgnoreCase)) ||
				(!ClassFilter.IsEmpty() && !Asset.AssetClassPath.ToString().Contains(ClassFilter, ESearchCase::IgnoreCase)))
			{
				continue;
			}
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Asset.AssetName.ToString());
			Item->SetStringField(TEXT("package"), Asset.PackageName.ToString());
			Item->SetStringField(TEXT("object_path"), Asset.GetSoftObjectPath().ToString());
			Item->SetStringField(TEXT("class"), Asset.AssetClassPath.ToString());
			Results.Add(JsonObjectValue(Item));
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("assets"), Results);
		Data->SetNumberField(TEXT("returned"), Results.Num());
		Data->SetBoolField(TEXT("truncated"), Results.Num() >= Limit);
		return MakeSuccess(Data);
	}

	if (Command == TEXT("inspect_asset"))
	{
		FString Path, Error;
		if (!RequiredString(Params, TEXT("asset_path"), Path, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		UObject* Object = LoadEditorObject(Path);
		if (!Object) return MakeError(TEXT("Asset could not be loaded: ") + Path, TEXT("asset_not_found"));
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("name"), Object->GetName());
		Data->SetStringField(TEXT("path"), Object->GetPathName());
		Data->SetStringField(TEXT("class"), Object->GetClass()->GetPathName());
		Data->SetStringField(TEXT("package"), Object->GetOutermost()->GetName());
		Data->SetBoolField(TEXT("dirty"), Object->GetOutermost()->IsDirty());
		Data->SetArrayField(TEXT("properties"), InspectProperties(Object, GetLimit(Params, 150)));
		return MakeSuccess(Data);
	}

	if (Command == TEXT("duplicate_asset") || Command == TEXT("rename_asset") || Command == TEXT("delete_asset") || Command == TEXT("save_asset"))
	{
		FString Path, Error;
		if (!RequiredString(Params, TEXT("asset_path"), Path, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		UEditorAssetSubsystem* Assets = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
		if (!Assets) return MakeError(TEXT("Editor Asset Subsystem is unavailable."), TEXT("subsystem_unavailable"));
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		if (Command == TEXT("duplicate_asset"))
		{
			FString Destination;
			if (!RequiredString(Params, TEXT("destination_path"), Destination, Error)) return MakeError(Error, TEXT("invalid_parameters"));
			UObject* Duplicate = Assets->DuplicateAsset(Path, Destination);
			if (!Duplicate) return MakeError(TEXT("Asset duplication failed."));
			Data->SetStringField(TEXT("new_asset"), Duplicate->GetPathName());
		}
		else if (Command == TEXT("rename_asset"))
		{
			FString Destination;
			if (!RequiredString(Params, TEXT("destination_path"), Destination, Error)) return MakeError(Error, TEXT("invalid_parameters"));
			if (!Assets->RenameAsset(Path, Destination)) return MakeError(TEXT("Asset rename failed."));
			Data->SetStringField(TEXT("new_path"), Destination);
		}
		else if (Command == TEXT("delete_asset"))
		{
			if (!GetBool(Params, TEXT("confirm"), false)) return MakeError(TEXT("Set confirm=true to permanently delete an asset."), TEXT("confirmation_required"));
			if (!Assets->DeleteAsset(Path)) return MakeError(TEXT("Asset deletion failed."));
			Data->SetBoolField(TEXT("deleted"), true);
		}
		else
		{
			UObject* Object = LoadEditorObject(Path);
			if (!Object) return MakeError(TEXT("Asset could not be loaded."), TEXT("asset_not_found"));
			const bool bSaved = Assets->SaveLoadedAsset(Object, false);
			Data->SetBoolField(TEXT("saved"), bSaved);
			if (!bSaved) return MakeError(TEXT("Asset save failed."));
		}
		return MakeSuccess(Data);
	}

	if (Command == TEXT("import_asset"))
	{
		FString SourceFile, DestinationPath, Error;
		if (!RequiredString(Params, TEXT("source_file"), SourceFile, Error) || !RequiredString(Params, TEXT("destination_path"), DestinationPath, Error))
		{
			return MakeError(Error, TEXT("invalid_parameters"));
		}
		if (!FPaths::FileExists(SourceFile)) return MakeError(TEXT("Source file does not exist."), TEXT("file_not_found"));
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = SourceFile;
		Task->DestinationPath = DestinationPath;
		Task->bAutomated = true;
		Task->bSave = false;
		Task->bReplaceExisting = GetBool(Params, TEXT("replace_existing"), false);
		TArray<UAssetImportTask*> Tasks{ Task };
		FAssetToolsModule::GetModule().Get().ImportAssetTasks(Tasks);
		TArray<TSharedPtr<FJsonValue>> Imported;
		for (const FString& ImportedPath : Task->ImportedObjectPaths)
		{
			Imported.Add(JsonString(ImportedPath));
		}
		if (Imported.IsEmpty()) return MakeError(TEXT("No asset was imported."));
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("imported_assets"), Imported);
		return MakeSuccess(Data);
	}

	if (Command == TEXT("list_actors"))
	{
		UWorld* World = EditorWorld();
		if (!World) return MakeError(TEXT("No editor world is open."), TEXT("world_unavailable"));
		FString LabelFilter, ClassFilter;
		Params->TryGetStringField(TEXT("label_filter"), LabelFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		const int32 Limit = GetLimit(Params);
		TArray<TSharedPtr<FJsonValue>> Actors;
		for (TActorIterator<AActor> It(World); It && Actors.Num() < Limit; ++It)
		{
			AActor* Actor = *It;
			if (!Actor || (!LabelFilter.IsEmpty() && !Actor->GetActorLabel().Contains(LabelFilter, ESearchCase::IgnoreCase)) ||
				(!ClassFilter.IsEmpty() && !Actor->GetClass()->GetPathName().Contains(ClassFilter, ESearchCase::IgnoreCase)))
			{
				continue;
			}
			Actors.Add(JsonObjectValue(ActorObject(Actor, false, 0)));
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("actors"), Actors);
		Data->SetNumberField(TEXT("returned"), Actors.Num());
		return MakeSuccess(Data);
	}

	if (Command == TEXT("inspect_actor"))
	{
		FString Identifier, Error;
		if (!RequiredString(Params, TEXT("actor"), Identifier, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		AActor* Actor = FindActor(Identifier);
		if (!Actor) return MakeError(TEXT("Actor was not found: ") + Identifier, TEXT("actor_not_found"));
		return MakeSuccess(ActorObject(Actor, true, GetLimit(Params, 150)));
	}

	if (Command == TEXT("spawn_actor"))
	{
		FString ClassPath, Error;
		if (!RequiredString(Params, TEXT("class_path"), ClassPath, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		UClass* ActorClass = LoadObject<UClass>(nullptr, *ClassPath);
		if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass())) return MakeError(TEXT("class_path is not an Actor class."), TEXT("invalid_actor_class"));
		UWorld* World = EditorWorld();
		if (!World) return MakeError(TEXT("No editor world is open."), TEXT("world_unavailable"));
		const FVector Location = ReadVector(Params, TEXT("location"), FVector::ZeroVector);
		const FRotator Rotation = ReadRotator(Params, TEXT("rotation"), FRotator::ZeroRotator);
		const FScopedTransaction Transaction(FText::FromString(TEXT("EditorLink: Spawn Actor")));
		AActor* Actor = World->SpawnActor<AActor>(ActorClass, Location, Rotation);
		if (!Actor) return MakeError(TEXT("Actor spawn failed."));
		FString Label;
		if (Params->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty()) Actor->SetActorLabel(Label, true);
		Actor->SetActorScale3D(ReadVector(Params, TEXT("scale"), FVector::OneVector));
		Actor->MarkPackageDirty();
		return MakeSuccess(ActorObject(Actor, false, 0));
	}

	if (Command == TEXT("delete_actor"))
	{
		FString Identifier, Error;
		if (!RequiredString(Params, TEXT("actor"), Identifier, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		if (!GetBool(Params, TEXT("confirm"), false)) return MakeError(TEXT("Set confirm=true to delete an actor."), TEXT("confirmation_required"));
		AActor* Actor = FindActor(Identifier);
		if (!Actor) return MakeError(TEXT("Actor was not found."), TEXT("actor_not_found"));
		const FString DeletedPath = Actor->GetPathName();
		const FScopedTransaction Transaction(FText::FromString(TEXT("EditorLink: Delete Actor")));
		Actor->Modify();
		const bool bDeleted = EditorWorld()->EditorDestroyActor(Actor, true);
		if (!bDeleted) return MakeError(TEXT("Actor deletion failed."));
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("deleted_actor"), DeletedPath);
		return MakeSuccess(Data);
	}

	if (Command == TEXT("set_actor_transform"))
	{
		FString Identifier, Error;
		if (!RequiredString(Params, TEXT("actor"), Identifier, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		AActor* Actor = FindActor(Identifier);
		if (!Actor) return MakeError(TEXT("Actor was not found."), TEXT("actor_not_found"));
		const FScopedTransaction Transaction(FText::FromString(TEXT("EditorLink: Set Actor Transform")));
		Actor->Modify();
		Actor->SetActorLocation(ReadVector(Params, TEXT("location"), Actor->GetActorLocation()));
		Actor->SetActorRotation(ReadRotator(Params, TEXT("rotation"), Actor->GetActorRotation()));
		Actor->SetActorScale3D(ReadVector(Params, TEXT("scale"), Actor->GetActorScale3D()));
		Actor->MarkPackageDirty();
		return MakeSuccess(ActorObject(Actor, false, 0));
	}

	if (Command == TEXT("add_actor_component") || Command == TEXT("remove_actor_component"))
	{
		FString Identifier, Error;
		if (!RequiredString(Params, TEXT("actor"), Identifier, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		AActor* Actor = FindActor(Identifier);
		if (!Actor) return MakeError(TEXT("Actor was not found."), TEXT("actor_not_found"));
		const FScopedTransaction Transaction(FText::FromString(TEXT("EditorLink: Edit Actor Components")));
		Actor->Modify();
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		if (Command == TEXT("add_actor_component"))
		{
			FString ClassPath, Name;
			if (!RequiredString(Params, TEXT("component_class"), ClassPath, Error)) return MakeError(Error, TEXT("invalid_parameters"));
			Params->TryGetStringField(TEXT("component_name"), Name);
			UClass* ComponentClass = LoadObject<UClass>(nullptr, *ClassPath);
			if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass())) return MakeError(TEXT("component_class is invalid."));
			UActorComponent* Component = NewObject<UActorComponent>(Actor, ComponentClass, Name.IsEmpty() ? NAME_None : FName(Name), RF_Transactional);
			Actor->AddInstanceComponent(Component);
			Component->OnComponentCreated();
			Component->RegisterComponent();
			Data->SetStringField(TEXT("component"), Component->GetPathName());
		}
		else
		{
			if (!GetBool(Params, TEXT("confirm"), false)) return MakeError(TEXT("Set confirm=true to remove an actor component."), TEXT("confirmation_required"));
			FString Name;
			if (!RequiredString(Params, TEXT("component_name"), Name, Error)) return MakeError(Error, TEXT("invalid_parameters"));
			UActorComponent* Found = nullptr;
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (Component && Component->GetName().Equals(Name, ESearchCase::IgnoreCase)) { Found = Component; break; }
			}
			if (!Found) return MakeError(TEXT("Component was not found."), TEXT("component_not_found"));
			Found->Modify();
			Found->DestroyComponent();
			Data->SetStringField(TEXT("removed_component"), Name);
		}
		Actor->MarkPackageDirty();
		return MakeSuccess(Data);
	}

	if (Command == TEXT("set_editable_property"))
	{
		FString Target, PropertyName, Value, Error;
		if (!RequiredString(Params, TEXT("target"), Target, Error) || !RequiredString(Params, TEXT("property_name"), PropertyName, Error))
		{
			return MakeError(Error, TEXT("invalid_parameters"));
		}
		Params->TryGetStringField(TEXT("value"), Value);
		UObject* Object = FindActor(Target);
		if (!Object) Object = LoadEditorObject(Target);
		if (!Object) return MakeError(TEXT("Target object was not found."), TEXT("object_not_found"));
		FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), FName(PropertyName));
		if (!Property) return MakeError(TEXT("Property was not found."), TEXT("property_not_found"));
		if (!Property->HasAnyPropertyFlags(CPF_Edit)) return MakeError(TEXT("Only editable properties can be changed in the Demo."), TEXT("property_not_editable"));
		const FScopedTransaction Transaction(FText::FromString(TEXT("EditorLink: Set Property")));
		Object->Modify();
		Object->PreEditChange(Property);
		void* Address = Property->ContainerPtrToValuePtr<void>(Object);
		if (!Property->ImportText_Direct(*Value, Address, Object, PPF_None)) return MakeError(TEXT("The value could not be imported using Unreal property syntax."), TEXT("invalid_property_value"));
		FPropertyChangedEvent ChangedEvent(Property, EPropertyChangeType::ValueSet);
		Object->PostEditChangeProperty(ChangedEvent);
		Object->MarkPackageDirty();
		TSharedPtr<FJsonObject> Data = PropertyObject(Property, Object);
		Data->SetStringField(TEXT("target"), Object->GetPathName());
		return MakeSuccess(Data);
	}

	if (Command == TEXT("load_level"))
	{
		FString Path, Error;
		if (!RequiredString(Params, TEXT("level_path"), Path, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		if (!FEditorFileUtils::LoadMap(Path, false, true)) return MakeError(TEXT("Level could not be loaded."));
		UWorld* Loaded = EditorWorld();
		if (!Loaded) return MakeError(TEXT("The level was loaded, but no editor world is available."), TEXT("world_unavailable"));
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("world"), Loaded->GetPathName());
		return MakeSuccess(Data);
	}

	if (Command == TEXT("save_current_level"))
	{
		const bool bSaved = FEditorFileUtils::SaveCurrentLevel();
		if (!bSaved) return MakeError(TEXT("Current level save failed."));
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("saved"), true);
		Data->SetStringField(TEXT("world"), EditorWorld() ? EditorWorld()->GetPathName() : FString());
		return MakeSuccess(Data);
	}

	if (Command == TEXT("inspect_blueprint") || Command == TEXT("inspect_anim_blueprint"))
	{
		FString Path, Error;
		if (!RequiredString(Params, TEXT("asset_path"), Path, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		UBlueprint* Blueprint = LoadBlueprint(Path);
		if (!Blueprint) return MakeError(TEXT("Blueprint could not be loaded."), TEXT("blueprint_not_found"));
		if (Command == TEXT("inspect_anim_blueprint") && !Blueprint->IsA<UAnimBlueprint>()) return MakeError(TEXT("Asset is not an Animation Blueprint."), TEXT("wrong_asset_type"));
		TSharedPtr<FJsonObject> Data = BlueprintSummary(Blueprint, GetLimit(Params, 200));
		if (UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint))
		{
			Data->SetStringField(TEXT("target_skeleton"), AnimBlueprint->TargetSkeleton ? AnimBlueprint->TargetSkeleton->GetPathName() : FString());
		}
		return MakeSuccess(Data);
	}

	if (Command == TEXT("compile_blueprint"))
	{
		FString Path, Error;
		if (!RequiredString(Params, TEXT("asset_path"), Path, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		UBlueprint* Blueprint = LoadBlueprint(Path);
		if (!Blueprint) return MakeError(TEXT("Blueprint could not be loaded."), TEXT("blueprint_not_found"));
		FCompilerResultsLog Log;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset"), Blueprint->GetPathName());
		Data->SetStringField(TEXT("status"), StaticEnum<EBlueprintStatus>()->GetNameStringByValue(Blueprint->Status));
		Data->SetNumberField(TEXT("errors"), Log.NumErrors);
		Data->SetNumberField(TEXT("warnings"), Log.NumWarnings);
		TArray<TSharedPtr<FJsonValue>> Messages;
		for (const TSharedRef<FTokenizedMessage>& Message : Log.Messages)
		{
			Messages.Add(JsonString(Message->ToText().ToString()));
		}
		Data->SetArrayField(TEXT("messages"), Messages);
		Data->SetBoolField(TEXT("compile_ok"), Log.NumErrors == 0 && Blueprint->Status != BS_Error);
		if (Log.NumErrors == 0 && Blueprint->Status != BS_Error)
		{
			return MakeSuccess(Data);
		}
		TSharedPtr<FJsonObject> Failure = MakeError(TEXT("Blueprint compilation failed. Inspect the compiler messages."), TEXT("compile_failed"));
		Failure->SetObjectField(TEXT("data"), Data);
		return Failure;
	}

	if (Command == TEXT("add_blueprint_variable") || Command == TEXT("remove_blueprint_variable"))
	{
		FString Path, Name, Error;
		if (!RequiredString(Params, TEXT("asset_path"), Path, Error) || !RequiredString(Params, TEXT("variable_name"), Name, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		UBlueprint* Blueprint = LoadBlueprint(Path);
		if (!Blueprint) return MakeError(TEXT("Blueprint could not be loaded."), TEXT("blueprint_not_found"));
		const FScopedTransaction Transaction(FText::FromString(TEXT("EditorLink: Edit Blueprint Variable")));
		Blueprint->Modify();
		if (Command == TEXT("add_blueprint_variable"))
		{
			FString TypeName = TEXT("string"), ClassPath, DefaultValue;
			Params->TryGetStringField(TEXT("type"), TypeName);
			Params->TryGetStringField(TEXT("object_class"), ClassPath);
			Params->TryGetStringField(TEXT("default_value"), DefaultValue);
			if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(Name), PinTypeFromName(TypeName, ClassPath), DefaultValue)) return MakeError(TEXT("Variable could not be added."));
		}
		else
		{
			if (!GetBool(Params, TEXT("confirm"), false)) return MakeError(TEXT("Set confirm=true to remove a Blueprint variable."), TEXT("confirmation_required"));
			if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(Name)) == INDEX_NONE) return MakeError(TEXT("Variable was not found."), TEXT("variable_not_found"));
			FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(Name));
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset"), Blueprint->GetPathName());
		Data->SetStringField(TEXT("variable"), Name);
		Data->SetBoolField(TEXT("dirty"), Blueprint->GetOutermost()->IsDirty());
		return MakeSuccess(Data);
	}

	if (Command == TEXT("add_blueprint_component") || Command == TEXT("remove_blueprint_component"))
	{
		FString Path, Name, Error;
		if (!RequiredString(Params, TEXT("asset_path"), Path, Error) || !RequiredString(Params, TEXT("component_name"), Name, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		UBlueprint* Blueprint = LoadBlueprint(Path);
		if (!Blueprint || !Blueprint->SimpleConstructionScript) return MakeError(TEXT("Blueprint has no Simple Construction Script."), TEXT("blueprint_not_supported"));
		const FScopedTransaction Transaction(FText::FromString(TEXT("EditorLink: Edit Blueprint Component")));
		Blueprint->Modify();
		USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
		if (Command == TEXT("add_blueprint_component"))
		{
			FString ClassPath;
			if (!RequiredString(Params, TEXT("component_class"), ClassPath, Error)) return MakeError(Error, TEXT("invalid_parameters"));
			UClass* ComponentClass = LoadObject<UClass>(nullptr, *ClassPath);
			if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass())) return MakeError(TEXT("component_class is invalid."));
			USCS_Node* Node = SCS->CreateNode(ComponentClass, FName(Name));
			if (!Node) return MakeError(TEXT("Blueprint component node could not be created."));
			SCS->AddNode(Node);
		}
		else
		{
			if (!GetBool(Params, TEXT("confirm"), false)) return MakeError(TEXT("Set confirm=true to remove a Blueprint component."), TEXT("confirmation_required"));
			USCS_Node* Found = nullptr;
			for (USCS_Node* Node : SCS->GetAllNodes())
			{
				if (Node && Node->GetVariableName().ToString().Equals(Name, ESearchCase::IgnoreCase)) { Found = Node; break; }
			}
			if (!Found) return MakeError(TEXT("Blueprint component was not found."), TEXT("component_not_found"));
			SCS->RemoveNode(Found);
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset"), Blueprint->GetPathName());
		Data->SetStringField(TEXT("component"), Name);
		return MakeSuccess(Data);
	}

	if (Command == TEXT("create_graph_node"))
	{
		FString Path, GraphName, NodeKind, Error;
		if (!RequiredString(Params, TEXT("asset_path"), Path, Error) || !RequiredString(Params, TEXT("graph_name"), GraphName, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		Params->TryGetStringField(TEXT("node_kind"), NodeKind);
		UBlueprint* Blueprint = LoadBlueprint(Path);
		UEdGraph* Graph = FindGraph(Blueprint, GraphName);
		if (!Blueprint || !Graph) return MakeError(TEXT("Blueprint or graph was not found."), TEXT("graph_not_found"));
		double XValue = 0, YValue = 0;
		Params->TryGetNumberField(TEXT("x"), XValue);
		Params->TryGetNumberField(TEXT("y"), YValue);
		const bool bAutoPlace = GetBool(Params, TEXT("auto_place"), true);
		double HorizontalGapValue = 160.0, VerticalGapValue = 64.0;
		Params->TryGetNumberField(TEXT("horizontal_gap"), HorizontalGapValue);
		Params->TryGetNumberField(TEXT("vertical_gap"), VerticalGapValue);
		const int32 HorizontalGap = FMath::Clamp(static_cast<int32>(HorizontalGapValue), 32, 1024);
		const int32 VerticalGap = FMath::Clamp(static_cast<int32>(VerticalGapValue), 16, 512);
		FString SourceNodeGuid;
		if (Params->TryGetStringField(TEXT("source_node_guid"), SourceNodeGuid) && !SourceNodeGuid.IsEmpty())
		{
			if (UEdGraphNode* SourceNode = FindNode(Graph, SourceNodeGuid))
			{
				XValue = SourceNode->NodePosX + EstimatedNodeWidth(SourceNode) + HorizontalGap;
				YValue = SourceNode->NodePosY;
			}
			else
			{
				return MakeError(TEXT("source_node_guid was not found in the graph."), TEXT("source_node_not_found"));
			}
		}
		const FScopedTransaction Transaction(FText::FromString(TEXT("EditorLink: Create Graph Node")));
		Blueprint->Modify();
		UEdGraphNode* Node = nullptr;
		if (NodeKind.Equals(TEXT("branch"), ESearchCase::IgnoreCase)) Node = CreateGraphNode(Graph, UK2Node_IfThenElse::StaticClass(), XValue, YValue);
		else if (NodeKind.Equals(TEXT("sequence"), ESearchCase::IgnoreCase)) Node = CreateGraphNode(Graph, UK2Node_ExecutionSequence::StaticClass(), XValue, YValue);
		else if (NodeKind.Equals(TEXT("custom_event"), ESearchCase::IgnoreCase))
		{
			UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(CreateGraphNode(Graph, UK2Node_CustomEvent::StaticClass(), XValue, YValue));
			FString EventName = TEXT("EditorLinkEvent");
			Params->TryGetStringField(TEXT("event_name"), EventName);
			if (Event) { Event->CustomFunctionName = FName(EventName); Event->ReconstructNode(); }
			Node = Event;
		}
		else if (NodeKind.Equals(TEXT("call_function"), ESearchCase::IgnoreCase))
		{
			FString OwnerClassPath, FunctionName;
			if (!RequiredString(Params, TEXT("owner_class"), OwnerClassPath, Error) || !RequiredString(Params, TEXT("function_name"), FunctionName, Error)) return MakeError(Error, TEXT("invalid_parameters"));
			UClass* OwnerClass = LoadObject<UClass>(nullptr, *OwnerClassPath);
			UFunction* Function = OwnerClass ? OwnerClass->FindFunctionByName(FName(FunctionName)) : nullptr;
			if (!Function) return MakeError(TEXT("Function was not found on owner_class."), TEXT("function_not_found"));
			UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(CreateGraphNode(Graph, UK2Node_CallFunction::StaticClass(), XValue, YValue));
			if (Call) { Call->SetFromFunction(Function); Call->ReconstructNode(); }
			Node = Call;
		}
		else
		{
			FString NodeClassPath;
			if (!RequiredString(Params, TEXT("node_class"), NodeClassPath, Error)) return MakeError(TEXT("Provide a supported node_kind or node_class."), TEXT("invalid_parameters"));
			Node = CreateGraphNode(Graph, LoadObject<UClass>(nullptr, *NodeClassPath), XValue, YValue);
		}
		if (!Node) return MakeError(TEXT("Graph node could not be created."));
		const bool bPositionAdjusted = bAutoPlace
			? PlaceNodeWithoutOverlap(Graph, Node, static_cast<int32>(XValue), static_cast<int32>(YValue), HorizontalGap, VerticalGap)
			: false;
		if (!bAutoPlace)
		{
			Node->NodePosX = FMath::GridSnap(static_cast<int32>(XValue), 16);
			Node->NodePosY = FMath::GridSnap(static_cast<int32>(YValue), 16);
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
		Data->SetStringField(TEXT("node_class"), Node->GetClass()->GetPathName());
		Data->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		Data->SetStringField(TEXT("graph"), Graph->GetName());
		Data->SetNumberField(TEXT("x"), Node->NodePosX);
		Data->SetNumberField(TEXT("y"), Node->NodePosY);
		Data->SetBoolField(TEXT("auto_placed"), bAutoPlace);
		Data->SetBoolField(TEXT("position_adjusted"), bPositionAdjusted);
		return MakeSuccess(Data);
	}

	if (Command == TEXT("connect_graph_pins") || Command == TEXT("disconnect_graph_pin") || Command == TEXT("set_graph_pin_default") || Command == TEXT("delete_graph_node"))
	{
		FString Path, GraphName, NodeGuid, Error;
		if (!RequiredString(Params, TEXT("asset_path"), Path, Error) || !RequiredString(Params, TEXT("graph_name"), GraphName, Error) || !RequiredString(Params, TEXT("node_guid"), NodeGuid, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		UBlueprint* Blueprint = LoadBlueprint(Path);
		UEdGraph* Graph = FindGraph(Blueprint, GraphName);
		UEdGraphNode* Node = FindNode(Graph, NodeGuid);
		if (!Blueprint || !Graph || !Node) return MakeError(TEXT("Blueprint, graph, or node was not found."), TEXT("graph_target_not_found"));
		const FScopedTransaction Transaction(FText::FromString(TEXT("EditorLink: Edit Graph")));
		Blueprint->Modify(); Graph->Modify(); Node->Modify();
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		if (Command == TEXT("delete_graph_node"))
		{
			if (!GetBool(Params, TEXT("confirm"), false)) return MakeError(TEXT("Set confirm=true to delete a graph node."), TEXT("confirmation_required"));
			for (UEdGraphPin* Pin : Node->Pins) if (Pin) Pin->BreakAllPinLinks();
			Graph->RemoveNode(Node);
			Data->SetStringField(TEXT("deleted_node"), NodeGuid);
		}
		else
		{
			FString PinName;
			if (!RequiredString(Params, TEXT("pin"), PinName, Error)) return MakeError(Error, TEXT("invalid_parameters"));
			if (Command == TEXT("set_graph_pin_default"))
			{
				FString Value;
				Params->TryGetStringField(TEXT("value"), Value);
				UEdGraphPin* Pin = FindPin(Node, PinName, EGPD_Input);
				if (!Pin) return MakeError(TEXT("The input pin was not found."), TEXT("pin_not_found"));
				Graph->GetSchema()->TrySetDefaultValue(*Pin, Value);
				Data->SetStringField(TEXT("value"), Pin->DefaultValue);
			}
			else if (Command == TEXT("disconnect_graph_pin"))
			{
				UEdGraphPin* Pin = FindPin(Node, PinName, EGPD_Input);
				if (!Pin) Pin = FindPin(Node, PinName, EGPD_Output);
				if (!Pin) return MakeError(TEXT("Pin was not found."), TEXT("pin_not_found"));
				Pin->BreakAllPinLinks();
				Data->SetBoolField(TEXT("disconnected"), true);
			}
			else
			{
				FString TargetGuid, TargetPin;
				if (!RequiredString(Params, TEXT("target_node_guid"), TargetGuid, Error) || !RequiredString(Params, TEXT("target_pin"), TargetPin, Error)) return MakeError(Error, TEXT("invalid_parameters"));
				UEdGraphNode* TargetNode = FindNode(Graph, TargetGuid);
				UEdGraphPin* OutputPin = FindPin(Node, PinName, EGPD_Output);
				UEdGraphPin* InputPin = FindPin(TargetNode, TargetPin, EGPD_Input);
				if (!OutputPin || !InputPin) return MakeError(TEXT("Source output pin or target input pin was not found."), TEXT("pin_not_found"));
				if (!Graph->GetSchema()->TryCreateConnection(OutputPin, InputPin)) return MakeError(TEXT("Graph schema rejected the connection."), TEXT("connection_rejected"));
				Data->SetBoolField(TEXT("connected"), true);
			}
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		return MakeSuccess(Data);
	}

	if (Command == TEXT("inspect_skeleton"))
	{
		FString Path, Error;
		if (!RequiredString(Params, TEXT("skeleton_path"), Path, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		USkeleton* Skeleton = LoadSkeleton(Path);
		if (!Skeleton) return MakeError(TEXT("Skeleton could not be loaded."), TEXT("skeleton_not_found"));
		return MakeSuccess(SkeletonSummary(Skeleton));
	}

	if (Command == TEXT("upsert_skeleton_socket") || Command == TEXT("delete_skeleton_socket") || Command == TEXT("add_animation_slot"))
	{
		FString Path, Error;
		if (!RequiredString(Params, TEXT("skeleton_path"), Path, Error)) return MakeError(Error, TEXT("invalid_parameters"));
		USkeleton* Skeleton = LoadSkeleton(Path);
		if (!Skeleton) return MakeError(TEXT("Skeleton could not be loaded."), TEXT("skeleton_not_found"));
		const FScopedTransaction Transaction(FText::FromString(TEXT("EditorLink: Edit Skeleton")));
		Skeleton->Modify();
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		if (Command == TEXT("add_animation_slot"))
		{
			FString SlotName;
			if (!RequiredString(Params, TEXT("slot_name"), SlotName, Error)) return MakeError(Error, TEXT("invalid_parameters"));
			const bool bAlreadyExists = Skeleton->ContainsSlotName(FName(SlotName));
			if (!bAlreadyExists && !Skeleton->RegisterSlotNode(FName(SlotName))) return MakeError(TEXT("Animation slot could not be registered."));
			Data->SetStringField(TEXT("slot"), SlotName);
			Data->SetBoolField(TEXT("created"), !bAlreadyExists);
		}
		else
		{
			FString SocketName;
			if (!RequiredString(Params, TEXT("socket_name"), SocketName, Error)) return MakeError(Error, TEXT("invalid_parameters"));
			USkeletalMeshSocket* Socket = nullptr;
			for (USkeletalMeshSocket* Existing : Skeleton->Sockets)
			{
				if (Existing && Existing->SocketName.ToString().Equals(SocketName, ESearchCase::IgnoreCase)) { Socket = Existing; break; }
			}
			if (Command == TEXT("delete_skeleton_socket"))
			{
				if (!GetBool(Params, TEXT("confirm"), false)) return MakeError(TEXT("Set confirm=true to delete a socket."), TEXT("confirmation_required"));
				if (!Socket) return MakeError(TEXT("Socket was not found."), TEXT("socket_not_found"));
				Skeleton->Sockets.Remove(Socket);
				Data->SetStringField(TEXT("deleted_socket"), SocketName);
			}
			else
			{
				FString BoneName;
				if (!RequiredString(Params, TEXT("bone_name"), BoneName, Error)) return MakeError(Error, TEXT("invalid_parameters"));
				if (Skeleton->GetReferenceSkeleton().FindBoneIndex(FName(BoneName)) == INDEX_NONE) return MakeError(TEXT("Bone was not found on the skeleton."), TEXT("bone_not_found"));
				const bool bCreated = Socket == nullptr;
				if (!Socket)
				{
					Socket = NewObject<USkeletalMeshSocket>(Skeleton, NAME_None, RF_Transactional);
					Skeleton->Sockets.Add(Socket);
				}
				Socket->Modify();
				Socket->SocketName = FName(SocketName);
				Socket->BoneName = FName(BoneName);
				Socket->RelativeLocation = ReadVector(Params, TEXT("location"), Socket->RelativeLocation);
				Socket->RelativeRotation = ReadRotator(Params, TEXT("rotation"), Socket->RelativeRotation);
				Socket->RelativeScale = ReadVector(Params, TEXT("scale"), Socket->RelativeScale.IsNearlyZero() ? FVector::OneVector : Socket->RelativeScale);
				Data->SetStringField(TEXT("socket"), SocketName);
				Data->SetStringField(TEXT("bone"), BoneName);
				Data->SetBoolField(TEXT("created"), bCreated);
			}
		}
		Skeleton->MarkPackageDirty();
		Skeleton->PostEditChange();
		Data->SetBoolField(TEXT("dirty"), Skeleton->GetOutermost()->IsDirty());
		return MakeSuccess(Data);
	}

	if (Command == TEXT("get_dirty_packages"))
	{
		TArray<TSharedPtr<FJsonValue>> Packages;
		const int32 Limit = GetLimit(Params, 200);
		for (TObjectIterator<UPackage> It; It && Packages.Num() < Limit; ++It)
		{
			UPackage* Package = *It;
			if (Package && Package->IsDirty() && (Package->GetName().StartsWith(TEXT("/Game/")) || Package->GetName().StartsWith(TEXT("/EditorLink"))))
			{
				Packages.Add(JsonString(Package->GetName()));
			}
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("packages"), Packages);
		Data->SetNumberField(TEXT("count"), Packages.Num());
		return MakeSuccess(Data);
	}

	if (Command == TEXT("undo") || Command == TEXT("redo"))
	{
		if (!GEditor) return MakeError(TEXT("Editor is unavailable."));
		const bool bResult = Command == TEXT("undo") ? GEditor->UndoTransaction() : GEditor->RedoTransaction();
		if (!bResult) return MakeError(TEXT("No matching transaction was available."), TEXT("transaction_unavailable"));
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("operation"), Command);
		Data->SetBoolField(TEXT("completed"), true);
		return MakeSuccess(Data);
	}

	if (Command == TEXT("capture_viewport"))
	{
		FString FileName;
		Params->TryGetStringField(TEXT("file_name"), FileName);
		if (FileName.IsEmpty()) FileName = FString::Printf(TEXT("EditorLink_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		if (FPaths::IsRelative(FileName)) FileName = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EditorLinkMCPDemo"), TEXT("Screenshots"), FileName);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FileName), true);
		FScreenshotRequest::RequestScreenshot(FileName, false, false);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_path"), FPaths::ConvertRelativePathToFull(FileName));
		Data->SetStringField(TEXT("status"), TEXT("Screenshot is captured on the next viewport render."));
		return MakeSuccess(Data);
	}

	if (Command == TEXT("start_pie") || Command == TEXT("stop_pie"))
	{
		if (!GEditor) return MakeError(TEXT("Editor is unavailable."));
		if (Command == TEXT("start_pie"))
		{
			if (GEditor->PlayWorld) return MakeError(TEXT("PIE is already running."), TEXT("already_running"));
			GEditor->Exec(EditorWorld(), TEXT("PIE"));
		}
		else
		{
			if (!GEditor->PlayWorld) return MakeError(TEXT("PIE is not running."), TEXT("not_running"));
			GEditor->RequestEndPlayMap();
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested"), Command);
		return MakeSuccess(Data);
	}

	return MakeError(TEXT("Unknown command: ") + Command, TEXT("unknown_command"));
}

