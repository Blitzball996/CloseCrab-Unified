using System;
using System.ComponentModel.Design;
using Microsoft.VisualStudio.Shell;
using Task = System.Threading.Tasks.Task;

namespace CloseCrab
{
    internal sealed class FixCommand
    {
        public static async Task InitializeAsync(AsyncPackage package)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            var commandService = await package.GetServiceAsync(typeof(IMenuCommandService)) as OleMenuCommandService;
            var cmdId = new CommandID(new Guid("c3d4e5f6-a7b8-9012-cdef-345678901234"), 0x0102);
            commandService?.AddCommand(new MenuCommand(async (s, e) => await ExecuteAsync(package), cmdId));
        }

        private static async Task ExecuteAsync(AsyncPackage package)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            var selection = ExplainCommand.GetSelectedText(package);
            if (string.IsNullOrEmpty(selection)) return;

            var client = new CloseCrabClient();
            var response = await client.ChatAsync($"Fix bugs in this code:\n```\n{selection}\n```");
            VsShellUtilities.ShowMessageBox(package, response, "CloseCrab - Fix",
                Microsoft.VisualStudio.Shell.Interop.OLEMSGICON.OLEMSGICON_INFO,
                Microsoft.VisualStudio.Shell.Interop.OLEMSGBUTTON.OLEMSGBUTTON_OK,
                Microsoft.VisualStudio.Shell.Interop.OLEMSGDEFBUTTON.OLEMSGDEFBUTTON_FIRST);
        }
    }
}
