using System;
using System.ComponentModel.Design;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Task = System.Threading.Tasks.Task;

namespace CloseCrab
{
    internal sealed class ChatCommand
    {
        public static async Task InitializeAsync(AsyncPackage package)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            var commandService = await package.GetServiceAsync(typeof(IMenuCommandService)) as OleMenuCommandService;
            var cmdId = new CommandID(new Guid("c3d4e5f6-a7b8-9012-cdef-345678901234"), 0x0100);
            commandService?.AddCommand(new MenuCommand((s, e) => Execute(package), cmdId));
        }

        private static void Execute(AsyncPackage package)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            var window = package.FindToolWindow(typeof(ChatToolWindow), 0, true);
            if (window?.Frame == null) return;
            var frame = (IVsWindowFrame)window.Frame;
            frame.Show();
        }
    }
}
