#include "EditorLinkMCPDemo.h"

#include "Editor.h"
#include "EditorLinkMCPDemoSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace EditorLinkMCPDemo
{
	static const FName TabName(TEXT("EditorLinkMCPDemo"));

	class SEditorLinkDashboard final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEditorLinkDashboard) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			ChildSlot
			[
				SNew(SBorder)
				.Padding(16.0f)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("EditorLink MCP Demo")))
							.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
						[
							SNew(STextBlock)
							.Text(this, &SEditorLinkDashboard::GetStatus)
							.AutoWrapText(true)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
						[
							SNew(SSeparator)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Client configuration")))
							.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Copy generic JSON configuration (Claude, Kimi, Qwen and compatible clients)")))
							.OnClicked(this, &SEditorLinkDashboard::CopyJson)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Copy TOML configuration (Codex and Grok Build)")))
							.OnClicked(this, &SEditorLinkDashboard::CopyToml)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("DeepSeek models are supported through any MCP-compatible host using the generic JSON configuration.")))
							.AutoWrapText(true)
						]
					]
				]
			];
		}

	private:
		UEditorLinkMCPDemoSubsystem* GetSubsystem() const
		{
			return GEditor ? GEditor->GetEditorSubsystem<UEditorLinkMCPDemoSubsystem>() : nullptr;
		}

		FText GetStatus() const
		{
			const UEditorLinkMCPDemoSubsystem* Subsystem = GetSubsystem();
			return FText::FromString(Subsystem ? Subsystem->GetStatusText() : TEXT("Editor subsystem is unavailable."));
		}

		FReply CopyJson()
		{
			if (const UEditorLinkMCPDemoSubsystem* Subsystem = GetSubsystem())
			{
				FPlatformApplicationMisc::ClipboardCopy(*Subsystem->MakeJsonClientSnippet());
			}
			return FReply::Handled();
		}

		FReply CopyToml()
		{
			if (const UEditorLinkMCPDemoSubsystem* Subsystem = GetSubsystem())
			{
				FPlatformApplicationMisc::ClipboardCopy(*Subsystem->MakeTomlClientSnippet());
			}
			return FReply::Handled();
		}
	};
}

void FEditorLinkMCPDemoModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		EditorLinkMCPDemo::TabName,
		FOnSpawnTab::CreateRaw(this, &FEditorLinkMCPDemoModule::SpawnEditorLinkTab))
		.SetDisplayName(FText::FromString(TEXT("EditorLink MCP Demo")))
		.SetTooltipText(FText::FromString(TEXT("Open the EditorLink MCP status and client configuration panel.")))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FEditorLinkMCPDemoModule::RegisterMenus));
}

void FEditorLinkMCPDemoModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(EditorLinkMCPDemo::TabName);
}

void FEditorLinkMCPDemoModule::OpenEditorLinkTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(EditorLinkMCPDemo::TabName);
}

void FEditorLinkMCPDemoModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection(
		TEXT("EditorLinkMCP"),
		FText::FromString(TEXT("EditorLink MCP")));
	Section.AddMenuEntry(
		TEXT("EditorLinkMCPDemo.OpenPanel"),
		FText::FromString(TEXT("EditorLink MCP Demo")),
		FText::FromString(TEXT("Open the EditorLink MCP status and client configuration panel.")),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Link")),
		FUIAction(FExecuteAction::CreateRaw(this, &FEditorLinkMCPDemoModule::OpenEditorLinkTab)));
}

TSharedRef<SDockTab> FEditorLinkMCPDemoModule::SpawnEditorLinkTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(EditorLinkMCPDemo::SEditorLinkDashboard)
		];
}

IMPLEMENT_MODULE(FEditorLinkMCPDemoModule, EditorLinkMCPDemo)

