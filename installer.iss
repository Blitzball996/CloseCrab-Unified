; CloseCrab-Unified Installer
#define MyAppName "CloseCrab-Unified"
#define MyAppVersion "0.2.0"
#define MyAppPublisher "CloseCrab"
#define MyAppExeName "closecrab-unified.exe"

[Setup]
AppId={{CloseCrab-Unified-AI}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
OutputDir=installer
OutputBaseFilename=CloseCrab-Unified_Setup
PrivilegesRequired=lowest
WizardStyle=modern
SetupIconFile=icons\closecrab.ico
WizardSizePercent=130,120

[Files]
Source: "out\build\x64-release\closecrab-unified.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "out\build\x64-release\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "config\config.yaml"; DestDir: "{app}\config"; Flags: ignoreversion
Source: "download_model.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "run.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "icons\closecrab.ico"; DestDir: "{app}\icons"; Flags: ignoreversion
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\run.bat"; IconFilename: "{app}\icons\closecrab.ico"; IconIndex: 0
Name: "{group}\Download Models"; Filename: "{app}\download_model.bat"; IconFilename: "{app}\icons\closecrab.ico"; IconIndex: 0
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\run.bat"; IconFilename: "{app}\icons\closecrab.ico"; IconIndex: 0; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Code]
var
  // Page 1: Provider mode
  ProviderPage: TWizardPage;
  ModeCombo: TNewComboBox;

  // Page 2: Local model selection (only shown if local)
  LocalPage: TWizardPage;
  ModelCombo: TNewComboBox;
  ModelPathEdit: TNewEdit;

  // Page 3: API configuration (only shown if API)
  ApiPage: TWizardPage;
  ApiUrlEdit: TNewEdit;
  ApiKeyEdit: TNewEdit;
  ApiModelEdit: TNewEdit;

procedure InitializeWizard;
var
  Lbl, Desc: TNewStaticText;
begin
  // ==========================================
  // PAGE 1: Choose Provider
  // ==========================================
  ProviderPage := CreateCustomPage(wpSelectTasks,
    'Choose AI Provider',
    'How should CloseCrab connect to an AI model?');

  Lbl := TNewStaticText.Create(ProviderPage);
  Lbl.Parent := ProviderPage.Surface;
  Lbl.Caption := 'Select a provider:';
  Lbl.Top := 8;
  Lbl.Font.Style := [fsBold];
  Lbl.Font.Size := 11;

  ModeCombo := TNewComboBox.Create(ProviderPage);
  ModeCombo.Parent := ProviderPage.Surface;
  ModeCombo.Top := 38;
  ModeCombo.Width := 440;
  ModeCombo.Style := csDropDownList;
  ModeCombo.Font.Size := 10;
  ModeCombo.Items.Add('Local Model  (run on your own GPU, no internet)');
  ModeCombo.Items.Add('Anthropic API  (Claude, or a compatible proxy)');
  ModeCombo.Items.Add('OpenAI Compatible  (OpenAI / LM Studio / Ollama)');
  ModeCombo.ItemIndex := 0;

  Desc := TNewStaticText.Create(ProviderPage);
  Desc.Parent := ProviderPage.Surface;
  Desc.Top := 80;
  Desc.Width := 440;
  Desc.WordWrap := True;
  Desc.Font.Size := 9;
  Desc.Caption :=
    'LOCAL MODEL' + #13#10 +
    'Runs a GGUF model on your GPU using llama.cpp.' + #13#10 +
    'No internet required. Your data stays on your machine.' + #13#10 +
    'Requires: NVIDIA GPU with 6GB+ VRAM.' + #13#10 +
    '' + #13#10 +
    'ANTHROPIC API' + #13#10 +
    'Connects to Claude API or a compatible proxy/relay.' + #13#10 +
    'Full tool_use support, auto-retry on errors.' + #13#10 +
    'Requires: API key and internet connection.' + #13#10 +
    '' + #13#10 +
    'OPENAI COMPATIBLE' + #13#10 +
    'Works with any OpenAI-format API: OpenAI, LM Studio,' + #13#10 +
    'SiliconFlow, Ollama, vLLM, and other providers.' + #13#10 +
    '' + #13#10 +
    'All modes include: 42 tools, 50+ commands, multi-agent,' + #13#10 +
    'memory system, hooks, vim mode, voice output, and more.';

  // ==========================================
  // PAGE 2: Local Model Selection
  // ==========================================
  LocalPage := CreateCustomPage(ProviderPage.ID,
    'Select Local Model',
    'Choose a model to download, or specify a custom path.');

  Lbl := TNewStaticText.Create(LocalPage);
  Lbl.Parent := LocalPage.Surface;
  Lbl.Caption := 'Download a model (recommended):';
  Lbl.Top := 8;
  Lbl.Font.Style := [fsBold];
  Lbl.Font.Size := 10;

  ModelCombo := TNewComboBox.Create(LocalPage);
  ModelCombo.Parent := LocalPage.Surface;
  ModelCombo.Top := 34;
  ModelCombo.Width := 440;
  ModelCombo.Style := csDropDownList;
  ModelCombo.Font.Size := 10;
  ModelCombo.Items.Add('Qwen2.5-7B   (Recommended, 4.5GB, needs 8GB VRAM)');
  ModelCombo.Items.Add('Qwen2.5-14B  (Stronger, 8.5GB, needs 12GB VRAM)');
  ModelCombo.Items.Add('Qwen2.5-3B   (Light, 2GB, needs 6GB VRAM)');
  ModelCombo.Items.Add('DeepSeek-Coder-V2-Lite  (Code-focused, 9GB, needs 12GB VRAM)');
  ModelCombo.Items.Add('Skip  (I already have a model)');
  ModelCombo.ItemIndex := 0;

  Lbl := TNewStaticText.Create(LocalPage);
  Lbl.Parent := LocalPage.Surface;
  Lbl.Caption := 'Or enter a custom model path:';
  Lbl.Top := 80;
  Lbl.Font.Style := [fsBold];
  Lbl.Font.Size := 10;

  ModelPathEdit := TNewEdit.Create(LocalPage);
  ModelPathEdit.Parent := LocalPage.Surface;
  ModelPathEdit.Top := 106;
  ModelPathEdit.Width := 440;
  ModelPathEdit.Font.Size := 10;
  ModelPathEdit.Text := '';

  Desc := TNewStaticText.Create(LocalPage);
  Desc.Parent := LocalPage.Surface;
  Desc.Top := 142;
  Desc.Width := 440;
  Desc.WordWrap := True;
  Desc.Font.Size := 9;
  Desc.Caption :=
    'If you select a model above, it will be downloaded after install' + #13#10 +
    'using download_model.bat (requires internet for first download).' + #13#10 +
    '' + #13#10 +
    'If you already have a .gguf model file, enter its full path above' + #13#10 +
    'and select "Skip" from the dropdown.' + #13#10 +
    '' + #13#10 +
    'You can always change the model later in config/config.yaml';

  // ==========================================
  // PAGE 3: API Configuration
  // ==========================================
  ApiPage := CreateCustomPage(LocalPage.ID,
    'API Configuration',
    'Enter your API credentials.');

  Lbl := TNewStaticText.Create(ApiPage);
  Lbl.Parent := ApiPage.Surface;
  Lbl.Caption := 'API Base URL:';
  Lbl.Top := 8;
  Lbl.Font.Style := [fsBold];
  Lbl.Font.Size := 10;

  ApiUrlEdit := TNewEdit.Create(ApiPage);
  ApiUrlEdit.Parent := ApiPage.Surface;
  ApiUrlEdit.Top := 34;
  ApiUrlEdit.Width := 440;
  ApiUrlEdit.Font.Size := 10;
  ApiUrlEdit.Text := 'https://api.anthropic.com';

  Lbl := TNewStaticText.Create(ApiPage);
  Lbl.Parent := ApiPage.Surface;
  Lbl.Caption := 'API Key:';
  Lbl.Top := 72;
  Lbl.Font.Style := [fsBold];
  Lbl.Font.Size := 10;

  ApiKeyEdit := TNewEdit.Create(ApiPage);
  ApiKeyEdit.Parent := ApiPage.Surface;
  ApiKeyEdit.Top := 98;
  ApiKeyEdit.Width := 440;
  ApiKeyEdit.Font.Size := 10;
  ApiKeyEdit.Text := '';

  Lbl := TNewStaticText.Create(ApiPage);
  Lbl.Parent := ApiPage.Surface;
  Lbl.Caption := 'Model Name:';
  Lbl.Top := 136;
  Lbl.Font.Style := [fsBold];
  Lbl.Font.Size := 10;

  ApiModelEdit := TNewEdit.Create(ApiPage);
  ApiModelEdit.Parent := ApiPage.Surface;
  ApiModelEdit.Top := 162;
  ApiModelEdit.Width := 440;
  ApiModelEdit.Font.Size := 10;
  ApiModelEdit.Text := 'claude-sonnet-4-20250514';

  Desc := TNewStaticText.Create(ApiPage);
  Desc.Parent := ApiPage.Surface;
  Desc.Top := 200;
  Desc.Width := 440;
  Desc.WordWrap := True;
  Desc.Font.Size := 9;
  Desc.Caption :=
    'Common URLs:' + #13#10 +
    '  Anthropic:    https://api.anthropic.com' + #13#10 +
    '  OpenAI:       https://api.openai.com' + #13#10 +
    '  LM Studio:    http://127.0.0.1:1234' + #13#10 +
    '  Ollama:       http://127.0.0.1:11434' + #13#10 +
    '  Proxy/Relay:  https://your-proxy.com' + #13#10 +
    '' + #13#10 +
    'You can change all of this later in config/config.yaml';
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  // Skip local page if API mode selected
  if PageID = LocalPage.ID then
    Result := (ModeCombo.ItemIndex <> 0);
  // Skip API page if local mode selected
  if PageID = ApiPage.ID then
    Result := (ModeCombo.ItemIndex = 0);
end;

function GetProviderMode: string;
begin
  case ModeCombo.ItemIndex of
    0: Result := 'local';
    1: Result := 'anthropic';
    2: Result := 'openai';
  else
    Result := 'local';
  end;
end;

function GetSelectedModelName: string;
begin
  case ModelCombo.ItemIndex of
    0: Result := 'qwen2.5-7b-instruct-q4_k_m.gguf';
    1: Result := 'qwen2.5-14b-instruct-q4_k_m.gguf';
    2: Result := 'qwen2.5-3b-instruct-q4_k_m.gguf';
    3: Result := 'deepseek-coder-v2-lite-instruct-q4_k_m.gguf';
  else
    Result := '';
  end;
end;

function GetModelPath: string;
begin
  if ModelPathEdit.Text <> '' then
    Result := ModelPathEdit.Text
  else if GetSelectedModelName <> '' then
    Result := 'models/' + GetSelectedModelName
  else
    Result := '';
end;

function GetSelectedModelUrl: string;
begin
  case ModelCombo.ItemIndex of
    0: Result := 'https://huggingface.co/Qwen/Qwen2.5-7B-Instruct-GGUF/resolve/main/qwen2.5-7b-instruct-q4_k_m.gguf';
    1: Result := 'https://huggingface.co/Qwen/Qwen2.5-14B-Instruct-GGUF/resolve/main/qwen2.5-14b-instruct-q4_k_m.gguf';
    2: Result := 'https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/main/qwen2.5-3b-instruct-q4_k_m.gguf';
    3: Result := 'https://huggingface.co/TheBloke/deepseek-coder-v2-lite-instruct-GGUF/resolve/main/deepseek-coder-v2-lite-instruct-q4_k_m.gguf';
  else
    Result := '';
  end;
end;

function UpdateConfigFile(ConfigPath: string): Boolean;
var
  Lines: TStringList;
  ModelPath: string;
begin
  Result := False;
  ModelPath := GetModelPath;
  Lines := TStringList.Create;
  try
    Lines.Text :=
      '# CloseCrab-Unified Configuration'#13#10 +
      '# To switch modes: change provider, then restart.'#13#10 +
      ''#13#10 +
      'server:'#13#10 +
      '  port: 9001'#13#10 +
      '  host: "127.0.0.1"'#13#10 +
      ''#13#10 +
      'database:'#13#10 +
      '  path: "data/closecrab.db"'#13#10 +
      ''#13#10 +
      'logging:'#13#10 +
      '  level: "info"'#13#10 +
      ''#13#10 +
      '# Provider: local, anthropic, openai'#13#10 +
      'provider: "' + GetProviderMode + '"'#13#10 +
      ''#13#10 +
      '# API (for anthropic/openai)'#13#10 +
      'api:'#13#10 +
      '  base_url: "' + ApiUrlEdit.Text + '"'#13#10 +
      '  api_key: "' + ApiKeyEdit.Text + '"'#13#10 +
      '  model: "' + ApiModelEdit.Text + '"'#13#10 +
      ''#13#10 +
      '# Local LLM (for local)'#13#10 +
      'llm:'#13#10 +
      '  model_path: "' + ModelPath + '"'#13#10 +
      '  max_tokens: 4096'#13#10 +
      '  temperature: 0.7'#13#10 +
      ''#13#10 +
      '# RAG'#13#10 +
      'rag:'#13#10 +
      '  embedding_model_path: "models/bge-small-zh/onnx/model_quantized.onnx"'#13#10 +
      '  embedding_tokenizer_path: "models/bge-small-zh/tokenizer.json"'#13#10 +
      '  reranker_model_path: "models/bge-reranker-base/onnx/model_uint8.onnx"'#13#10 +
      '  reranker_tokenizer_path: "models/bge-reranker-base/tokenizer.json"';
    Lines.SaveToFile(ConfigPath);
    Result := True;
  finally
    Lines.Free;
  end;
end;

function DownloadFile(Url, DestPath: string): Boolean;
var
  ResultCode: Integer;
begin
  Result := Exec('curl', '-L --retry 3 -o "' + DestPath + '" "' + Url + '"',
                 '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := Result and (ResultCode = 0);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ConfigPath, ModelDir, ModelUrl, ModelName: string;
begin
  if CurStep = ssPostInstall then
  begin
    ConfigPath := ExpandConstant('{app}\config\config.yaml');
    UpdateConfigFile(ConfigPath);

    // Download model if local mode and a model was selected (not "Skip")
    if (ModeCombo.ItemIndex = 0) and (ModelCombo.ItemIndex < 4) then
    begin
      ModelDir := ExpandConstant('{app}\models');
      CreateDir(ModelDir);
      ModelUrl := GetSelectedModelUrl;
      ModelName := GetSelectedModelName;
      if ModelUrl <> '' then
      begin
        MsgBox('Will now download ' + ModelName + '. This may take a while.', mbInformation, MB_OK);
        if not DownloadFile(ModelUrl, ModelDir + '\' + ModelName) then
          MsgBox('Download failed. Run download_model.bat later to retry.', mbError, MB_OK)
        else
          MsgBox('Model downloaded successfully!', mbInformation, MB_OK);
      end;
    end;
  end;
end;

[UninstallDelete]
Type: dirifempty; Name: "{app}\models"
Type: dirifempty; Name: "{app}\data"
Type: dirifempty; Name: "{app}\config"
Type: dirifempty; Name: "{app}\icons"
Type: dirifempty; Name: "{app}\docs"
Type: dirifempty; Name: "{app}\.claude\memory"
Type: dirifempty; Name: "{app}\.claude\skills"
Type: dirifempty; Name: "{app}\.claude\plugins"
Type: dirifempty; Name: "{app}\.claude"
