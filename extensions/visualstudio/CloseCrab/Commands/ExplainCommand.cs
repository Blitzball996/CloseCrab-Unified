using System;
using System.ComponentModel.Design;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.TextManager.Interop;
using Task = System.Threading.Tasks.Task;

namespace CloseCrab
{
    internal sealed class ExplainCommand
    {
        public static async Task InitializeAsync(AsyncPackage package)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            var commandService = await package.GetServiceAsync(typeof(IMenuCommandService)) as OleMenuCommandService;
            var cmdId = new CommandID(new Guid("c3d4e5f6-a7b8-9012-cdef-345678901234"), 0x0101);
            commandService?.AddCommand(new MenuCommand(async (s, e) => await ExecuteAsync(package), cmdId));
        }

        private static async Task ExecuteAsync(AsyncPackage package)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            var selection = GetSelectedText(package);
            if (string.IsNullOrEmpty(selection))
            {
                VsShellUtilities.ShowMessageBox(package, "No text selected", "CloseCrab",
                    Microsoft.VisualStudio.Shell.Interop.OLEMSGICON.OLEMSGICON_INFO,
                    Microsoft.VisualStudio.Shell.Interop.OLEMSGBUTTON.OLEMSGBUTTON_OK,
                    Microsoft.VisualStudio.Shell.Interop.OLEMSGDEFBUTTON.OLEMSGDEFBUTTON_FIRST);
                return;
            }

            var client = new CloseCrabClient();
            var response = await client.ChatAsync($"Explain this code:\n```\n{selection}\n```");
            VsShellUtilities.ShowMessageBox(package, response, "CloseCrab - Explain",
                Microsoft.VisualStudio.Shell.Interop.OLEMSGICON.OLEMSGICON_INFO,
                Microsoft.VisualStudio.Shell.Interop.OLEMSGBUTTON.OLEMSGBUTTON_OK,
                Microsoft.VisualStudio.Shell.Interop.OLEMSGDEFBUTTON.OLEMSGDEFBUTTON_FIRST);
        }

        internal static string GetSelectedText(AsyncPackage package)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            var textManager = package.GetService<SVsTextManager, IVsTextManager>();
            if (textManager == null) return "";
            IVsTextView view = null;
            textManager.GetActiveView(1, null, out view);
            if (view == null) return "";
            string text = null;
            view.GetSelectedText(out text);
            return text ?? "";
        }
    }
}
