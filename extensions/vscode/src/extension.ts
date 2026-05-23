import * as vscode from 'vscode';

let chatPanel: vscode.WebviewPanel | undefined;

export function activate(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('closecrab');
    const serverUrl = config.get<string>('serverUrl', 'http://localhost:9001');

    // Register commands
    context.subscriptions.push(
        vscode.commands.registerCommand('closecrab.chat', () => openChat(context, serverUrl)),
        vscode.commands.registerCommand('closecrab.explain', () => sendSelection(serverUrl, 'explain')),
        vscode.commands.registerCommand('closecrab.fix', () => sendSelection(serverUrl, 'fix')),
        vscode.commands.registerCommand('closecrab.refactor', () => sendSelection(serverUrl, 'refactor')),
        vscode.commands.registerCommand('closecrab.test', () => sendSelection(serverUrl, 'test')),
        vscode.commands.registerCommand('closecrab.review', () => reviewFile(serverUrl))
    );

    // Register webview provider
    context.subscriptions.push(
        vscode.window.registerWebviewViewProvider('closecrab.chatView',
            new ChatViewProvider(context, serverUrl))
    );

    // Status bar
    const statusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
    statusBar.text = '$(hubot) CloseCrab';
    statusBar.command = 'closecrab.chat';
    statusBar.tooltip = 'Open CloseCrab AI Chat';
    statusBar.show();
    context.subscriptions.push(statusBar);

    // Check backend connection
    checkConnection(serverUrl).then(ok => {
        if (ok) {
            vscode.window.showInformationMessage('CloseCrab connected!');
        } else {
            vscode.window.showWarningMessage('CloseCrab backend not running. Start closecrab-unified.exe first.');
        }
    });
}

async function checkConnection(serverUrl: string): Promise<boolean> {
    try {
        const resp = await fetch(`${serverUrl}/health`);
        return resp.ok;
    } catch { return false; }
}

async function sendSelection(serverUrl: string, action: string) {
    const editor = vscode.window.activeTextEditor;
    if (!editor) return;

    const selection = editor.document.getText(editor.selection);
    if (!selection) {
        vscode.window.showWarningMessage('No text selected');
        return;
    }

    const lang = editor.document.languageId;

    let prompt = '';
    switch (action) {
        case 'explain': prompt = `Explain this ${lang} code:\n\`\`\`${lang}\n${selection}\n\`\`\``; break;
        case 'fix': prompt = `Fix any bugs in this ${lang} code:\n\`\`\`${lang}\n${selection}\n\`\`\``; break;
        case 'refactor': prompt = `Refactor this ${lang} code for better readability:\n\`\`\`${lang}\n${selection}\n\`\`\``; break;
        case 'test': prompt = `Generate unit tests for this ${lang} code:\n\`\`\`${lang}\n${selection}\n\`\`\``; break;
    }

    try {
        const resp = await fetch(`${serverUrl}/chat`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ message: prompt, session_id: 'vscode' })
        });
        const data = await resp.json() as any;

        // Show result in output channel
        const channel = vscode.window.createOutputChannel('CloseCrab');
        channel.clear();
        channel.appendLine(data.response || data.error || 'No response');
        channel.show();
    } catch (e: any) {
        vscode.window.showErrorMessage(`CloseCrab: ${e.message}`);
    }
}

async function reviewFile(serverUrl: string) {
    const editor = vscode.window.activeTextEditor;
    if (!editor) return;
    const content = editor.document.getText();
    const file = editor.document.fileName;
    const prompt = `Review this file for bugs, security issues, and improvements: ${file}\n\`\`\`\n${content.substring(0, 5000)}\n\`\`\``;

    try {
        const resp = await fetch(`${serverUrl}/chat`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ message: prompt, session_id: 'vscode' })
        });
        const data = await resp.json() as any;
        const channel = vscode.window.createOutputChannel('CloseCrab Review');
        channel.clear();
        channel.appendLine(data.response || 'No response');
        channel.show();
    } catch (e: any) {
        vscode.window.showErrorMessage(`CloseCrab: ${e.message}`);
    }
}

function openChat(context: vscode.ExtensionContext, serverUrl: string) {
    if (chatPanel) { chatPanel.reveal(); return; }

    chatPanel = vscode.window.createWebviewPanel('closecrabChat', 'CloseCrab Chat',
        vscode.ViewColumn.Beside, { enableScripts: true });

    chatPanel.webview.html = getChatHtml(serverUrl);
    chatPanel.onDidDispose(() => { chatPanel = undefined; });
}

class ChatViewProvider implements vscode.WebviewViewProvider {
    constructor(private context: vscode.ExtensionContext, private serverUrl: string) {}

    resolveWebviewView(webviewView: vscode.WebviewView) {
        webviewView.webview.options = { enableScripts: true };
        webviewView.webview.html = getChatHtml(this.serverUrl);
    }
}

function getChatHtml(serverUrl: string): string {
    return `<!DOCTYPE html>
<html>
<head>
<style>
body { font-family: var(--vscode-font-family); padding: 10px; background: var(--vscode-editor-background); color: var(--vscode-editor-foreground); }
#messages { height: calc(100vh - 80px); overflow-y: auto; }
.msg { margin: 8px 0; padding: 8px; border-radius: 6px; }
.user { background: var(--vscode-input-background); }
.assistant { background: var(--vscode-editor-inactiveSelectionBackground); }
#input { width: calc(100% - 60px); padding: 8px; border: 1px solid var(--vscode-input-border); background: var(--vscode-input-background); color: var(--vscode-input-foreground); }
#send { padding: 8px 16px; background: var(--vscode-button-background); color: var(--vscode-button-foreground); border: none; cursor: pointer; }
</style>
</head>
<body>
<div id="messages"></div>
<div style="position:fixed;bottom:10px;width:calc(100% - 20px);">
<input id="input" placeholder="Ask CloseCrab..." onkeydown="if(event.key==='Enter')send()">
<button id="send" onclick="send()">Send</button>
</div>
<script>
const SERVER = '${serverUrl}';
function send() {
    const input = document.getElementById('input');
    const msg = input.value.trim();
    if (!msg) return;
    input.value = '';
    addMsg('user', msg);
    fetch(SERVER + '/chat', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({message: msg, session_id: 'vscode'})
    }).then(r => r.json()).then(d => {
        addMsg('assistant', d.response || d.error || 'No response');
    }).catch(e => addMsg('assistant', 'Error: ' + e.message));
}
function addMsg(role, text) {
    const div = document.createElement('div');
    div.className = 'msg ' + role;
    div.textContent = text;
    document.getElementById('messages').appendChild(div);
    div.scrollIntoView();
}
</script>
</body>
</html>`;
}

export function deactivate() {}