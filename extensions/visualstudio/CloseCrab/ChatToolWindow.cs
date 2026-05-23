using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using Microsoft.VisualStudio.Shell;

namespace CloseCrab
{
    [Guid("b2c3d4e5-f6a7-8901-bcde-f23456789012")]
    public class ChatToolWindow : ToolWindowPane
    {
        public ChatToolWindow() : base(null)
        {
            Caption = "CloseCrab AI";
            Content = new ChatControl();
        }
    }

    public class ChatControl : UserControl
    {
        private readonly TextBox _chatBox;
        private readonly TextBox _inputBox;
        private readonly Button _sendButton;
        private readonly CloseCrabClient _client;

        public ChatControl()
        {
            _client = new CloseCrabClient();

            var grid = new Grid();
            grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

            _chatBox = new TextBox
            {
                IsReadOnly = true,
                TextWrapping = TextWrapping.Wrap,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                Margin = new Thickness(5)
            };
            Grid.SetRow(_chatBox, 0);
            grid.Children.Add(_chatBox);

            var inputPanel = new DockPanel { Margin = new Thickness(5) };
            _sendButton = new Button { Content = "Send", Width = 60, Margin = new Thickness(5, 0, 0, 0) };
            DockPanel.SetDock(_sendButton, Dock.Right);
            _inputBox = new TextBox { Margin = new Thickness(0) };
            inputPanel.Children.Add(_sendButton);
            inputPanel.Children.Add(_inputBox);
            Grid.SetRow(inputPanel, 1);
            grid.Children.Add(inputPanel);

            Content = grid;

            _sendButton.Click += async (s, e) => await SendMessageAsync();
            _inputBox.KeyDown += async (s, e) =>
            {
                if (e.Key == System.Windows.Input.Key.Enter) await SendMessageAsync();
            };

            CheckConnection();
        }

        private async void CheckConnection()
        {
            var connected = await _client.IsConnectedAsync();
            _chatBox.Text = connected
                ? "[CloseCrab] Connected to backend.\n"
                : "[CloseCrab] Backend not running. Start closecrab-unified.exe first.\n";
        }

        private async System.Threading.Tasks.Task SendMessageAsync()
        {
            var msg = _inputBox.Text.Trim();
            if (string.IsNullOrEmpty(msg)) return;
            _inputBox.Text = "";
            _chatBox.AppendText($"\nYou: {msg}\n");

            var response = await _client.ChatAsync(msg);
            _chatBox.AppendText($"\nCloseCrab: {response}\n");
            _chatBox.ScrollToEnd();
        }
    }
}
